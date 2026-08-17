package com.threaddeck.tablet.data

import android.util.Base64
import java.io.BufferedInputStream
import java.io.BufferedOutputStream
import java.io.EOFException
import java.net.InetSocketAddress
import java.net.Socket
import java.net.URI
import java.security.MessageDigest
import java.security.SecureRandom
import java.util.concurrent.Executors
import java.util.concurrent.atomic.AtomicBoolean
import javax.net.ssl.SSLSocketFactory

class RawWebSocketClient(
  private val endpoint: String,
  private val listener: Listener,
) {
  interface Listener {
    fun onOpen()
    fun onMessage(text: String)
    fun onClosed(reason: String)
    fun onFailure(error: Throwable)
  }

  private val running = AtomicBoolean(false)
  private val terminalDelivered = AtomicBoolean(false)
  private val writeLock = Any()
  private val random = SecureRandom()
  private val writer = Executors.newSingleThreadExecutor { task ->
    Thread(task, "threaddeck-websocket-writer")
  }
  private var socket: Socket? = null
  private var input: BufferedInputStream? = null
  private var output: BufferedOutputStream? = null

  fun connect() {
    if (!running.compareAndSet(false, true)) return

    Thread({ runConnection() }, "threaddeck-websocket").start()
  }

  fun send(text: String): Boolean {
    if (!running.get()) return false
    writer.execute {
      if (!writeFrame(0x1, text.toByteArray(Charsets.UTF_8))) {
        running.set(false)
        runCatching { socket?.close() }
      }
    }
    return true
  }

  fun close() {
    if (!running.getAndSet(false)) return

    runCatching { writeFrame(0x8, byteArrayOf()) }
    runCatching { socket?.close() }
  }

  private fun runConnection() {
    try {
      val uri = URI(endpoint)
      require(uri.scheme == "ws" || uri.scheme == "wss") { "Use a ws:// or wss:// address" }
      val host = requireNotNull(uri.host) { "The ThreadDeck host is missing" }
      val secure = uri.scheme == "wss"
      val port = if (uri.port > 0) uri.port else if (secure) 443 else 80
      val connection =
        if (secure) {
          (SSLSocketFactory.getDefault().createSocket() as Socket)
        } else {
          Socket()
        }

      connection.tcpNoDelay = true
      connection.keepAlive = true
      connection.connect(InetSocketAddress(host, port), 10_000)

      socket = connection
      input = BufferedInputStream(connection.getInputStream(), 16 * 1024)
      output = BufferedOutputStream(connection.getOutputStream(), 16 * 1024)

      performHandshake(uri, host, port, secure)
      listener.onOpen()
      readFrames()
    } catch (error: Throwable) {
      if (running.get()) notifyFailure(error)
    } finally {
      val wasRunning = running.getAndSet(false)
      runCatching { socket?.close() }
      socket = null
      input = null
      output = null
      writer.shutdownNow()
      if (wasRunning) notifyClosed("Connection closed")
    }
  }

  private fun performHandshake(uri: URI, host: String, port: Int, secure: Boolean) {
    val nonce = ByteArray(16).also(random::nextBytes)
    val key = Base64.encodeToString(nonce, Base64.NO_WRAP)
    val path =
      buildString {
        append(if (uri.rawPath.isNullOrEmpty()) "/" else uri.rawPath)
        if (!uri.rawQuery.isNullOrEmpty()) append('?').append(uri.rawQuery)
      }
    val defaultPort = if (secure) 443 else 80
    val hostHeader = if (port == defaultPort) host else "$host:$port"
    val request =
      buildString {
        append("GET ").append(path).append(" HTTP/1.1\r\n")
        append("Host: ").append(hostHeader).append("\r\n")
        append("Upgrade: websocket\r\n")
        append("Connection: Upgrade\r\n")
        append("Sec-WebSocket-Key: ").append(key).append("\r\n")
        append("Sec-WebSocket-Version: 13\r\n\r\n")
      }

    synchronized(writeLock) {
      outputOrThrow().write(request.toByteArray(Charsets.US_ASCII))
      outputOrThrow().flush()
    }

    val response = readHttpHeaders()
    val status = response.lineSequence().firstOrNull().orEmpty()
    require(status.contains(" 101 ")) { "ThreadDeck bridge rejected the connection: $status" }

    val expectedAccept =
      Base64.encodeToString(
        MessageDigest.getInstance("SHA-1")
          .digest((key + WEBSOCKET_GUID).toByteArray(Charsets.US_ASCII)),
        Base64.NO_WRAP,
      )
    val accept =
      response.lineSequence()
        .firstOrNull { it.startsWith("Sec-WebSocket-Accept:", ignoreCase = true) }
        ?.substringAfter(':')
        ?.trim()

    require(accept == expectedAccept) { "ThreadDeck bridge returned an invalid WebSocket handshake" }
  }

  private fun readHttpHeaders(): String {
    val bytes = ArrayList<Byte>(1024)
    var matched = 0
    val terminator = byteArrayOf(13, 10, 13, 10)

    while (bytes.size < 16 * 1024) {
      val value = inputOrThrow().read()
      if (value < 0) throw EOFException("Connection closed during WebSocket handshake")
      val byte = value.toByte()
      bytes += byte

      if (byte == terminator[matched]) {
        matched += 1
        if (matched == terminator.size) break
      } else {
        matched = if (byte == terminator[0]) 1 else 0
      }
    }

    require(matched == terminator.size) { "ThreadDeck bridge returned oversized HTTP headers" }
    return bytes.toByteArray().toString(Charsets.US_ASCII)
  }

  private fun readFrames() {
    var messageOpcode = -1
    var message = ByteArray(0)

    while (running.get()) {
      val first = readByte()
      val second = readByte()
      val finished = first and 0x80 != 0
      val opcode = first and 0x0f
      val masked = second and 0x80 != 0
      var payloadLength = (second and 0x7f).toLong()

      payloadLength =
        when (payloadLength) {
          126L -> (readByte().toLong() shl 8) or readByte().toLong()
          127L -> {
            var length = 0L
            repeat(8) { length = (length shl 8) or readByte().toLong() }
            length
          }
          else -> payloadLength
        }

      require(payloadLength in 0..MAX_MESSAGE_SIZE) { "WebSocket message is too large" }
      val mask = if (masked) readExactly(4) else null
      val payload = readExactly(payloadLength.toInt())

      if (mask != null) {
        payload.indices.forEach { index ->
          payload[index] = (payload[index].toInt() xor mask[index % 4].toInt()).toByte()
        }
      }

      when (opcode) {
        0x8 -> {
          running.set(false)
          notifyClosed("Host closed the connection")
          return
        }
        0x9 -> writeFrame(0xA, payload)
        0xA -> Unit
        0x1, 0x2 -> {
          messageOpcode = opcode
          message = payload
          if (finished) {
            deliverMessage(messageOpcode, message)
            messageOpcode = -1
            message = ByteArray(0)
          }
        }
        0x0 -> {
          require(messageOpcode >= 0) { "Unexpected continuation frame" }
          message += payload
          require(message.size <= MAX_MESSAGE_SIZE) { "WebSocket message is too large" }
          if (finished) {
            deliverMessage(messageOpcode, message)
            messageOpcode = -1
            message = ByteArray(0)
          }
        }
        else -> error("Unsupported WebSocket frame type: $opcode")
      }
    }
  }

  private fun deliverMessage(opcode: Int, payload: ByteArray) {
    require(opcode == 0x1) { "ThreadDeck received an unsupported binary message" }
    listener.onMessage(payload.toString(Charsets.UTF_8))
  }

  private fun writeFrame(opcode: Int, payload: ByteArray): Boolean {
    if (!running.get() && opcode != 0x8) return false

    val result = runCatching {
      val mask = ByteArray(4).also(random::nextBytes)
      val header = ArrayList<Byte>(14)
      header += (0x80 or (opcode and 0x0f)).toByte()

      when {
        payload.size <= 125 -> header += (0x80 or payload.size).toByte()
        payload.size <= 0xffff -> {
          header += (0x80 or 126).toByte()
          header += (payload.size ushr 8).toByte()
          header += payload.size.toByte()
        }
        else -> {
          header += (0x80 or 127).toByte()
          val length = payload.size.toLong()
          for (shift in 56 downTo 0 step 8) header += (length ushr shift).toByte()
        }
      }

      mask.forEach { header += it }
      val maskedPayload = ByteArray(payload.size) { index ->
        (payload[index].toInt() xor mask[index % mask.size].toInt()).toByte()
      }

      synchronized(writeLock) {
        outputOrThrow().write(header.toByteArray())
        outputOrThrow().write(maskedPayload)
        outputOrThrow().flush()
      }
    }

    result.exceptionOrNull()?.let {
      if (running.get()) notifyFailure(it)
    }
    return result.isSuccess
  }

  private fun readByte(): Int {
    val value = inputOrThrow().read()
    if (value < 0) throw EOFException("WebSocket closed")
    return value
  }

  private fun readExactly(size: Int): ByteArray {
    val bytes = ByteArray(size)
    var offset = 0
    while (offset < size) {
      val count = inputOrThrow().read(bytes, offset, size - offset)
      if (count < 0) throw EOFException("WebSocket closed mid-frame")
      offset += count
    }
    return bytes
  }

  private fun inputOrThrow() = requireNotNull(input) { "WebSocket input is unavailable" }
  private fun outputOrThrow() = requireNotNull(output) { "WebSocket output is unavailable" }

  private fun notifyFailure(error: Throwable) {
    if (terminalDelivered.compareAndSet(false, true)) listener.onFailure(error)
  }

  private fun notifyClosed(reason: String) {
    if (terminalDelivered.compareAndSet(false, true)) listener.onClosed(reason)
  }

  companion object {
    private const val WEBSOCKET_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
    private const val MAX_MESSAGE_SIZE = 64 * 1024 * 1024
  }
}
