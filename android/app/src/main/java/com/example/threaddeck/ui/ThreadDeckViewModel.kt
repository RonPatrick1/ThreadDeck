package com.threaddeck.tablet.ui

import android.app.Application
import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import android.net.Uri
import android.provider.OpenableColumns
import android.util.Log
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.threaddeck.tablet.data.CodexAppServer
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import org.json.JSONArray
import org.json.JSONObject
import java.text.DateFormat
import java.util.Date
import java.util.UUID

enum class HostConnectionState {
  DISCONNECTED,
  CONNECTING,
  CONNECTED,
}

data class ThreadSummary(
  val id: String,
  val title: String,
  val cwd: String,
  val projectId: String,
  val updatedLabel: String,
  val updatedAt: Long,
  val pinned: Boolean,
)

data class ProjectThreads(
  val id: String,
  val title: String,
  val cwd: String,
  val threads: List<ThreadSummary>,
)

data class ModelChoice(
  val id: String,
  val title: String,
  val defaultEffort: String,
  val reasoningEfforts: List<String>,
)

data class SkillChoice(
  val name: String,
  val description: String,
)

data class RemoteHostChoice(
  val id: String,
  val title: String,
  val credentialSaved: Boolean,
)

private data class ThreadDeckMetadata(
  val projectIds: List<String> = emptyList(),
  val projectPaths: Map<String, String> = emptyMap(),
  val projectLabels: Map<String, String> = emptyMap(),
  val threadLabels: Map<String, String> = emptyMap(),
  val threadProjectAssignments: Map<String, String> = emptyMap(),
  val projectThreadSorts: Map<String, String> = emptyMap(),
  val projectSort: String = "updated-desc",
  val threadAccessSelections: Map<String, String> = emptyMap(),
  val threadShieldSelections: Set<String> = emptySet(),
  val threadAutoCopySelections: Set<String> = emptySet(),
  val pausedThreads: Set<String> = emptySet(),
  val remoteHostLabels: Map<String, String> = emptyMap(),
  val remoteHostCredentialSaved: Set<String> = emptySet(),
  val threadRemoteShieldHosts: Map<String, Set<String>> = emptyMap(),
  val threadConfiguredApprovalPolicies: Map<String, String> = emptyMap(),
  val threadConfiguredSandboxPolicies: Map<String, JSONObject> = emptyMap(),
  val threadModelSelections: Map<String, String> = emptyMap(),
  val threadReasoningSelections: Map<String, String> = emptyMap(),
  val themeId: String = "system",
  val sidebarVisible: Boolean = true,
)

enum class TranscriptKind {
  USER,
  AGENT,
  COMMENTARY,
  REASONING,
  PLAN,
  ACTIVITY,
}

data class TranscriptEntry(
  val id: String,
  val kind: TranscriptKind,
  val heading: String,
  val text: String,
  val running: Boolean = false,
)

data class ApprovalPrompt(
  val request: CodexAppServer.ServerRequest,
  val title: String,
  val details: String,
)

data class TabletAttachment(
  val id: String,
  val name: String,
  val kind: String,
  val hostPath: String = "",
  val uploading: Boolean = true,
)

data class ThreadDeckUiState(
  val connection: HostConnectionState = HostConnectionState.DISCONNECTED,
  val endpoint: String = DEFAULT_ENDPOINT,
  val connectionMessage: String = "Not connected",
  val threads: List<ThreadSummary> = emptyList(),
  val projects: List<ProjectThreads> = emptyList(),
  val collapsedProjects: Set<String> = emptySet(),
  val navigationVisible: Boolean = true,
  val selectedThreadId: String? = null,
  val selectedThreadTitle: String = "Select a thread",
  val selectedThreadCwd: String = "",
  val transcript: List<TranscriptEntry> = emptyList(),
  val loadingThreads: Boolean = false,
  val loadingTranscript: Boolean = false,
  val activeTurnId: String? = null,
  val turnRunning: Boolean = false,
  val threadWritable: Boolean = false,
  val selectedAccessMode: String = "configured",
  val selectedShieldEnabled: Boolean = false,
  val selectedAutoCopyEnabled: Boolean = false,
  val selectedPaused: Boolean = false,
  val selectedRemoteShieldHosts: Set<String> = emptySet(),
  val remoteHosts: List<RemoteHostChoice> = emptyList(),
  val selectedModelId: String = "",
  val selectedReasoningEffort: String = "",
  val models: List<ModelChoice> = emptyList(),
  val skills: List<SkillChoice> = emptyList(),
  val usageLabel: String = "Usage —",
  val usageDetails: String = "Usage information is not available yet.",
  val search: String = "",
  val draft: String = "",
  val attachments: List<TabletAttachment> = emptyList(),
  val themeId: String = "midnight-ocean",
  val approval: ApprovalPrompt? = null,
  val message: String? = null,
  val showThemePicker: Boolean = false,
  val showConnectionDialog: Boolean = false,
  val showNewThreadDialog: Boolean = false,
  val showNewProjectDialog: Boolean = false,
  val showYoloConfirmation: Boolean = false,
  val showModelPicker: Boolean = false,
  val showReasoningPicker: Boolean = false,
  val showDetailsDialog: Boolean = false,
  val showSkillPicker: Boolean = false,
  val showRemoteShieldDialog: Boolean = false,
)

interface ThreadDeckActions {
  fun reconnect()
  fun setEndpoint(endpoint: String)
  fun selectThread(threadId: String)
  fun refreshThreads()
  fun setNavigationVisible(visible: Boolean)
  fun toggleProject(projectId: String)
  fun setAllProjectsCollapsed(collapsed: Boolean)
  fun setSearch(search: String)
  fun setDraft(draft: String)
  fun addAttachments(uriStrings: List<String>, kind: String)
  fun removeAttachment(id: String)
  fun clearAttachments()
  fun requestAccessMode(accessMode: String)
  fun confirmYolo(enable: Boolean)
  fun setShieldEnabled(enabled: Boolean)
  fun setAutoCopyEnabled(enabled: Boolean)
  fun togglePause()
  fun continueThread()
  fun selectModel(modelId: String)
  fun selectReasoningEffort(effort: String)
  fun showModelPicker(show: Boolean)
  fun showReasoningPicker(show: Boolean)
  fun showDetailsDialog(show: Boolean)
  fun showSkillPicker(show: Boolean)
  fun selectSkill(name: String)
  fun showRemoteShieldDialog(show: Boolean)
  fun setRemoteShieldHost(host: String, enabled: Boolean)
  fun send()
  fun stopTurn()
  fun createThread(cwd: String)
  fun addProject(cwd: String)
  fun renameProject(projectId: String, label: String)
  fun deleteProject(projectId: String)
  fun setProjectSort(sortId: String)
  fun setThreadSort(projectId: String, sortId: String)
  fun renameThread(threadId: String, label: String)
  fun summarizeAndTitle(threadId: String)
  fun moveThread(threadId: String, projectId: String)
  fun deleteThread(threadId: String)
  fun answerApproval(decision: String)
  fun setTheme(themeId: String)
  fun showThemePicker(show: Boolean)
  fun showConnectionDialog(show: Boolean)
  fun showNewThreadDialog(show: Boolean)
  fun showNewProjectDialog(show: Boolean)
  fun clearMessage()
}

