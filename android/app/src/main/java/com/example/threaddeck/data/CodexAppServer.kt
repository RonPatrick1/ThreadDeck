package com.threaddeck.tablet.data

import org.json.JSONArray
import org.json.JSONObject
import java.net.HttpURLConnection
import java.net.URI
import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.atomic.AtomicLong

class CodexAppServer(
  private val endpoint: String,
  private val listener: Listener,
) : RawWebSocketClient.Listener {
  interface Listener {
    fun onConnecting(endpoint: String)
    fun onReady(initializeResult: JSONObject)
    fun onDisconnected(reason: String)
    fun onNotification(method: String, params: JSONObject)
    fun onServerRequest(request: ServerRequest)
    fun onError(message: String)
  }

  data class ServerRequest(
    val id: Any,
    val method: String,
    val params: JSONObject,
  )

  data class ThreadTurnSettings(
    val approvalPolicy: String,
    val sandboxPolicy: JSONObject,
    val model: String,
    val effort: String,
    val shieldEnabled: Boolean,
    val remoteShieldHosts: List<String>,
  )

  data class TurnAttachment(
    val path: String,
    val kind: String,
  )

  private val nextRequestId = AtomicLong(1)
  private val pending = ConcurrentHashMap<String, (JSONObject) -> Unit>()
  private var webSocket: RawWebSocketClient? = null

  fun connect() {
    disconnect()
    listener.onConnecting(endpoint)
    webSocket = RawWebSocketClient(endpoint, this).also { it.connect() }
  }

  fun disconnect() {
    webSocket?.close()
    webSocket = null
    pending.clear()
  }

  override fun onOpen() {
    request(
      method = "initialize",
      params =
        JSONObject()
          .put(
            "clientInfo",
            JSONObject()
              .put("name", "threaddeck-android")
              .put("title", "ThreadDeck for Android")
              .put("version", "0.1.0"),
          )
          .put("capabilities", JSONObject().put("experimentalApi", true)),
    ) { message ->
      val error = message.optJSONObject("error")
      if (error != null) {
        listener.onError("Codex initialization failed: ${error.optString("message", error.toString())}")
        return@request
      }

      val notification = JSONObject().put("method", "initialized")
      if (webSocket?.send(notification.toString()) != true) {
        listener.onError("Could not finish Codex initialization")
        return@request
      }

      listener.onReady(message.optJSONObject("result") ?: JSONObject())
    }
  }

  override fun onMessage(text: String) {
    val message =
      runCatching { JSONObject(text) }
        .getOrElse {
          listener.onError("The host sent invalid JSON: ${it.message}")
          return
        }

    val method = message.optString("method")

    if (method.isNotEmpty() && message.has("id")) {
      listener.onServerRequest(
        ServerRequest(
          id = message.get("id"),
          method = method,
          params = message.optJSONObject("params") ?: JSONObject(),
        ),
      )
      return
    }

    if (message.has("id")) {
      val callback = pending.remove(message.get("id").toString())
      if (callback != null) callback(message)
      return
    }

    if (method.isNotEmpty()) {
      listener.onNotification(method, message.optJSONObject("params") ?: JSONObject())
    }
  }

  override fun onClosed(reason: String) {
    pending.clear()
    listener.onDisconnected(reason)
  }

  override fun onFailure(error: Throwable) {
    pending.clear()
    listener.onDisconnected(error.message ?: error.javaClass.simpleName)
  }

  fun listThreads(
    search: String = "",
    cwd: String = "",
    callback: (JSONObject) -> Unit,
  ) {
    val params =
      JSONObject()
        .put("archived", false)
        .put("limit", 200)
        .put("sortDirection", "desc")
        .put("sortKey", "recency_at")
        .put("useStateDbOnly", false)
    if (search.isNotBlank()) params.put("searchTerm", search.trim())
    if (cwd.isNotBlank()) params.put("cwd", cwd)
    request("thread/list", params, callback)
  }

  fun listModels(callback: (JSONObject) -> Unit) {
    request(
      "model/list",
      JSONObject()
        .put("limit", 100)
        .put("includeHidden", false),
      callback,
    )
  }

  fun readRateLimits(callback: (JSONObject) -> Unit) {
    request("account/rateLimits/read", null, callback)
  }

  fun readAccountUsage(callback: (JSONObject) -> Unit) {
    request("account/usage/read", null, callback)
  }

  fun listSkills(cwd: String, callback: (JSONObject) -> Unit) {
    request(
      "skills/list",
      JSONObject()
        .put("cwds", JSONArray().put(cwd))
        .put("forceReload", false),
      callback,
    )
  }

  fun loadThreadDeckState(callback: (Result<JSONObject>) -> Unit) {
    Thread(
      {
        callback(
          runCatching {
            val connection = threadDeckStateUri().toURL().openConnection() as HttpURLConnection
            try {
              connection.connectTimeout = 10_000
              connection.readTimeout = 10_000
              connection.useCaches = false
              connection.setRequestProperty("Accept", "application/json")
              val status = connection.responseCode
              require(status == HttpURLConnection.HTTP_OK) {
                "ThreadDeck metadata request failed with HTTP $status"
              }
              JSONObject(connection.inputStream.bufferedReader().use { it.readText() })
            } finally {
              connection.disconnect()
            }
          },
        )
      },
      "threaddeck-metadata",
    ).start()
  }

  fun updateThreadDeckState(
    updates: JSONObject,
    callback: (Result<JSONObject>) -> Unit,
  ) {
    Thread(
      {
        callback(
          runCatching {
            val connection = threadDeckStateUri().toURL().openConnection() as HttpURLConnection
            try {
              val payload = JSONObject().put("updates", updates).toString().toByteArray(Charsets.UTF_8)
              connection.connectTimeout = 10_000
              connection.readTimeout = 10_000
              connection.useCaches = false
              connection.requestMethod = "POST"
              connection.doOutput = true
              connection.setFixedLengthStreamingMode(payload.size)
              connection.setRequestProperty("Accept", "application/json")
              connection.setRequestProperty("Content-Type", "application/json; charset=utf-8")
              connection.outputStream.use { it.write(payload) }
              val status = connection.responseCode
              val stream =
                if (status in 200..299) connection.inputStream
                else connection.errorStream
              val responseText = stream?.bufferedReader()?.use { it.readText() }.orEmpty()
              require(status == HttpURLConnection.HTTP_OK) {
                runCatching { JSONObject(responseText).optString("error") }.getOrNull()
                  ?.takeIf(String::isNotBlank)
                  ?: "ThreadDeck settings update failed with HTTP $status"
              }
              JSONObject(responseText)
            } finally {
              connection.disconnect()
            }
          },
        )
      },
      "threaddeck-settings-update",
    ).start()
  }

  fun uploadAttachment(
    bytes: ByteArray,
    kind: String,
    extension: String,
    callback: (Result<JSONObject>) -> Unit,
  ) {
    Thread(
      {
        callback(
          runCatching {
            val websocketUri = URI(endpoint)
            val uploadUri =
              URI(
                if (websocketUri.scheme == "wss") "https" else "http",
                null,
                websocketUri.host,
                websocketUri.port,
                "/threaddeck/attachment/${if (kind == "audio") "audio" else "image"}",
                null,
                null,
              )
            val connection = uploadUri.toURL().openConnection() as HttpURLConnection
            try {
              connection.connectTimeout = 15_000
              connection.readTimeout = 60_000
              connection.useCaches = false
              connection.requestMethod = "POST"
              connection.doOutput = true
              connection.setFixedLengthStreamingMode(bytes.size)
              connection.setRequestProperty("Accept", "application/json")
              connection.setRequestProperty("Content-Type", "application/octet-stream")
              connection.setRequestProperty("X-ThreadDeck-Extension", extension)
              connection.outputStream.use { it.write(bytes) }
              val status = connection.responseCode
              val stream =
                if (status in 200..299) connection.inputStream
                else connection.errorStream
              val responseText = stream?.bufferedReader()?.use { it.readText() }.orEmpty()
              require(status == HttpURLConnection.HTTP_OK) {
                runCatching { JSONObject(responseText).optString("error") }.getOrNull()
                  ?.takeIf(String::isNotBlank)
                  ?: "Attachment upload failed with HTTP $status"
              }
              JSONObject(responseText)
            } finally {
              connection.disconnect()
            }
          },
        )
      },
      "threaddeck-attachment-upload",
    ).start()
  }

  private fun threadDeckStateUri(): URI {
    val websocketUri = URI(endpoint)
    return URI(
      if (websocketUri.scheme == "wss") "https" else "http",
      null,
      websocketUri.host,
      websocketUri.port,
      "/threaddeck/ui-state",
      null,
      null,
    )
  }

  fun resumeThread(threadId: String, callback: (JSONObject) -> Unit) {
    request(
      "thread/resume",
      JSONObject()
        .put("threadId", threadId)
        .put("excludeTurns", false),
      callback,
    )
  }

  fun readThread(threadId: String, callback: (JSONObject) -> Unit) {
    request(
      "thread/read",
      JSONObject()
        .put("threadId", threadId)
        .put("includeTurns", true),
      callback,
    )
  }

  fun startThread(cwd: String, callback: (JSONObject) -> Unit) {
    request(
      "thread/start",
      JSONObject()
        .put("cwd", cwd)
        .put("ephemeral", false),
      callback,
    )
  }

  fun deleteThread(threadId: String, callback: (JSONObject) -> Unit) {
    request(
      "thread/delete",
      JSONObject().put("threadId", threadId),
      callback,
    )
  }

  fun updateThreadCwd(
    threadId: String,
    cwd: String,
    callback: (JSONObject) -> Unit,
  ) {
    request(
      "thread/settings/update",
      JSONObject()
        .put("threadId", threadId)
        .put("cwd", cwd),
      callback,
    )
  }

  fun startTurn(
    threadId: String,
    text: String,
    settings: ThreadTurnSettings,
    attachments: List<TurnAttachment> = emptyList(),
    callback: (JSONObject) -> Unit,
  ) {
    val input = JSONArray()
    if (text.isNotBlank()) {
      input.put(
        JSONObject()
          .put("type", "text")
          .put("text", text)
          .put("text_elements", JSONArray()),
      )
    }
    attachments.forEach { attachment ->
      input.put(
        JSONObject()
          .put("type", if (attachment.kind == "audio") "localAudio" else "localImage")
          .put("path", attachment.path),
      )
    }
    val params =
      JSONObject()
        .put("threadId", threadId)
        .put("input", input)
        .put("approvalPolicy", settings.approvalPolicy)
        .put("sandboxPolicy", settings.sandboxPolicy)
        .put("additionalContext", additionalContext(settings.shieldEnabled, settings.remoteShieldHosts))
    if (settings.model.isNotBlank()) params.put("model", settings.model)
    if (settings.effort.isNotBlank()) params.put("effort", settings.effort)

    request(
      "turn/start",
      params,
      callback,
    )
  }

  fun steerTurn(
    threadId: String,
    turnId: String,
    text: String,
    settings: ThreadTurnSettings,
    attachments: List<TurnAttachment> = emptyList(),
    callback: (JSONObject) -> Unit,
  ) {
    request(
      "turn/steer",
      JSONObject()
        .put("threadId", threadId)
        .put("expectedTurnId", turnId)
        .put("additionalContext", additionalContext(settings.shieldEnabled, settings.remoteShieldHosts))
        .put(
          "input",
          JSONArray().also { input ->
            if (text.isNotBlank()) {
              input.put(
                JSONObject()
                  .put("type", "text")
                  .put("text", text)
                  .put("text_elements", JSONArray()),
              )
            }
            attachments.forEach { attachment ->
              input.put(
                JSONObject()
                  .put("type", if (attachment.kind == "audio") "localAudio" else "localImage")
                  .put("path", attachment.path),
              )
            }
          },
        ),
      callback,
    )
  }

  private fun additionalContext(
    shieldEnabled: Boolean,
    remoteShieldHosts: List<String>,
  ): JSONObject {
    val context =
      JSONObject().put(
        "threaddeck.command-copy-format",
        JSONObject()
          .put("kind", "application")
          .put(
            "value",
            "When you provide a shell command that the user should copy and run themselves, put only the complete command in its own fenced code block labeled bash (or the appropriate shell). Keep explanations, command output, and other prose outside that fenced block.",
          ),
      )
    if (shieldEnabled) {
      context.put(
        "threaddeck.shield",
        JSONObject()
          .put("kind", "application")
          .put(
            "value",
            "ThreadDeck Shield is enabled for this local thread. A ThreadDeck-managed sudo command is available in PATH and uses the user's active privileged authorization. Use sudo when local privileged work requires it; do not claim that sudo is unavailable merely because it would normally prompt for a password. Shield is independent of YOLO: normal Codex approvals and sandbox policy still apply unless YOLO is also enabled. Shield applies only to this local ThreadDeck process and does not provide sudo authentication on remote hosts reached through SSH.",
          ),
      )
    }
    if (remoteShieldHosts.isNotEmpty()) {
      context.put(
        "threaddeck.remote-shield",
        JSONObject()
          .put("kind", "application")
          .put(
            "value",
            "ThreadDeck Remote Shield is enabled for this thread on these SSH destinations: ${remoteShieldHosts.joinToString(", ")}. SSH login uses the user's normal OpenSSH keys and host verification. For a privileged remote command, use the normal form `ssh HOST sudo COMMAND`; ThreadDeck supplies that host's saved sudo credential securely. Do not request or print the password, do not add `sudo -S`, and do not claim remote sudo is unavailable without attempting the enabled workflow. Remote Shield is independent of YOLO and local Shield.",
          ),
      )
    }
    return context
  }

  fun interruptTurn(threadId: String, turnId: String) {
    request(
      "turn/interrupt",
      JSONObject().put("threadId", threadId).put("turnId", turnId),
    ) { message ->
      message.optJSONObject("error")?.let {
        listener.onError("Could not stop the turn: ${it.optString("message", it.toString())}")
      }
    }
  }

  fun respondToApproval(request: ServerRequest, decision: String) {
    val result =
      if (request.method == "item/permissions/requestApproval") {
        val approved = decision == "accept" || decision == "acceptForSession"
        JSONObject()
          .put(
            "permissions",
            if (approved) request.params.optJSONObject("permissions") ?: JSONObject() else JSONObject(),
          )
          .put("scope", if (decision == "acceptForSession") "session" else "turn")
      } else {
        JSONObject().put("decision", decision)
      }

    send(JSONObject().put("id", request.id).put("result", result))
  }

  private fun request(
    method: String,
    params: JSONObject? = null,
    callback: (JSONObject) -> Unit,
  ) {
    val id = nextRequestId.getAndIncrement()
    val message = JSONObject().put("id", id).put("method", method)
    if (params != null) message.put("params", params)
    pending[id.toString()] = callback

    if (!send(message)) {
      pending.remove(id.toString())
      listener.onError("Could not send $method to the ThreadDeck host")
    }
  }

  private fun send(message: JSONObject): Boolean = webSocket?.send(message.toString()) == true
}