class ThreadDeckViewModel(application: Application) :
  AndroidViewModel(application),
  ThreadDeckActions,
  CodexAppServer.Listener {

  private val preferences = application.getSharedPreferences("threaddeck", 0)
  private val _uiState =
    MutableStateFlow(
      ThreadDeckUiState(
        endpoint = preferences.getString("endpoint", DEFAULT_ENDPOINT) ?: DEFAULT_ENDPOINT,
        themeId = preferences.getString("theme", "midnight-ocean") ?: "midnight-ocean",
        selectedThreadId = preferences.getString("selected_thread", null),
        collapsedProjects =
          preferences.getStringSet("collapsed_projects", emptySet())?.toSet().orEmpty(),
        navigationVisible = preferences.getBoolean("navigation_visible", true),
      ),
    )
  val uiState: StateFlow<ThreadDeckUiState> = _uiState.asStateFlow()

  private var appServer: CodexAppServer? = null
  private var metadata = ThreadDeckMetadata()
  private var refreshGeneration = 0L
  private var reconnectJob: Job? = null
  private var threadResumeJob: Job? = null
  private var reconnectAttempt = 0

  init {
    connect()
  }

  override fun onCleared() {
    reconnectJob?.cancel()
    threadResumeJob?.cancel()
    appServer?.disconnect()
    super.onCleared()
  }

  private fun connect() {
    reconnectJob?.cancel()
    reconnectJob = null
    val endpoint = _uiState.value.endpoint.trim().ifEmpty { DEFAULT_ENDPOINT }
    appServer?.disconnect()
    appServer = CodexAppServer(endpoint, this).also { it.connect() }
  }

  override fun reconnect() {
    reconnectAttempt = 0
    connect()
  }

  override fun setEndpoint(endpoint: String) {
    val normalized = endpoint.trim().ifEmpty { DEFAULT_ENDPOINT }
    preferences.edit().putString("endpoint", normalized).apply()
    _uiState.value =
      _uiState.value.copy(
        endpoint = normalized,
        showConnectionDialog = false,
      )
    connect()
  }

  override fun onConnecting(endpoint: String) = onMain {
    _uiState.value =
      _uiState.value.copy(
        connection = HostConnectionState.CONNECTING,
        connectionMessage = "Connecting to $endpoint",
      )
  }

  override fun onReady(initializeResult: JSONObject) = onMain {
    reconnectAttempt = 0
    reconnectJob?.cancel()
    reconnectJob = null
    val host = initializeResult.optString("platformOs", "Ubuntu host")
    Log.i(TAG, "Connected to Codex App Server on $host")
    _uiState.value =
      _uiState.value.copy(
        connection = HostConnectionState.CONNECTED,
        connectionMessage = "Connected to $host",
        message = null,
      )
    loadModelCatalog()
    loadUsage()
    refreshThreads()
  }

  private fun loadModelCatalog() {
    appServer?.listModels { response ->
      val error = response.optJSONObject("error")
      if (error != null) {
        Log.w(TAG, "Could not load Codex models: ${error.optString("message", error.toString())}")
        return@listModels
      }

      val data = response.optJSONObject("result")?.optJSONArray("data") ?: JSONArray()
      val models =
        buildList<ModelChoice> {
          for (index in 0 until data.length()) {
            val item = data.optJSONObject(index) ?: continue
            val id = item.stringOrBlank("model").ifBlank { item.stringOrBlank("id") }
            if (id.isBlank()) continue
            val efforts =
              buildList {
                val choices = item.optJSONArray("supportedReasoningEfforts") ?: JSONArray()
                for (choiceIndex in 0 until choices.length()) {
                  val choice = choices.optJSONObject(choiceIndex) ?: continue
                  choice.stringOrBlank("reasoningEffort").takeIf(String::isNotBlank)?.let(::add)
                }
              }
            add(
              ModelChoice(
                id = id,
                title = item.stringOrBlank("displayName").ifBlank { id },
                defaultEffort = item.stringOrBlank("defaultReasoningEffort"),
                reasoningEfforts = efforts,
              ),
            )
          }
        }
      onMain { _uiState.value = _uiState.value.copy(models = models) }
    }
  }

  private fun loadUsage() {
    appServer?.readRateLimits { response ->
      response.optJSONObject("result")?.let(::applyUsageSnapshot)
    }
    appServer?.readAccountUsage { response ->
      val summary = response.optJSONObject("result")?.optJSONObject("summary") ?: return@readAccountUsage
      val details =
        buildList<String> {
          if (summary.has("lifetimeTokens")) add("Lifetime tokens: ${summary.optLong("lifetimeTokens")}")
          if (summary.has("currentStreakDays")) add("Current streak: ${summary.optInt("currentStreakDays")} days")
        }.joinToString("\n")
      if (details.isNotBlank()) {
        onMain {
          _uiState.value =
            _uiState.value.copy(
              usageDetails = listOf(_uiState.value.usageDetails, details).filterNot { it.startsWith("Usage information") }.joinToString("\n"),
            )
        }
      }
    }
  }

  private fun loadSkills(cwd: String) {
    if (cwd.isBlank()) return
    appServer?.listSkills(cwd) { response ->
      val data = response.optJSONObject("result")?.optJSONArray("data") ?: JSONArray()
      val skills =
        buildList<SkillChoice> {
          for (index in 0 until data.length()) {
            val entries = data.optJSONObject(index)?.optJSONArray("skills") ?: continue
            for (skillIndex in 0 until entries.length()) {
              val skill = entries.optJSONObject(skillIndex) ?: continue
              if (!skill.optBoolean("enabled", true)) continue
              val name = skill.stringOrBlank("name")
              if (name.isBlank() || any { it.name == name }) continue
              add(SkillChoice(name, skill.stringOrBlank("description")))
            }
          }
        }.sortedBy { it.name.lowercase() }
      onMain { _uiState.value = _uiState.value.copy(skills = skills) }
    }
  }

  private fun applyUsageSnapshot(result: JSONObject) = onMain {
    val byId = result.optJSONObject("rateLimitsByLimitId")?.optJSONObject("codex")
    val snapshot = byId ?: result.optJSONObject("rateLimits") ?: result
    val parts = mutableListOf<String>()
    val details = mutableListOf<String>()
    listOf("primary", "secondary").forEach { key ->
      val window = snapshot.optJSONObject(key) ?: return@forEach
      val minutes = window.optInt("windowDurationMins")
      val name =
        when {
          minutes == 300 -> "5h"
          minutes == 10080 -> "7d"
          minutes > 0 && minutes % 60 == 0 -> "${minutes / 60}h"
          minutes > 0 -> "${minutes}m"
          else -> "Limit"
        }
      val used = window.optDouble("usedPercent", 0.0).toInt()
      parts += "$name $used%"
      details += "$name usage: $used%"
    }
    if (parts.isNotEmpty()) {
      _uiState.value =
        _uiState.value.copy(
          usageLabel = parts.joinToString(" · "),
          usageDetails = details.joinToString("\n"),
        )
    }
  }

  override fun onDisconnected(reason: String) = onMain {
    Log.w(TAG, "Disconnected from ThreadDeck host: $reason")
    _uiState.value =
      _uiState.value.copy(
        connection = HostConnectionState.DISCONNECTED,
        connectionMessage = reason,
        turnRunning = false,
        activeTurnId = null,
        threadWritable = false,
      )
    threadResumeJob?.cancel()
    scheduleReconnect(reason)
  }

  override fun onError(message: String) = onMain {
    Log.e(TAG, message)
    _uiState.value = _uiState.value.copy(message = message)
  }

  private fun scheduleReconnect(reason: String) {
    if (reconnectJob?.isActive == true) return

    val delayMs = minOf(30_000L, 1_000L shl reconnectAttempt.coerceAtMost(5))
    reconnectAttempt += 1
    _uiState.value =
      _uiState.value.copy(
        connection = HostConnectionState.DISCONNECTED,
        connectionMessage = "$reason · Retrying in ${delayMs / 1_000}s",
      )
    reconnectJob =
      viewModelScope.launch {
        delay(delayMs)
        reconnectJob = null
        connect()
      }
  }

  override fun refreshThreads() {
    if (_uiState.value.connection != HostConnectionState.CONNECTED) return
    val generation = ++refreshGeneration
    _uiState.value = _uiState.value.copy(loadingThreads = true)

    appServer?.loadThreadDeckState { result ->
      onMain {
        if (generation != refreshGeneration) return@onMain
        result
          .onSuccess {
            metadata = parseThreadDeckMetadata(it)
            val selectedThreadId = _uiState.value.selectedThreadId
            preferences.edit().putString("theme", metadata.themeId).apply()
            _uiState.value =
              _uiState.value.copy(
                selectedAccessMode = selectedThreadId?.let(::accessMode) ?: "configured",
                selectedShieldEnabled =
                  selectedThreadId?.let { it in metadata.threadShieldSelections } == true,
                selectedAutoCopyEnabled =
                  selectedThreadId?.let { it in metadata.threadAutoCopySelections } == true,
                selectedPaused =
                  selectedThreadId?.let { it in metadata.pausedThreads } == true,
                selectedRemoteShieldHosts =
                  selectedThreadId?.let { metadata.threadRemoteShieldHosts[it] }.orEmpty(),
                remoteHosts = remoteHostChoices(),
                selectedModelId =
                  selectedThreadId?.let { metadata.threadModelSelections[it] }.orEmpty(),
                selectedReasoningEffort =
                  selectedThreadId?.let { metadata.threadReasoningSelections[it] }.orEmpty(),
                themeId = metadata.themeId,
                navigationVisible = metadata.sidebarVisible,
              )
            Log.i(
              TAG,
              "Loaded ${metadata.projectIds.size} ThreadDeck projects and ${metadata.threadLabels.size} custom thread titles",
            )
          }
          .onFailure {
            Log.w(TAG, "ThreadDeck names are temporarily unavailable: ${it.message}")
          }
        beginThreadRefresh(generation)
      }
    }
  }

  override fun setNavigationVisible(visible: Boolean) {
    preferences.edit().putBoolean("navigation_visible", visible).apply()
    _uiState.value = _uiState.value.copy(navigationVisible = visible)
    persistThreadDeckSettings(JSONObject().put("sidebarVisible", visible))
  }

  private fun beginThreadRefresh(generation: Long) {
    val search = _uiState.value.search.trim()
    val projectCwds =
      metadata.projectIds
        .mapNotNull { metadata.projectPaths[it]?.takeIf(String::isNotBlank) }
        .distinct()
    val cwds = if (search.isNotEmpty() || projectCwds.isEmpty()) listOf("") else projectCwds
    loadThreadBatch(generation, search, cwds, 0, mutableListOf())
  }

  private fun loadThreadBatch(
    generation: Long,
    search: String,
    cwds: List<String>,
    index: Int,
    collected: MutableList<JSONObject>,
  ) {
    if (generation != refreshGeneration) return
    if (index >= cwds.size) {
      finishThreadRefresh(generation, collected)
      return
    }

    appServer?.listThreads(search = search, cwd = cwds[index]) { response ->
      val error = response.optJSONObject("error")
      if (error != null) {
        onMain {
          if (generation == refreshGeneration) {
            _uiState.value =
              _uiState.value.copy(
                loadingThreads = false,
                message = error.optString("message", error.toString()),
              )
          }
        }
        return@listThreads
      }

      val data = response.optJSONObject("result")?.optJSONArray("data") ?: JSONArray()
      for (itemIndex in 0 until data.length()) {
        data.optJSONObject(itemIndex)?.let(collected::add)
      }
      loadThreadBatch(generation, search, cwds, index + 1, collected)
    }
  }

  private fun finishThreadRefresh(generation: Long, data: List<JSONObject>) = onMain {
    if (generation != refreshGeneration) return@onMain
    val seenIds = mutableSetOf<String>()
    val parsed =
      data.mapNotNull { thread ->
        val id = thread.stringOrBlank("id")
        if (id.isBlank() || !seenIds.add(id)) null else parseThreadSummary(thread)
      }
    val projects = buildProjectGroups(parsed, _uiState.value.search.isNotBlank())
    val threads = projects.flatMap(ProjectThreads::threads)
    Log.i(TAG, "Loaded ${threads.size} threads in ${projects.size} ThreadDeck projects")
    val selectedStillExists = threads.any { it.id == _uiState.value.selectedThreadId }
    _uiState.value =
      _uiState.value.copy(
        threads = threads,
        projects = projects,
        loadingThreads = false,
        selectedThreadId = if (selectedStillExists) _uiState.value.selectedThreadId else null,
      )

    val threadToOpen = _uiState.value.selectedThreadId ?: threads.firstOrNull()?.id
    if (threadToOpen != null) selectThread(threadToOpen)
  }

  override fun setSearch(search: String) {
    _uiState.value = _uiState.value.copy(search = search)
  }

  override fun toggleProject(projectId: String) {
    val collapsed = _uiState.value.collapsedProjects.toMutableSet()
    if (!collapsed.add(projectId)) collapsed.remove(projectId)
    saveCollapsedProjects(collapsed)
  }

  override fun setAllProjectsCollapsed(collapsed: Boolean) {
    saveCollapsedProjects(
      if (collapsed) _uiState.value.projects.map(ProjectThreads::id).toSet() else emptySet(),
    )
  }

  private fun saveCollapsedProjects(collapsed: Set<String>) {
    preferences.edit().putStringSet("collapsed_projects", HashSet(collapsed)).apply()
    _uiState.value = _uiState.value.copy(collapsedProjects = collapsed)
  }

  override fun selectThread(threadId: String) {
    val summary = _uiState.value.threads.firstOrNull { it.id == threadId }
    threadResumeJob?.cancel()
    threadResumeJob = null
    preferences.edit().putString("selected_thread", threadId).apply()
    _uiState.value =
      _uiState.value.copy(
        selectedThreadId = threadId,
        selectedThreadTitle = summary?.title ?: "Thread",
        selectedThreadCwd = summary?.cwd.orEmpty(),
        transcript = emptyList(),
        loadingTranscript = true,
        turnRunning = false,
        activeTurnId = null,
        threadWritable = false,
        selectedAccessMode = accessMode(threadId),
        selectedShieldEnabled = threadId in metadata.threadShieldSelections,
        selectedAutoCopyEnabled = threadId in metadata.threadAutoCopySelections,
        selectedPaused = threadId in metadata.pausedThreads,
        selectedRemoteShieldHosts = metadata.threadRemoteShieldHosts[threadId].orEmpty(),
        remoteHosts = remoteHostChoices(),
        selectedModelId = metadata.threadModelSelections[threadId].orEmpty(),
        selectedReasoningEffort = metadata.threadReasoningSelections[threadId].orEmpty(),
      )

    resumeSelectedThread(threadId, summary, loadReadOnlyOnConflict = true)
  }

  private fun resumeSelectedThread(
    threadId: String,
    summary: ThreadSummary?,
    loadReadOnlyOnConflict: Boolean,
  ) {
    appServer?.resumeThread(threadId) { response ->
      onMain {
        if (_uiState.value.selectedThreadId != threadId) return@onMain
        val error = response.optJSONObject("error")
        if (error != null) {
          val errorMessage = error.optString("message", error.toString())
          if (errorMessage.contains("active writer", ignoreCase = true)) {
            _uiState.value =
              _uiState.value.copy(
                loadingTranscript = loadReadOnlyOnConflict && _uiState.value.transcript.isEmpty(),
                threadWritable = false,
                message = "This thread is open on the desktop. Showing it read-only while ThreadDeck waits to take over.",
              )
            if (loadReadOnlyOnConflict || _uiState.value.transcript.isEmpty()) {
              loadThreadReadOnly(threadId, summary)
            }
            scheduleThreadResume(threadId, summary)
            return@onMain
          }

          _uiState.value =
            _uiState.value.copy(
              loadingTranscript = false,
              threadWritable = false,
              message = errorMessage,
            )
          return@onMain
        }

        val result = response.optJSONObject("result") ?: JSONObject()
        val thread = result.optJSONObject("thread") ?: JSONObject()
        val entries = parseTranscript(thread)
        val status = thread.optJSONObject("status")
        val isActive = status?.optString("type") == "active"
        val effectiveModel =
          metadata.threadModelSelections[threadId]
            ?: result.stringOrBlank("model")
        val effectiveEffort =
          metadata.threadReasoningSelections[threadId]
            ?: result.stringOrBlank("reasoningEffort")
        val resumedCwd = result.optString("cwd", summary?.cwd.orEmpty())
        loadSkills(resumedCwd)
        _uiState.value =
          _uiState.value.copy(
            selectedThreadTitle = summary?.title ?: threadDisplayTitle(thread),
            selectedThreadCwd = resumedCwd,
            transcript = entries,
            loadingTranscript = false,
            turnRunning = isActive,
            threadWritable = true,
            selectedModelId = effectiveModel,
            selectedReasoningEffort = effectiveEffort,
            message = null,
          )
      }
    }
  }

  private fun loadThreadReadOnly(threadId: String, summary: ThreadSummary?) {
    appServer?.readThread(threadId) { response ->
      onMain {
        if (_uiState.value.selectedThreadId != threadId) return@onMain
        val error = response.optJSONObject("error")
        if (error != null) {
          _uiState.value =
            _uiState.value.copy(
              loadingTranscript = false,
              message = error.optString("message", error.toString()),
            )
          return@onMain
        }

        val result = response.optJSONObject("result") ?: JSONObject()
        val thread = result.optJSONObject("thread") ?: JSONObject()
        _uiState.value =
          _uiState.value.copy(
            selectedThreadTitle = summary?.title ?: threadDisplayTitle(thread),
            selectedThreadCwd = thread.optString("cwd", summary?.cwd.orEmpty()),
            transcript = parseTranscript(thread),
            loadingTranscript = false,
            threadWritable = false,
          )
      }
    }
  }

  private fun scheduleThreadResume(threadId: String, summary: ThreadSummary?) {
    threadResumeJob?.cancel()
    threadResumeJob =
      viewModelScope.launch {
        delay(2_000)
        threadResumeJob = null
        val state = _uiState.value
        if (state.connection == HostConnectionState.CONNECTED && state.selectedThreadId == threadId) {
          resumeSelectedThread(threadId, summary, loadReadOnlyOnConflict = false)
        }
      }
  }

  override fun setDraft(draft: String) {
    _uiState.value = _uiState.value.copy(draft = draft)
  }

  override fun addAttachments(uriStrings: List<String>, kind: String) {
    uriStrings.forEach { uriString ->
      val id = UUID.randomUUID().toString()
      val uri = Uri.parse(uriString)
      val name = attachmentName(uri, kind)
      val attachment = TabletAttachment(id = id, name = name, kind = kind)
      _uiState.value = _uiState.value.copy(attachments = _uiState.value.attachments + attachment)

      Thread(
        {
          runCatching {
            val bytes =
              getApplication<Application>().contentResolver.openInputStream(uri)?.use { it.readBytes() }
                ?: error("The selected file could not be opened")
            require(bytes.size <= 64 * 1024 * 1024) { "Attachments are limited to 64 MB" }
            val extension = name.substringAfterLast('.', "bin").take(10)
            appServer?.uploadAttachment(bytes, kind, extension) { result ->
              onMain {
                result
                  .onSuccess { response ->
                    val hostPath = response.optString("path")
                    if (hostPath.isBlank()) {
                      failAttachment(id, "The host did not return an attachment path")
                    } else {
                      _uiState.value =
                        _uiState.value.copy(
                          attachments =
                            _uiState.value.attachments.map {
                              if (it.id == id) it.copy(hostPath = hostPath, uploading = false) else it
                            },
                        )
                    }
                  }
                  .onFailure { failAttachment(id, it.message ?: "Upload failed") }
              }
            } ?: onMain { failAttachment(id, "ThreadDeck is not connected") }
          }.onFailure { error -> onMain { failAttachment(id, error.message ?: "Upload failed") } }
        },
        "threaddeck-read-attachment",
      ).start()
    }
  }

  private fun attachmentName(uri: Uri, kind: String): String {
    val resolver = getApplication<Application>().contentResolver
    val displayName =
      runCatching {
        resolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)?.use { cursor ->
          if (cursor.moveToFirst()) cursor.getString(0) else null
        }
      }.getOrNull()
    return displayName?.takeIf(String::isNotBlank)
      ?: if (kind == "audio") "audio.bin" else "image.bin"
  }

  private fun failAttachment(id: String, message: String) {
    _uiState.value =
      _uiState.value.copy(
        attachments = _uiState.value.attachments.filterNot { it.id == id },
        message = "Attachment failed: $message",
      )
  }

  override fun removeAttachment(id: String) {
    _uiState.value =
      _uiState.value.copy(attachments = _uiState.value.attachments.filterNot { it.id == id })
  }

  override fun clearAttachments() {
    _uiState.value = _uiState.value.copy(attachments = emptyList())
  }

  override fun requestAccessMode(accessMode: String) {
    if (accessMode == "yolo" && _uiState.value.selectedAccessMode != "yolo") {
      _uiState.value = _uiState.value.copy(showYoloConfirmation = true)
      return
    }
    applyAccessMode(accessMode)
  }

  override fun confirmYolo(enable: Boolean) {
    _uiState.value = _uiState.value.copy(showYoloConfirmation = false)
    if (enable) applyAccessMode("yolo")
  }

  private fun applyAccessMode(accessMode: String) {
    val threadId = _uiState.value.selectedThreadId ?: return
    val normalized = if (accessMode == "yolo") "yolo" else "configured"
    val selections = metadata.threadAccessSelections.toMutableMap()
    selections[threadId] = normalized
    metadata = metadata.copy(threadAccessSelections = selections)
    _uiState.value =
      _uiState.value.copy(
        selectedAccessMode = normalized,
        message =
          if (normalized == "yolo") "YOLO full access will apply to the next message."
          else "The thread's configured access policy will apply to the next message.",
      )
    persistThreadDeckSettings(
      JSONObject().put("threadAccessSelections", JSONObject(selections as Map<*, *>)),
    )
  }

  override fun setShieldEnabled(enabled: Boolean) {
    val threadId = _uiState.value.selectedThreadId ?: return
    val selections = metadata.threadShieldSelections.toMutableSet()
    if (enabled) selections.add(threadId) else selections.remove(threadId)
    metadata = metadata.copy(threadShieldSelections = selections)
    _uiState.value =
      _uiState.value.copy(
        selectedShieldEnabled = enabled,
        message = if (enabled) "Shield sudo is enabled for this thread." else "Shield sudo is disabled for this thread.",
      )
    persistThreadDeckSettings(
      JSONObject().put("threadShieldSelections", JSONArray(selections.toList())),
    )
  }

  override fun setAutoCopyEnabled(enabled: Boolean) {
    val threadId = _uiState.value.selectedThreadId ?: return
    val selections = metadata.threadAutoCopySelections.toMutableSet()
    if (enabled) selections.add(threadId) else selections.remove(threadId)
    metadata = metadata.copy(threadAutoCopySelections = selections)
    _uiState.value =
      _uiState.value.copy(
        selectedAutoCopyEnabled = enabled,
        message = if (enabled) "Command auto-copy is enabled." else "Command auto-copy is disabled.",
      )
    persistThreadDeckSettings(
      JSONObject().put("threadAutoCopySelections", JSONArray(selections.toList())),
    )
  }

  override fun togglePause() {
    val state = _uiState.value
    val threadId = state.selectedThreadId ?: return
    if (state.selectedPaused) {
      val paused = metadata.pausedThreads.toMutableSet().apply { remove(threadId) }
      metadata = metadata.copy(pausedThreads = paused)
      _uiState.value = state.copy(selectedPaused = false)
      persistThreadDeckSettings(JSONObject().put("pausedThreads", JSONArray(paused.toList())))
      startInstructionTurn(threadId, "Continue from the last safe checkpoint and resume the unfinished task.")
      return
    }

    val paused = metadata.pausedThreads.toMutableSet().apply { add(threadId) }
    metadata = metadata.copy(pausedThreads = paused)
    _uiState.value =
      state.copy(
        selectedPaused = true,
        message =
          if (state.turnRunning) "Codex is preparing a safe checkpoint."
          else "Thread paused at its current checkpoint.",
      )
    persistThreadDeckSettings(JSONObject().put("pausedThreads", JSONArray(paused.toList())))
    if (state.turnRunning && state.activeTurnId != null) {
      appServer?.steerTurn(
        threadId,
        state.activeTurnId,
        "Find a safe place to pause. Finish only the operation currently in progress so the workspace is left coherent. Record a concise checkpoint of completed work and the exact next step, then end this turn. Do not begin additional work.",
        turnSettings(threadId),
      ) { response ->
        response.optJSONObject("error")?.let { error ->
          onError("The safe-pause request was not accepted: ${error.optString("message", error.toString())}")
        }
      }
    }
  }

  override fun continueThread() {
    val state = _uiState.value
    val threadId = state.selectedThreadId ?: return
    if (state.selectedPaused) {
      togglePause()
      return
    }
    if (state.turnRunning && state.activeTurnId != null) {
      appServer?.steerTurn(threadId, state.activeTurnId, "continue", turnSettings(threadId)) { response ->
        response.optJSONObject("error")?.let { error ->
          onError("Continue was not accepted: ${error.optString("message", error.toString())}")
        }
      }
    } else {
      startInstructionTurn(threadId, "continue")
    }
  }

  private fun startInstructionTurn(threadId: String, instruction: String) {
    _uiState.value = _uiState.value.copy(turnRunning = true)
    appServer?.startTurn(threadId, instruction, turnSettings(threadId)) { response ->
      val error = response.optJSONObject("error")
      if (error != null) {
        onMain {
          _uiState.value =
            _uiState.value.copy(
              turnRunning = false,
              message = error.optString("message", error.toString()),
            )
        }
      }
    }
  }

  override fun selectModel(modelId: String) {
    val threadId = _uiState.value.selectedThreadId ?: return
    val model = _uiState.value.models.firstOrNull { it.id == modelId } ?: return
    val modelSelections = metadata.threadModelSelections.toMutableMap()
    val effortSelections = metadata.threadReasoningSelections.toMutableMap()
    modelSelections[threadId] = modelId
    val effort =
      model.defaultEffort.ifBlank { model.reasoningEfforts.firstOrNull().orEmpty() }
    if (effort.isNotBlank()) effortSelections[threadId] = effort
    metadata =
      metadata.copy(
        threadModelSelections = modelSelections,
        threadReasoningSelections = effortSelections,
      )
    _uiState.value =
      _uiState.value.copy(
        selectedModelId = modelId,
        selectedReasoningEffort = effort,
        showModelPicker = false,
        message = "Model change will apply to the next message.",
      )
    persistThreadDeckSettings(
      JSONObject()
        .put("threadModelSelections", JSONObject(modelSelections as Map<*, *>))
        .put("threadReasoningSelections", JSONObject(effortSelections as Map<*, *>)),
    )
  }

  override fun selectReasoningEffort(effort: String) {
    val threadId = _uiState.value.selectedThreadId ?: return
    if (effort.isBlank()) return
    val selections = metadata.threadReasoningSelections.toMutableMap()
    selections[threadId] = effort
    metadata = metadata.copy(threadReasoningSelections = selections)
    _uiState.value =
      _uiState.value.copy(
        selectedReasoningEffort = effort,
        showReasoningPicker = false,
        message = "Reasoning change will apply to the next message.",
      )
    persistThreadDeckSettings(
      JSONObject().put("threadReasoningSelections", JSONObject(selections as Map<*, *>)),
    )
  }

  override fun showModelPicker(show: Boolean) {
    _uiState.value = _uiState.value.copy(showModelPicker = show)
  }

  override fun showReasoningPicker(show: Boolean) {
    _uiState.value = _uiState.value.copy(showReasoningPicker = show)
  }

  override fun showDetailsDialog(show: Boolean) {
    _uiState.value = _uiState.value.copy(showDetailsDialog = show)
  }

  override fun showSkillPicker(show: Boolean) {
    _uiState.value = _uiState.value.copy(showSkillPicker = show)
  }

  override fun selectSkill(name: String) {
    val draft = _uiState.value.draft
    val insertion = "\$$name"
    _uiState.value =
      _uiState.value.copy(
        draft = if (draft.isBlank()) "$insertion " else "$draft $insertion ",
        showSkillPicker = false,
      )
  }

  override fun showRemoteShieldDialog(show: Boolean) {
    _uiState.value = _uiState.value.copy(showRemoteShieldDialog = show)
  }

  override fun setRemoteShieldHost(host: String, enabled: Boolean) {
    val threadId = _uiState.value.selectedThreadId ?: return
    if (host !in metadata.remoteHostCredentialSaved) {
      _uiState.value = _uiState.value.copy(message = "Save this host's sudo credential on desktop ThreadDeck first.")
      return
    }
    val allSelections = metadata.threadRemoteShieldHosts.toMutableMap()
    val selected = allSelections[threadId].orEmpty().toMutableSet()
    if (enabled) selected.add(host) else selected.remove(host)
    if (selected.isEmpty()) allSelections.remove(threadId) else allSelections[threadId] = selected
    metadata = metadata.copy(threadRemoteShieldHosts = allSelections)
    _uiState.value = _uiState.value.copy(selectedRemoteShieldHosts = selected)
    val json = JSONObject()
    allSelections.forEach { (id, hosts) -> json.put(id, JSONArray(hosts.toList())) }
    persistThreadDeckSettings(JSONObject().put("threadRemoteShieldHosts", json))
  }

  private fun persistThreadDeckSettings(updates: JSONObject) {
    appServer?.updateThreadDeckState(updates) { result ->
      result.onFailure { error ->
        onMain {
          _uiState.value =
            _uiState.value.copy(
              message = "The tablet setting could not be synchronized: ${error.message}",
            )
        }
      }
    }
  }

  override fun send() {
    val state = _uiState.value
    if (!state.threadWritable) return
    val threadId = state.selectedThreadId ?: return
    val text = state.draft.trim()
    val readyAttachments = state.attachments.filter { !it.uploading && it.hostPath.isNotBlank() }
    if (text.isEmpty() && readyAttachments.isEmpty()) return
    if (state.attachments.any(TabletAttachment::uploading)) return
    val settings = turnSettings(threadId)
    val turnAttachments =
      readyAttachments.map {
        CodexAppServer.TurnAttachment(path = it.hostPath, kind = it.kind)
      }
    val localText =
      buildString {
        append(text)
        readyAttachments.forEach {
          if (isNotEmpty()) append('\n')
          append("Attached ").append(it.kind).append(": ").append(it.name)
        }
      }

    _uiState.value =
      state.copy(
        draft = "",
        attachments = emptyList(),
        transcript =
          state.transcript +
            TranscriptEntry(
              id = "local-user-${System.nanoTime()}",
              kind = TranscriptKind.USER,
              heading = "You",
              text = localText,
            ),
      )

    if (state.turnRunning && state.activeTurnId != null) {
      appServer?.steerTurn(threadId, state.activeTurnId, text, settings, turnAttachments) { response ->
        response.optJSONObject("error")?.let { error ->
          onError("Follow-up was not accepted: ${error.optString("message", error.toString())}")
        }
      }
      return
    }

    _uiState.value = _uiState.value.copy(turnRunning = true)
    appServer?.startTurn(threadId, text, settings, turnAttachments) { response ->
      val error = response.optJSONObject("error")
      if (error != null) {
        onMain {
          _uiState.value =
            _uiState.value.copy(
              turnRunning = false,
              activeTurnId = null,
              message = error.optString("message", error.toString()),
            )
        }
        return@startTurn
      }

      val turnId = response.optJSONObject("result")?.optJSONObject("turn")?.optString("id")
      if (!turnId.isNullOrBlank()) {
        onMain { _uiState.value = _uiState.value.copy(activeTurnId = turnId) }
      }
    }
  }

  override fun stopTurn() {
    val state = _uiState.value
    val threadId = state.selectedThreadId ?: return
    val turnId = state.activeTurnId ?: return
    appServer?.interruptTurn(threadId, turnId)
  }

  override fun addProject(cwd: String) {
    val path = cwd.trim().trimEnd('/')
    if (path.isBlank()) return
    val projectId = "project-${UUID.randomUUID()}"
    val projectIds = metadata.projectIds + projectId
    val projectPaths = metadata.projectPaths.toMutableMap().apply { put(projectId, path) }
    metadata = metadata.copy(projectIds = projectIds, projectPaths = projectPaths)
    _uiState.value = _uiState.value.copy(showNewProjectDialog = false)
    persistThreadDeckSettings(
      JSONObject()
        .put("projectFolders", JSONArray(projectIds))
        .put("projectPaths", JSONObject(projectPaths as Map<*, *>))
        .put("selectedProjectId", projectId)
        .put("selectedFolder", path),
    )
    refreshThreads()
  }

  override fun renameProject(projectId: String, label: String) {
    val labels = metadata.projectLabels.toMutableMap()
    val trimmed = label.trim()
    if (trimmed.isBlank()) labels.remove(projectId) else labels[projectId] = trimmed
    metadata = metadata.copy(projectLabels = labels)
    persistThreadDeckSettings(
      JSONObject().put("folderLabels", JSONObject(labels as Map<*, *>)),
    )
    rebuildVisibleProjects()
  }

  override fun deleteProject(projectId: String) {
    val project = _uiState.value.projects.firstOrNull { it.id == projectId } ?: return
    if (project.threads.isNotEmpty()) {
      _uiState.value = _uiState.value.copy(message = "Move or delete every thread before deleting this project.")
      return
    }
    val projectIds = metadata.projectIds.filterNot { it == projectId }
    val projectPaths = metadata.projectPaths.toMutableMap().apply { remove(projectId) }
    val labels = metadata.projectLabels.toMutableMap().apply { remove(projectId) }
    val sorts = metadata.projectThreadSorts.toMutableMap().apply { remove(projectId) }
    metadata =
      metadata.copy(
        projectIds = projectIds,
        projectPaths = projectPaths,
        projectLabels = labels,
        projectThreadSorts = sorts,
      )
    persistThreadDeckSettings(
      JSONObject()
        .put("projectFolders", JSONArray(projectIds))
        .put("projectPaths", JSONObject(projectPaths as Map<*, *>))
        .put("folderLabels", JSONObject(labels as Map<*, *>))
        .put("projectThreadSorts", JSONObject(sorts as Map<*, *>)),
    )
    rebuildVisibleProjects()
  }

  override fun setProjectSort(sortId: String) {
    metadata = metadata.copy(projectSort = sortId)
    persistThreadDeckSettings(JSONObject().put("projectSort", sortId))
    rebuildVisibleProjects()
  }

  override fun setThreadSort(projectId: String, sortId: String) {
    val sorts = metadata.projectThreadSorts.toMutableMap().apply { put(projectId, sortId) }
    metadata = metadata.copy(projectThreadSorts = sorts)
    persistThreadDeckSettings(
      JSONObject().put("projectThreadSorts", JSONObject(sorts as Map<*, *>)),
    )
    rebuildVisibleProjects()
  }

  override fun renameThread(threadId: String, label: String) {
    val labels = metadata.threadLabels.toMutableMap()
    val trimmed = label.trim()
    if (trimmed.isBlank()) labels.remove(threadId) else labels[threadId] = trimmed
    metadata = metadata.copy(threadLabels = labels)
    persistThreadDeckSettings(
      JSONObject().put("threadLabels", JSONObject(labels as Map<*, *>)),
    )
    val title =
      trimmed.ifBlank { _uiState.value.threads.firstOrNull { it.id == threadId }?.title.orEmpty() }
    _uiState.value =
      _uiState.value.copy(
        selectedThreadTitle =
          if (_uiState.value.selectedThreadId == threadId && title.isNotBlank()) title
          else _uiState.value.selectedThreadTitle,
      )
    rebuildVisibleProjects()
  }

  override fun summarizeAndTitle(threadId: String) {
    val instruction =
      "Summarize this thread's purpose, important work, decisions, and current outcome in one concise paragraph. Do not use tools or modify anything. After the summary, write one final separate line exactly as THREADDECK_TITLE: Short title. The title must be descriptive, contain no quotes, and be at most 48 characters including spaces. Write nothing after the title line."
    appServer?.startTurn(threadId, instruction, turnSettings(threadId)) { response ->
      response.optJSONObject("error")?.let { error ->
        onError("Could not summarize this thread: ${error.optString("message", error.toString())}")
      }
    }
    _uiState.value = _uiState.value.copy(message = "Summarizing and titling the thread…")
  }

  override fun moveThread(threadId: String, projectId: String) {
    val cwd = metadata.projectPaths[projectId] ?: return
    appServer?.updateThreadCwd(threadId, cwd) { response ->
      val error = response.optJSONObject("error")
      if (error != null) {
        onError("Could not move the thread: ${error.optString("message", error.toString())}")
        return@updateThreadCwd
      }
      onMain {
        val assignments = metadata.threadProjectAssignments.toMutableMap().apply { put(threadId, projectId) }
        metadata = metadata.copy(threadProjectAssignments = assignments)
        persistThreadDeckSettings(
          JSONObject().put("threadProjectAssignments", JSONObject(assignments as Map<*, *>)),
        )
        refreshThreads()
      }
    }
  }

  override fun deleteThread(threadId: String) {
    appServer?.deleteThread(threadId) { response ->
      val error = response.optJSONObject("error")
      if (error != null) {
        onError("Could not delete the thread: ${error.optString("message", error.toString())}")
        return@deleteThread
      }
      onMain {
        removeThreadMetadata(threadId)
        if (_uiState.value.selectedThreadId == threadId) {
          preferences.edit().remove("selected_thread").apply()
          _uiState.value =
            _uiState.value.copy(
              selectedThreadId = null,
              selectedThreadTitle = "Select a thread",
              selectedThreadCwd = "",
              transcript = emptyList(),
              threadWritable = false,
            )
        }
        refreshThreads()
      }
    }
  }

  private fun removeThreadMetadata(threadId: String) {
    val labels = metadata.threadLabels.toMutableMap().apply { remove(threadId) }
    val assignments = metadata.threadProjectAssignments.toMutableMap().apply { remove(threadId) }
    val access = metadata.threadAccessSelections.toMutableMap().apply { remove(threadId) }
    val shield = metadata.threadShieldSelections.toMutableSet().apply { remove(threadId) }
    val autoCopy = metadata.threadAutoCopySelections.toMutableSet().apply { remove(threadId) }
    val paused = metadata.pausedThreads.toMutableSet().apply { remove(threadId) }
    val remoteShield = metadata.threadRemoteShieldHosts.toMutableMap().apply { remove(threadId) }
    val models = metadata.threadModelSelections.toMutableMap().apply { remove(threadId) }
    val efforts = metadata.threadReasoningSelections.toMutableMap().apply { remove(threadId) }
    val approvals = metadata.threadConfiguredApprovalPolicies.toMutableMap().apply { remove(threadId) }
    val sandboxes = metadata.threadConfiguredSandboxPolicies.toMutableMap().apply { remove(threadId) }
    metadata =
      metadata.copy(
        threadLabels = labels,
        threadProjectAssignments = assignments,
        threadAccessSelections = access,
        threadShieldSelections = shield,
        threadAutoCopySelections = autoCopy,
        pausedThreads = paused,
        threadRemoteShieldHosts = remoteShield,
        threadModelSelections = models,
        threadReasoningSelections = efforts,
        threadConfiguredApprovalPolicies = approvals,
        threadConfiguredSandboxPolicies = sandboxes,
      )
    persistThreadDeckSettings(
      JSONObject()
        .put("threadLabels", JSONObject(labels as Map<*, *>))
        .put("threadProjectAssignments", JSONObject(assignments as Map<*, *>))
        .put("threadAccessSelections", JSONObject(access as Map<*, *>))
        .put("threadShieldSelections", JSONArray(shield.toList()))
        .put("threadAutoCopySelections", JSONArray(autoCopy.toList()))
        .put("pausedThreads", JSONArray(paused.toList()))
        .put(
          "threadRemoteShieldHosts",
          JSONObject().also { json ->
            remoteShield.forEach { (id, hosts) -> json.put(id, JSONArray(hosts.toList())) }
          },
        )
        .put("threadModelSelections", JSONObject(models as Map<*, *>))
        .put("threadReasoningSelections", JSONObject(efforts as Map<*, *>))
        .put("threadConfiguredApprovalPolicies", JSONObject(approvals as Map<*, *>))
        .put("threadConfiguredSandboxPolicies", JSONObject(sandboxes as Map<*, *>)),
    )
  }

  private fun rebuildVisibleProjects() {
    val threads = _uiState.value.threads.map { thread ->
      thread.copy(
        title = metadata.threadLabels[thread.id]?.takeIf(String::isNotBlank) ?: thread.title,
        projectId = metadata.threadProjectAssignments[thread.id] ?: thread.projectId,
      )
    }
    _uiState.value =
      _uiState.value.copy(
        threads = threads,
        projects = buildProjectGroups(threads, _uiState.value.search.isNotBlank()),
      )
  }

  override fun createThread(cwd: String) {
    val path = cwd.trim()
    if (path.isEmpty()) return
    _uiState.value = _uiState.value.copy(showNewThreadDialog = false, loadingTranscript = true)

    appServer?.startThread(path) { response ->
      val error = response.optJSONObject("error")
      if (error != null) {
        onMain {
          _uiState.value =
            _uiState.value.copy(
              loadingTranscript = false,
              message = error.optString("message", error.toString()),
            )
        }
        return@startThread
      }

      val thread = response.optJSONObject("result")?.optJSONObject("thread") ?: JSONObject()
      val threadId = thread.optString("id")
      if (threadId.isBlank()) return@startThread
      onMain {
        var projectIds = metadata.projectIds
        val projectPaths = metadata.projectPaths.toMutableMap()
        val projectId =
          metadata.projectIds.firstOrNull { metadata.projectPaths[it] == path }
            ?: "project-${UUID.randomUUID()}".also {
              projectIds = projectIds + it
              projectPaths[it] = path
            }
        val assignments = metadata.threadProjectAssignments.toMutableMap().apply { put(threadId, projectId) }
        metadata =
          metadata.copy(
            projectIds = projectIds,
            projectPaths = projectPaths,
            threadProjectAssignments = assignments,
          )
        persistThreadDeckSettings(
          JSONObject()
            .put("projectFolders", JSONArray(projectIds))
            .put("projectPaths", JSONObject(projectPaths as Map<*, *>))
            .put("threadProjectAssignments", JSONObject(assignments as Map<*, *>))
            .put("selectedProjectId", projectId)
            .put("selectedFolder", path),
        )
        preferences.edit().putString("selected_thread", threadId).apply()
        _uiState.value =
          _uiState.value.copy(
            selectedThreadId = threadId,
            selectedThreadTitle = "New Thread",
            selectedThreadCwd = path,
            transcript = emptyList(),
            loadingTranscript = false,
            threadWritable = true,
          )
      }
    }
  }

  override fun onNotification(method: String, params: JSONObject) = onMain {
    val state = _uiState.value
    val threadId = params.optString("threadId")
    if (threadId.isNotEmpty() && threadId != state.selectedThreadId) return@onMain

    when (method) {
      "turn/started" -> {
        val turnId = params.optJSONObject("turn")?.optString("id")
        _uiState.value = state.copy(turnRunning = true, activeTurnId = turnId)
      }
      "item/agentMessage/delta" ->
        appendLiveText(
          id = params.optString("itemId", "live-agent"),
          kind = TranscriptKind.AGENT,
          heading = "Codex",
          delta = params.optString("delta"),
        )
      "item/reasoning/summaryTextDelta" ->
        appendLiveText(
          id = params.optString("itemId", "live-reasoning"),
          kind = TranscriptKind.REASONING,
          heading = "Codex reasoning",
          delta = params.optString("delta"),
        )
      "item/plan/delta" ->
        appendLiveText(
          id = params.optString("itemId", "live-plan"),
          kind = TranscriptKind.PLAN,
          heading = "Codex plan",
          delta = params.optString("delta"),
        )
      "item/commandExecution/outputDelta" ->
        appendLiveText(
          id = params.optString("itemId", "live-command"),
          kind = TranscriptKind.ACTIVITY,
          heading = "Codex running command",
          delta = params.optString("delta"),
        )
      "item/started", "item/completed" -> handleItem(method, params.optJSONObject("item") ?: return@onMain)
      "turn/completed" -> {
        _uiState.value =
          _uiState.value.copy(
            turnRunning = false,
            activeTurnId = null,
          )
        refreshThreads()
      }
      "thread/name/updated" -> {
        val name = params.stringOrBlank("name")
        if (name.isNotBlank()) _uiState.value = _uiState.value.copy(selectedThreadTitle = name)
        refreshThreads()
      }
      "thread/tokenUsage/updated" -> applyTokenUsage(params.optJSONObject("tokenUsage") ?: JSONObject())
      "account/rateLimits/updated" -> {
        val limits = params.optJSONObject("rateLimits") ?: JSONObject()
        applyUsageSnapshot(JSONObject().put("rateLimits", limits))
      }
    }
  }

  private fun applyTokenUsage(tokenUsage: JSONObject) {
    val lastTokens = tokenUsage.optJSONObject("last")?.optDouble("totalTokens", 0.0) ?: 0.0
    val contextWindow = tokenUsage.optDouble("modelContextWindow", 0.0)
    if (contextWindow <= 0.0) return
    val percentLeft = ((1.0 - lastTokens / contextWindow) * 100.0).toInt().coerceIn(0, 100)
    val rateUsage = _uiState.value.usageLabel.takeUnless { it == "Usage —" }.orEmpty()
    _uiState.value =
      _uiState.value.copy(
        usageLabel = listOf("$percentLeft% context left", rateUsage).filter(String::isNotBlank).joinToString(" · "),
        usageDetails = "Latest turn: ${lastTokens.toLong()} / ${contextWindow.toLong()} context tokens\n${_uiState.value.usageDetails}",
      )
  }

  override fun onServerRequest(request: CodexAppServer.ServerRequest) = onMain {
    if (
      request.method != "item/commandExecution/requestApproval" &&
      request.method != "item/fileChange/requestApproval" &&
      request.method != "item/permissions/requestApproval"
    ) {
      appServer?.respondToApproval(request, "decline")
      return@onMain
    }

    val title =
      when (request.method) {
        "item/commandExecution/requestApproval" -> "Approve command?"
        "item/fileChange/requestApproval" -> "Approve file changes?"
        else -> "Approve permissions?"
      }
    val details =
      request.params.optString("reason").ifBlank {
        request.params.optString("command").ifBlank { request.params.toString(2) }
      }
    _uiState.value =
      _uiState.value.copy(
        approval = ApprovalPrompt(request, title, details),
      )
  }

  override fun answerApproval(decision: String) {
    val approval = _uiState.value.approval ?: return
    appServer?.respondToApproval(approval.request, decision)
    _uiState.value = _uiState.value.copy(approval = null)
  }

  override fun setTheme(themeId: String) {
    preferences.edit().putString("theme", themeId).apply()
    _uiState.value = _uiState.value.copy(themeId = themeId, showThemePicker = false)
    persistThreadDeckSettings(JSONObject().put("theme", themeId))
  }

  override fun showThemePicker(show: Boolean) {
    _uiState.value = _uiState.value.copy(showThemePicker = show)
  }

  override fun showConnectionDialog(show: Boolean) {
    _uiState.value = _uiState.value.copy(showConnectionDialog = show)
  }

  override fun showNewThreadDialog(show: Boolean) {
    _uiState.value = _uiState.value.copy(showNewThreadDialog = show)
  }

  override fun showNewProjectDialog(show: Boolean) {
    _uiState.value = _uiState.value.copy(showNewProjectDialog = show)
  }

  override fun clearMessage() {
    _uiState.value = _uiState.value.copy(message = null)
  }

  private fun appendLiveText(
    id: String,
    kind: TranscriptKind,
    heading: String,
    delta: String,
  ) {
    if (delta.isEmpty()) return
    val entries = _uiState.value.transcript.toMutableList()
    val index = entries.indexOfLast { it.id == id }
    if (index >= 0) {
      entries[index] = entries[index].copy(text = entries[index].text + delta, running = true)
    } else {
      entries += TranscriptEntry(id, kind, heading, delta, running = true)
    }
    _uiState.value = _uiState.value.copy(transcript = entries)
  }

  private fun handleItem(method: String, item: JSONObject) {
    val type = item.optString("type")
    if (type == "userMessage") return
    val id = item.optString("id", "item-${System.nanoTime()}")
    val complete = method == "item/completed"
    val entry = parseItem(item, complete) ?: return
    val entries = _uiState.value.transcript.toMutableList()
    val index = entries.indexOfLast { it.id == id }
    if (index >= 0) entries[index] = entry.copy(id = id) else entries += entry.copy(id = id)
    _uiState.value = _uiState.value.copy(transcript = entries)
    if (complete && type == "agentMessage" && _uiState.value.selectedAutoCopyEnabled) {
      copyLastShellBlock(item.optString("text"))
    }
  }

  private fun copyLastShellBlock(text: String) {
    val command =
      Regex("```(?:bash|sh|shell)\\s*\\n([\\s\\S]*?)```", RegexOption.IGNORE_CASE)
        .findAll(text)
        .lastOrNull()
        ?.groupValues
        ?.getOrNull(1)
        ?.trim()
        .orEmpty()
    if (command.isBlank()) return
    val clipboard = getApplication<Application>().getSystemService(Context.CLIPBOARD_SERVICE) as ClipboardManager
    clipboard.setPrimaryClip(ClipData.newPlainText("ThreadDeck command", command))
  }

  private fun parseThreadSummary(thread: JSONObject): ThreadSummary {
    val cwd = thread.stringOrBlank("cwd")
    val id = thread.stringOrBlank("id")
    val projectId =
      metadata.threadProjectAssignments[id]
        ?: metadata.projectIds.firstOrNull { metadata.projectPaths[it] == cwd }
        ?: cwd
    val title = metadata.threadLabels[id]?.takeIf(String::isNotBlank) ?: threadDisplayTitle(thread)
    val updatedSeconds = thread.optLong("updatedAt", thread.optLong("createdAt", 0L))
    val updated =
      if (updatedSeconds > 0) {
        DateFormat.getDateTimeInstance(DateFormat.SHORT, DateFormat.SHORT).format(Date(updatedSeconds * 1000L))
      } else {
        ""
      }
    return ThreadSummary(
      id = id,
      title = title.take(100),
      cwd = cwd,
      projectId = projectId,
      updatedLabel = updated,
      updatedAt = updatedSeconds,
      pinned = thread.optBoolean("isPinned"),
    )
  }

  private fun threadDisplayTitle(thread: JSONObject): String {
    val name = singleLinePreview(thread.stringOrBlank("name"))
    if (name.isNotBlank()) return name
    val preview = singleLinePreview(thread.stringOrBlank("preview"))
    if (preview.isNotBlank()) return preview
    return "Thread ${thread.stringOrBlank("id").take(8)}".trim()
  }

  private fun singleLinePreview(value: String): String {
    val normalized = value.replace('\n', ' ').replace('\r', ' ').trim()
    return if (normalized.length > 58) normalized.take(58) + "..." else normalized
  }

  private fun parseThreadDeckMetadata(state: JSONObject): ThreadDeckMetadata {
    val projectIds = state.optJSONArray("projectFolders").toStringList()
    val projectPaths = state.stringMap("projectPaths").toMutableMap()
    projectIds.forEach { projectPaths.putIfAbsent(it, it) }
    return ThreadDeckMetadata(
      projectIds = projectIds,
      projectPaths = projectPaths,
      projectLabels = state.stringMap("folderLabels"),
      threadLabels = state.stringMap("threadLabels"),
      threadProjectAssignments = state.stringMap("threadProjectAssignments"),
      projectThreadSorts = state.stringMap("projectThreadSorts"),
      projectSort = state.stringOrBlank("projectSort").ifBlank { "updated-desc" },
      threadAccessSelections = state.stringMap("threadAccessSelections"),
      threadShieldSelections = state.optJSONArray("threadShieldSelections").toStringList().toSet(),
      threadAutoCopySelections = state.optJSONArray("threadAutoCopySelections").toStringList().toSet(),
      pausedThreads = state.optJSONArray("pausedThreads").toStringList().toSet(),
      remoteHostLabels = state.stringMap("remoteHostLabels"),
      remoteHostCredentialSaved = state.optJSONArray("remoteHostCredentialSaved").toStringList().toSet(),
      threadRemoteShieldHosts = state.stringSetMap("threadRemoteShieldHosts"),
      threadConfiguredApprovalPolicies = state.stringMap("threadConfiguredApprovalPolicies"),
      threadConfiguredSandboxPolicies = state.objectMap("threadConfiguredSandboxPolicies"),
      threadModelSelections = state.stringMap("threadModelSelections"),
      threadReasoningSelections = state.stringMap("threadReasoningSelections"),
      themeId = state.stringOrBlank("theme").ifBlank { "system" },
      sidebarVisible = state.optBoolean("sidebarVisible", true),
    )
  }

  private fun accessMode(threadId: String): String =
    metadata.threadAccessSelections[threadId].orEmpty().ifBlank { "configured" }

  private fun remoteHostChoices(): List<RemoteHostChoice> =
    metadata.remoteHostLabels.keys
      .plus(metadata.remoteHostCredentialSaved)
      .distinct()
      .sorted()
      .map { host ->
        RemoteHostChoice(
          id = host,
          title = metadata.remoteHostLabels[host].orEmpty().ifBlank { host },
          credentialSaved = host in metadata.remoteHostCredentialSaved,
        )
      }

  private fun turnSettings(threadId: String): CodexAppServer.ThreadTurnSettings {
    val yolo = accessMode(threadId) == "yolo"
    val approvalPolicy =
      if (yolo) "never"
      else metadata.threadConfiguredApprovalPolicies[threadId] ?: "on-request"
    val sandboxPolicy =
      if (yolo) {
        JSONObject().put("type", "dangerFullAccess")
      } else {
        metadata.threadConfiguredSandboxPolicies[threadId]
          ?.let { JSONObject(it.toString()) }
          ?: JSONObject().put("type", "workspaceWrite")
      }
    return CodexAppServer.ThreadTurnSettings(
      approvalPolicy = approvalPolicy,
      sandboxPolicy = sandboxPolicy,
      model = metadata.threadModelSelections[threadId].orEmpty(),
      effort = metadata.threadReasoningSelections[threadId].orEmpty(),
      shieldEnabled = threadId in metadata.threadShieldSelections,
      remoteShieldHosts =
        metadata.threadRemoteShieldHosts[threadId]
          .orEmpty()
          .filter { it in metadata.remoteHostCredentialSaved },
    )
  }

  private fun buildProjectGroups(
    threads: List<ThreadSummary>,
    searching: Boolean,
  ): List<ProjectThreads> {
    val grouped = threads.groupBy(ThreadSummary::projectId)
    val projectIds =
      buildList {
        addAll(metadata.projectIds)
        threads.map(ThreadSummary::projectId).filter(String::isNotBlank).forEach { id ->
          if (id !in this) add(id)
        }
      }
    val projects =
      projectIds.mapNotNull { projectId ->
        val cwd = metadata.projectPaths[projectId] ?: grouped[projectId]?.firstOrNull()?.cwd.orEmpty()
        val projectThreads = sortThreads(grouped[projectId].orEmpty(), metadata.projectThreadSorts[projectId])
        if (searching && projectThreads.isEmpty()) return@mapNotNull null
        ProjectThreads(
          id = projectId,
          title = metadata.projectLabels[projectId]?.takeIf(String::isNotBlank) ?: folderName(cwd),
          cwd = cwd,
          threads = projectThreads,
        )
      }

    val sortId = metadata.projectSort
    val byName = sortId == "name-asc" || sortId == "name-desc"
    val descending = sortId == "updated-desc" || sortId == "name-desc"
    return projects.sortedWith { left, right ->
      val comparison =
        if (byName) {
          left.title.lowercase().compareTo(right.title.lowercase())
        } else {
          val leftUpdated = left.threads.maxOfOrNull(ThreadSummary::updatedAt) ?: 0L
          val rightUpdated = right.threads.maxOfOrNull(ThreadSummary::updatedAt) ?: 0L
          leftUpdated.compareTo(rightUpdated)
        }
      when {
        comparison != 0 && descending -> -comparison
        comparison != 0 -> comparison
        else -> left.id.compareTo(right.id)
      }
    }
  }

  private fun sortThreads(threads: List<ThreadSummary>, requestedSort: String?): List<ThreadSummary> {
    val sortId = requestedSort ?: "updated-desc"
    val byName = sortId == "name-asc" || sortId == "name-desc"
    val descending = sortId == "updated-desc" || sortId == "name-desc"
    return threads.sortedWith { left, right ->
      val comparison =
        if (byName) {
          left.title.lowercase().compareTo(right.title.lowercase())
        } else {
          left.updatedAt.compareTo(right.updatedAt)
        }
      when {
        comparison != 0 && descending -> -comparison
        comparison != 0 -> comparison
        else -> left.id.compareTo(right.id)
      }
    }
  }

  private fun folderName(path: String): String {
    if (path.isBlank()) return "No folder"
    return path.trimEnd('/').substringAfterLast('/').ifBlank { path }
  }

  private fun JSONObject.stringMap(key: String): Map<String, String> {
    val values = optJSONObject(key) ?: return emptyMap()
    return buildMap {
      val keys = values.keys()
      while (keys.hasNext()) {
        val itemKey = keys.next()
        values.stringOrBlank(itemKey).takeIf(String::isNotBlank)?.let { put(itemKey, it) }
      }
    }
  }

  private fun JSONObject.objectMap(key: String): Map<String, JSONObject> {
    val values = optJSONObject(key) ?: return emptyMap()
    return buildMap {
      val keys = values.keys()
      while (keys.hasNext()) {
        val itemKey = keys.next()
        values.optJSONObject(itemKey)?.let { put(itemKey, JSONObject(it.toString())) }
      }
    }
  }

  private fun JSONObject.stringSetMap(key: String): Map<String, Set<String>> {
    val values = optJSONObject(key) ?: return emptyMap()
    return buildMap {
      val keys = values.keys()
      while (keys.hasNext()) {
        val itemKey = keys.next()
        put(itemKey, values.optJSONArray(itemKey).toStringList().toSet())
      }
    }
  }

  private fun JSONArray?.toStringList(): List<String> {
    if (this == null) return emptyList()
    return buildList {
      for (index in 0 until length()) optString(index).takeIf(String::isNotBlank)?.let(::add)
    }
  }

  private fun parseTranscript(thread: JSONObject): List<TranscriptEntry> {
    val entries = mutableListOf<TranscriptEntry>()
    val turns = thread.optJSONArray("turns") ?: JSONArray()
    for (turnIndex in 0 until turns.length()) {
      val turn = turns.optJSONObject(turnIndex) ?: continue
      val items = turn.optJSONArray("items") ?: continue
      for (itemIndex in 0 until items.length()) {
        val item = items.optJSONObject(itemIndex) ?: continue
        parseItem(item, true)?.let { parsed ->
          entries +=
            parsed.copy(
              id = item.optString("id", "stored-$turnIndex-$itemIndex"),
            )
        }
      }
    }
    return entries
  }

  private fun parseItem(item: JSONObject, complete: Boolean): TranscriptEntry? {
    val type = item.optString("type")
    val id = item.optString("id", "item-${System.nanoTime()}")
    return when (type) {
      "userMessage" ->
        TranscriptEntry(id, TranscriptKind.USER, "You", renderUserContent(item.optJSONArray("content")))
      "agentMessage" -> {
        val commentary = item.optString("phase") == "commentary"
        TranscriptEntry(
          id,
          if (commentary) TranscriptKind.COMMENTARY else TranscriptKind.AGENT,
          if (commentary) "Codex commentary" else "Codex",
          item.optString("text"),
          running = !complete,
        )
      }
      "reasoning" ->
        TranscriptEntry(
          id,
          TranscriptKind.REASONING,
          "Codex reasoning",
          joinStrings(item.optJSONArray("summary")),
          running = !complete,
        )
      "plan" -> TranscriptEntry(id, TranscriptKind.PLAN, "Codex plan", item.optString("text"), !complete)
      "commandExecution" -> {
        val command = jsonText(item.opt("command"))
        val output = item.optString("aggregatedOutput")
        val body = buildString {
          if (command.isNotBlank()) append("$ ").append(command)
          if (output.isNotBlank()) {
            if (isNotEmpty()) append('\n')
            append(output)
          }
          if (item.has("exitCode")) append("\n[exit ").append(item.optInt("exitCode")).append(']')
        }
        TranscriptEntry(id, TranscriptKind.ACTIVITY, if (complete) "Codex ran command" else "Codex running command", body, !complete)
      }
      "fileChange" -> TranscriptEntry(id, TranscriptKind.ACTIVITY, "Codex changed files", renderFileChanges(item.optJSONArray("changes")), !complete)
      "mcpToolCall", "dynamicToolCall" -> {
        val name = listOf(item.optString("server"), item.optString("tool")).filter { it.isNotBlank() }.joinToString("/")
        TranscriptEntry(id, TranscriptKind.ACTIVITY, if (complete) "Codex called tool" else "Codex calling tool", name.ifBlank { item.toString(2) }, !complete)
      }
      "webSearch" -> TranscriptEntry(id, TranscriptKind.ACTIVITY, "Codex searched the web", jsonText(item.opt("query")), !complete)
      "contextCompaction" -> TranscriptEntry(id, TranscriptKind.ACTIVITY, "Context compaction", "Conversation context was compacted.", !complete)
      "imageView" -> TranscriptEntry(id, TranscriptKind.ACTIVITY, "Codex viewed image", item.optString("path"), !complete)
      else -> null
    }
  }

  private fun renderUserContent(content: JSONArray?): String {
    if (content == null) return ""
    return buildList {
      for (index in 0 until content.length()) {
        when (val part = content.opt(index)) {
          is String -> add(part)
          is JSONObject -> {
            val text = part.optString("text")
            if (text.isNotBlank()) add(text)
            else if (part.optString("type").isNotBlank()) add("[${part.optString("type")}]")
          }
        }
      }
    }.joinToString("\n")
  }

  private fun renderFileChanges(changes: JSONArray?): String {
    if (changes == null) return "File details pending"
    return buildList {
      for (index in 0 until changes.length()) {
        changes.optJSONObject(index)?.let { change ->
          add("${change.optString("kind", "change")} ${change.optString("path", "unknown path")}")
        }
      }
    }.joinToString("\n")
  }

  private fun joinStrings(values: JSONArray?): String {
    if (values == null) return ""
    return buildList {
      for (index in 0 until values.length()) values.optString(index).takeIf { it.isNotBlank() }?.let(::add)
    }.joinToString("\n")
  }

  private fun jsonText(value: Any?): String =
    when (value) {
      null, JSONObject.NULL -> ""
      is String -> value
      else -> value.toString()
    }

  private fun JSONObject.stringOrBlank(key: String): String =
    if (!has(key) || isNull(key)) "" else optString(key)

  private fun onMain(block: () -> Unit) {
    viewModelScope.launch(Dispatchers.Main.immediate) { block() }
  }
}

const val DEFAULT_ENDPOINT = "ws://127.0.0.1:4545"
private const val TAG = "ThreadDeckTablet"
