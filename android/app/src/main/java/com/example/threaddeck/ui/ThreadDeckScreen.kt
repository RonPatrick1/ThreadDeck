package com.threaddeck.tablet.ui

import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.imePadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawingPadding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.text.selection.SelectionContainer
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.AdminPanelSettings
import androidx.compose.material.icons.filled.ContentCopy
import androidx.compose.material.icons.filled.Pause
import androidx.compose.material.icons.filled.PlayArrow
import androidx.compose.material.icons.filled.Shield
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.FilledTonalButton
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Scaffold
import androidx.compose.material3.SnackbarHost
import androidx.compose.material3.SnackbarHostState
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.material3.VerticalDivider
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.platform.LocalClipboardManager
import androidx.compose.ui.platform.LocalFocusManager
import androidx.compose.ui.platform.LocalSoftwareKeyboardController
import androidx.compose.ui.text.AnnotatedString
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.threaddeck.tablet.theme.ThreadDeckPalettes

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun ThreadDeckScreen(
  state: ThreadDeckUiState,
  actions: ThreadDeckActions,
) {
  val snackbar = remember { SnackbarHostState() }

  LaunchedEffect(state.message) {
    state.message?.let {
      snackbar.showSnackbar(it)
      actions.clearMessage()
    }
  }

  Scaffold(
    modifier = Modifier.fillMaxSize(),
    topBar = {
      TopAppBar(
        colors =
          TopAppBarDefaults.topAppBarColors(
            containerColor = MaterialTheme.colorScheme.surfaceContainerHigh,
          ),
        title = {
          Row(verticalAlignment = Alignment.CenterVertically) {
            Text("ThreadDeck", fontWeight = FontWeight.Bold)
            Spacer(Modifier.width(12.dp))
            ConnectionBadge(state)
          }
        },
        actions = {
          TextButton(onClick = { actions.setNavigationVisible(!state.navigationVisible) }) {
            Text(if (state.navigationVisible) "◀ Threads" else "☰ Threads")
          }
          if (state.selectedThreadId != null) {
            TextButton(onClick = { actions.showDetailsDialog(true) }) {
              Text("ⓘ  Details")
            }
          }
          TextButton(onClick = { actions.showThemePicker(true) }) {
            Text("◐  Theme")
          }
          TextButton(onClick = { actions.showConnectionDialog(true) }) {
            Text("⚙  Host")
          }
        },
      )
    },
    snackbarHost = { SnackbarHost(snackbar) },
  ) { padding ->
    BoxWithConstraints(
      modifier =
        Modifier
          .padding(padding)
          .safeDrawingPadding()
          .fillMaxSize(),
    ) {
      if (maxWidth >= 700.dp && state.navigationVisible) {
        Row(Modifier.fillMaxSize()) {
          ThreadPane(
            state,
            actions,
            Modifier.widthIn(min = 340.dp, max = 440.dp).fillMaxHeight(),
          )
          VerticalDivider()
          ConversationPane(state, actions, Modifier.weight(1f).fillMaxHeight())
        }
      } else if (state.selectedThreadId == null && state.navigationVisible) {
        ThreadPane(state, actions, Modifier.fillMaxSize())
      } else {
        ConversationPane(state, actions, Modifier.fillMaxSize())
      }
    }
  }

  if (state.showThemePicker) ThemePicker(state, actions)
  if (state.showConnectionDialog) ConnectionDialog(state, actions)
  if (state.showNewThreadDialog) NewThreadDialog(state, actions)
  if (state.showNewProjectDialog) NewProjectDialog(actions)
  if (state.showYoloConfirmation) YoloConfirmationDialog(actions)
  if (state.showModelPicker) {
    SettingChoiceDialog(
      title = "Model",
      choices = state.models.map { it.id to it.title },
      selectedId = state.selectedModelId,
      onSelect = actions::selectModel,
      onDismiss = { actions.showModelPicker(false) },
    )
  }
  if (state.showReasoningPicker) {
    val efforts =
      state.models
        .firstOrNull { it.id == state.selectedModelId }
        ?.reasoningEfforts
        .orEmpty()
        .let { available ->
          if (state.selectedReasoningEffort.isBlank() || state.selectedReasoningEffort in available) available
          else available + state.selectedReasoningEffort
        }
    SettingChoiceDialog(
      title = "Reasoning effort",
      choices = efforts.map { it to it.replaceFirstChar(Char::uppercase) },
      selectedId = state.selectedReasoningEffort,
      onSelect = actions::selectReasoningEffort,
      onDismiss = { actions.showReasoningPicker(false) },
    )
  }
  if (state.showDetailsDialog) DetailsDialog(state, actions)
  if (state.showSkillPicker) SkillPickerDialog(state, actions)
  if (state.showRemoteShieldDialog) RemoteShieldDialog(state, actions)
  state.approval?.let { ApprovalDialog(it, actions) }
}

@Composable
private fun ConnectionBadge(state: ThreadDeckUiState) {
  val color =
    when (state.connection) {
      HostConnectionState.CONNECTED -> Color(0xFF4CAF76)
      HostConnectionState.CONNECTING -> Color(0xFFF1B84B)
      HostConnectionState.DISCONNECTED -> Color(0xFFE46D72)
    }
  Row(verticalAlignment = Alignment.CenterVertically) {
    Box(Modifier.size(9.dp).background(color, CircleShape))
    Spacer(Modifier.width(6.dp))
    Text(
      when (state.connection) {
        HostConnectionState.CONNECTED -> "Connected"
        HostConnectionState.CONNECTING -> "Connecting"
        HostConnectionState.DISCONNECTED -> "Offline"
      },
      style = MaterialTheme.typography.labelMedium,
      color = MaterialTheme.colorScheme.onSurfaceVariant,
    )
  }
}

@Composable
private fun ThreadPane(
  state: ThreadDeckUiState,
  actions: ThreadDeckActions,
  modifier: Modifier,
) {
  var showProjectSort by remember { mutableStateOf(false) }
  Column(
    modifier = modifier.background(MaterialTheme.colorScheme.surfaceContainerLow),
  ) {
    Row(
      Modifier.fillMaxWidth().padding(horizontal = 12.dp, vertical = 10.dp),
      horizontalArrangement = Arrangement.spacedBy(8.dp),
      verticalAlignment = Alignment.CenterVertically,
    ) {
      Text("Threads", style = MaterialTheme.typography.titleMedium, fontWeight = FontWeight.SemiBold, modifier = Modifier.weight(1f))
      FilledTonalButton(
        onClick = { actions.showNewProjectDialog(true) },
        enabled = state.connection == HostConnectionState.CONNECTED,
        modifier = Modifier.height(48.dp),
      ) {
        Text("＋ Project")
      }
      FilledTonalButton(
        onClick = { actions.showNewThreadDialog(true) },
        enabled = state.connection == HostConnectionState.CONNECTED,
        modifier = Modifier.height(48.dp),
      ) {
        Text("＋ New")
      }
    }

    val allCollapsed =
      state.projects.isNotEmpty() &&
        state.projects.all { it.id in state.collapsedProjects }
    Row(
      Modifier.fillMaxWidth().padding(horizontal = 12.dp),
      horizontalArrangement = Arrangement.spacedBy(8.dp),
    ) {
      OutlinedButton(
        onClick = { actions.showSkillPicker(true) },
        enabled = state.threadWritable && state.skills.isNotEmpty(),
        modifier = Modifier.height(56.dp),
      ) { Text("\$ Skills") }
      OutlinedButton(
        onClick = actions::refreshThreads,
        modifier = Modifier.weight(1f).height(48.dp),
      ) {
        Text("⟳ Refresh", maxLines = 1, fontSize = 12.sp)
      }
      OutlinedButton(
        onClick = { actions.setAllProjectsCollapsed(!allCollapsed) },
        enabled = state.projects.isNotEmpty(),
        modifier = Modifier.weight(1f).height(48.dp),
      ) {
        Text(if (allCollapsed) "▸ All" else "▾ All", maxLines = 1, fontSize = 12.sp)
      }
      Box(Modifier.weight(1f)) {
        OutlinedButton(
          onClick = { showProjectSort = true },
          modifier = Modifier.fillMaxWidth().height(48.dp),
        ) {
          Text("⇅ Sort", maxLines = 1, fontSize = 12.sp)
        }
        SortMenu(
          expanded = showProjectSort,
          onDismiss = { showProjectSort = false },
          onSelect = {
            showProjectSort = false
            actions.setProjectSort(it)
          },
        )
      }
    }

    Spacer(Modifier.height(8.dp))

    OutlinedTextField(
      value = state.search,
      onValueChange = actions::setSearch,
      modifier = Modifier.fillMaxWidth().padding(horizontal = 12.dp),
      singleLine = true,
      label = { Text("Search threads") },
      keyboardOptions = KeyboardOptions(imeAction = ImeAction.Search),
      keyboardActions = KeyboardActions(onSearch = { actions.refreshThreads() }),
    )

    Spacer(Modifier.height(10.dp))

    when {
      state.loadingThreads && state.threads.isEmpty() ->
        Box(Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
          CircularProgressIndicator()
        }
      state.connection == HostConnectionState.DISCONNECTED ->
        OfflinePanel(state, actions)
      state.threads.isEmpty() ->
        Box(Modifier.fillMaxSize().padding(24.dp), contentAlignment = Alignment.Center) {
          Text("No matching Codex threads", color = MaterialTheme.colorScheme.onSurfaceVariant)
        }
      else ->
        LazyColumn(Modifier.fillMaxSize()) {
          state.projects.forEach { project ->
            val collapsed = project.id in state.collapsedProjects
            item(key = "project-${project.id}") {
              ProjectHeader(
                project = project,
                collapsed = collapsed,
                onClick = { actions.toggleProject(project.id) },
                actions = actions,
              )
            }
            if (collapsed) {
              Unit
            } else if (project.threads.isEmpty()) {
              item(key = "empty-${project.id}") {
                Text(
                  "No threads",
                  modifier =
                    Modifier
                      .fillMaxWidth()
                      .padding(start = 76.dp, end = 18.dp, top = 12.dp, bottom = 12.dp),
                  style = MaterialTheme.typography.bodySmall,
                  color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
              }
            } else {
              items(project.threads, key = { it.id }) { thread ->
                ThreadRow(
                  thread = thread,
                  selected = thread.id == state.selectedThreadId,
                  onClick = { actions.selectThread(thread.id) },
                  projects = state.projects,
                  actions = actions,
                )
              }
            }
          }
        }
    }
  }
}

@Composable
private fun ProjectHeader(
  project: ProjectThreads,
  collapsed: Boolean,
  onClick: () -> Unit,
  actions: ThreadDeckActions,
) {
  var showActions by remember(project.id) { mutableStateOf(false) }
  var showSort by remember(project.id) { mutableStateOf(false) }
  var showRename by remember(project.id) { mutableStateOf(false) }
  var showDelete by remember(project.id) { mutableStateOf(false) }
  Column(
    Modifier
      .fillMaxWidth()
      .background(MaterialTheme.colorScheme.primary.copy(alpha = 0.12f))
      .clickable(role = Role.Button, onClick = onClick)
      .heightIn(min = 84.dp)
      .padding(horizontal = 12.dp, vertical = 10.dp),
  ) {
    Row(verticalAlignment = Alignment.CenterVertically) {
      Surface(
        modifier = Modifier.size(52.dp).clickable(role = Role.Button, onClick = onClick),
        color = MaterialTheme.colorScheme.primaryContainer,
        contentColor = MaterialTheme.colorScheme.onPrimaryContainer,
        shape = RoundedCornerShape(10.dp),
      ) {
        Box(contentAlignment = Alignment.Center) {
          Text(if (collapsed) "▸" else "▾", fontSize = 26.sp, fontWeight = FontWeight.Bold)
        }
      }
      Spacer(Modifier.width(12.dp))
      Column(Modifier.weight(1f)) {
        Text(
          "PROJECT",
          style = MaterialTheme.typography.labelSmall,
          fontWeight = FontWeight.Bold,
          color = MaterialTheme.colorScheme.primary,
          letterSpacing = 1.2.sp,
        )
        Text(
          project.title,
          maxLines = 1,
          overflow = TextOverflow.Ellipsis,
          style = MaterialTheme.typography.titleMedium,
          fontWeight = FontWeight.Bold,
        )
        Text(
          project.cwd,
          maxLines = 1,
          overflow = TextOverflow.Ellipsis,
          style = MaterialTheme.typography.labelSmall,
          color = MaterialTheme.colorScheme.onSurfaceVariant,
        )
      }
      Surface(
        color = MaterialTheme.colorScheme.secondaryContainer,
        contentColor = MaterialTheme.colorScheme.onSecondaryContainer,
        shape = RoundedCornerShape(999.dp),
      ) {
        Text(
          project.threads.size.toString(),
          Modifier.padding(horizontal = 10.dp, vertical = 6.dp),
          style = MaterialTheme.typography.labelMedium,
          fontWeight = FontWeight.Bold,
        )
      }
      Box {
        TextButton(
          onClick = { showActions = true },
          modifier = Modifier.size(52.dp),
        ) { Text("⋮", fontSize = 24.sp) }
        DropdownMenu(expanded = showActions, onDismissRequest = { showActions = false }) {
          DropdownMenuItem(
            text = { Text("Rename project") },
            onClick = {
              showActions = false
              showRename = true
            },
          )
          DropdownMenuItem(
            text = { Text("Sort threads") },
            onClick = {
              showActions = false
              showSort = true
            },
          )
          DropdownMenuItem(
            text = { Text("Delete project") },
            enabled = project.threads.isEmpty(),
            onClick = {
              showActions = false
              showDelete = true
            },
          )
        }
        SortMenu(
          expanded = showSort,
          onDismiss = { showSort = false },
          onSelect = {
            showSort = false
            actions.setThreadSort(project.id, it)
          },
        )
      }
    }
  }
  HorizontalDivider(
    thickness = 2.dp,
    color = MaterialTheme.colorScheme.primary.copy(alpha = 0.42f),
  )
  if (showRename) {
    RenameDialog(
      title = "Rename project",
      initialValue = project.title,
      onSave = {
        showRename = false
        actions.renameProject(project.id, it)
      },
      onDismiss = { showRename = false },
    )
  }
  if (showDelete) {
    ConfirmationDialog(
      title = "Delete project?",
      message = "This removes the empty ThreadDeck project. It does not delete the Ubuntu folder.",
      confirmLabel = "Delete",
      onConfirm = {
        showDelete = false
        actions.deleteProject(project.id)
      },
      onDismiss = { showDelete = false },
    )
  }
}

@Composable
private fun OfflinePanel(state: ThreadDeckUiState, actions: ThreadDeckActions) {
  Column(
    Modifier.fillMaxSize().padding(24.dp),
    verticalArrangement = Arrangement.Center,
    horizontalAlignment = Alignment.CenterHorizontally,
  ) {
    Text("ThreadDeck host is unavailable", fontWeight = FontWeight.SemiBold)
    Spacer(Modifier.height(8.dp))
    Text(
      state.connectionMessage,
      color = MaterialTheme.colorScheme.onSurfaceVariant,
      style = MaterialTheme.typography.bodySmall,
    )
    Spacer(Modifier.height(16.dp))
    Button(onClick = actions::reconnect) { Text("Reconnect") }
  }
}

@Composable
private fun ThreadRow(
  thread: ThreadSummary,
  selected: Boolean,
  onClick: () -> Unit,
  projects: List<ProjectThreads>,
  actions: ThreadDeckActions,
) {
  var showActions by remember(thread.id) { mutableStateOf(false) }
  var showRename by remember(thread.id) { mutableStateOf(false) }
  var showMove by remember(thread.id) { mutableStateOf(false) }
  var showDelete by remember(thread.id) { mutableStateOf(false) }
  val background =
    if (selected) MaterialTheme.colorScheme.primary.copy(alpha = 0.16f)
    else MaterialTheme.colorScheme.surfaceContainerLow
  Row(
    Modifier
      .fillMaxWidth()
      .background(background)
      .clickable(onClick = onClick)
      .heightIn(min = 76.dp)
      .padding(start = 24.dp, end = 8.dp, top = 10.dp, bottom = 10.dp),
    verticalAlignment = Alignment.CenterVertically,
  ) {
    Box(
      Modifier
        .width(4.dp)
        .height(48.dp)
        .background(
          if (selected) MaterialTheme.colorScheme.primary
          else MaterialTheme.colorScheme.outlineVariant,
          RoundedCornerShape(999.dp),
        ),
    )
    Spacer(Modifier.width(12.dp))
    Column(Modifier.weight(1f)) {
      Row(verticalAlignment = Alignment.CenterVertically) {
        if (thread.pinned) {
          Text("◆", color = MaterialTheme.colorScheme.primary, fontSize = 9.sp)
          Spacer(Modifier.width(6.dp))
        }
        Text(
          thread.title,
          maxLines = 2,
          overflow = TextOverflow.Ellipsis,
          fontWeight = if (selected) FontWeight.SemiBold else FontWeight.Normal,
          modifier = Modifier.weight(1f),
        )
        Text(
          "THREAD",
          style = MaterialTheme.typography.labelSmall,
          fontWeight = FontWeight.Bold,
          color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.72f),
          letterSpacing = 0.8.sp,
        )
      }
      Text(
        thread.cwd,
        maxLines = 1,
        overflow = TextOverflow.Ellipsis,
        style = MaterialTheme.typography.labelSmall,
        color = MaterialTheme.colorScheme.onSurfaceVariant,
      )
      if (thread.updatedLabel.isNotBlank()) {
        Text(
          thread.updatedLabel,
          style = MaterialTheme.typography.labelSmall,
          color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.7f),
        )
      }
    }
    Box {
      TextButton(
        onClick = { showActions = true },
        modifier = Modifier.size(52.dp),
      ) { Text("⋮", fontSize = 24.sp) }
      DropdownMenu(expanded = showActions, onDismissRequest = { showActions = false }) {
        DropdownMenuItem(
          text = { Text("Rename label") },
          onClick = {
            showActions = false
            showRename = true
          },
        )
        DropdownMenuItem(
          text = { Text("Summarize and title") },
          onClick = {
            showActions = false
            actions.summarizeAndTitle(thread.id)
          },
        )
        DropdownMenuItem(
          text = { Text("Move to project") },
          enabled = projects.size > 1,
          onClick = {
            showActions = false
            showMove = true
          },
        )
        DropdownMenuItem(
          text = { Text("Delete thread") },
          onClick = {
            showActions = false
            showDelete = true
          },
        )
      }
    }
  }
  HorizontalDivider(color = MaterialTheme.colorScheme.outlineVariant)
  if (showRename) {
    RenameDialog(
      title = "Rename thread",
      initialValue = thread.title,
      onSave = {
        showRename = false
        actions.renameThread(thread.id, it)
      },
      onDismiss = { showRename = false },
    )
  }
  if (showMove) {
    SettingChoiceDialog(
      title = "Move to project",
      choices = projects.filterNot { it.id == thread.projectId }.map { it.id to it.title },
      selectedId = thread.projectId,
      onSelect = {
        showMove = false
        actions.moveThread(thread.id, it)
      },
      onDismiss = { showMove = false },
    )
  }
  if (showDelete) {
    ConfirmationDialog(
      title = "Delete thread permanently?",
      message = "${thread.title}\n\nThis removes the Codex thread and cannot be undone.",
      confirmLabel = "Delete",
      onConfirm = {
        showDelete = false
        actions.deleteThread(thread.id)
      },
      onDismiss = { showDelete = false },
    )
  }
}

@Composable
private fun ConversationPane(
  state: ThreadDeckUiState,
  actions: ThreadDeckActions,
  modifier: Modifier,
) {
  Column(modifier.background(MaterialTheme.colorScheme.surface)) {
    ConversationHeader(state, actions)
    HorizontalDivider()

    if (state.selectedThreadId == null) {
      Box(Modifier.weight(1f).fillMaxWidth(), contentAlignment = Alignment.Center) {
        Text("Select a thread to continue working", color = MaterialTheme.colorScheme.onSurfaceVariant)
      }
    } else {
      Transcript(state, Modifier.weight(1f).fillMaxWidth())
      HorizontalDivider()
      Composer(state, actions)
    }
  }
}

@Composable
private fun ConversationHeader(state: ThreadDeckUiState, actions: ThreadDeckActions) {
  Row(
    Modifier.fillMaxWidth().padding(horizontal = 18.dp, vertical = 12.dp),
    verticalAlignment = Alignment.CenterVertically,
  ) {
    Column(Modifier.weight(1f)) {
      Row(
        Modifier.fillMaxWidth(),
        verticalAlignment = Alignment.CenterVertically,
      ) {
        Column(Modifier.weight(1f)) {
          Text(
            state.selectedThreadTitle,
            style = MaterialTheme.typography.titleMedium,
            fontWeight = FontWeight.SemiBold,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
          )
          Text(
            state.selectedThreadCwd,
            style = MaterialTheme.typography.labelSmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
          )
        }
        if (state.turnRunning) {
          CircularProgressIndicator(Modifier.size(18.dp), strokeWidth = 2.dp)
          Spacer(Modifier.width(8.dp))
          Text("Working", style = MaterialTheme.typography.labelMedium)
          Spacer(Modifier.width(12.dp))
        }
        Text(
          state.usageLabel,
          style = MaterialTheme.typography.labelSmall,
          color = MaterialTheme.colorScheme.onSurfaceVariant,
        )
      }
      Spacer(Modifier.height(10.dp))
      Row(
        Modifier.fillMaxWidth().horizontalScroll(rememberScrollState()),
        horizontalArrangement = Arrangement.spacedBy(8.dp),
        verticalAlignment = Alignment.CenterVertically,
      ) {
        ModeBadge(
          text = if (state.selectedAccessMode == "yolo") "YOLO · full access" else "Thread policy",
          containerColor =
            if (state.selectedAccessMode == "yolo") MaterialTheme.colorScheme.errorContainer
            else MaterialTheme.colorScheme.secondaryContainer,
          contentColor =
            if (state.selectedAccessMode == "yolo") MaterialTheme.colorScheme.onErrorContainer
            else MaterialTheme.colorScheme.onSecondaryContainer,
          onClick = {
            actions.requestAccessMode(
              if (state.selectedAccessMode == "yolo") "configured" else "yolo",
            )
          },
        )
        SessionIconButton(
          icon = Icons.Filled.Shield,
          contentDescription =
            if (state.selectedShieldEnabled) "Shield sudo on; tap to turn off"
            else "Shield off; tap to enable sudo",
          containerColor =
            if (state.selectedShieldEnabled) MaterialTheme.colorScheme.errorContainer
            else MaterialTheme.colorScheme.surfaceContainerHighest,
          contentColor =
            if (state.selectedShieldEnabled) MaterialTheme.colorScheme.onErrorContainer
            else MaterialTheme.colorScheme.onSurfaceVariant,
          onClick = { actions.setShieldEnabled(!state.selectedShieldEnabled) },
        )
        SessionIconButton(
          icon = Icons.Filled.ContentCopy,
          contentDescription =
            if (state.selectedAutoCopyEnabled) "Auto-copy on; tap to turn off"
            else "Auto-copy off; tap to turn on",
          containerColor =
            if (state.selectedAutoCopyEnabled) MaterialTheme.colorScheme.tertiaryContainer
            else MaterialTheme.colorScheme.surfaceContainerHighest,
          contentColor =
            if (state.selectedAutoCopyEnabled) MaterialTheme.colorScheme.onTertiaryContainer
            else MaterialTheme.colorScheme.onSurfaceVariant,
          onClick = { actions.setAutoCopyEnabled(!state.selectedAutoCopyEnabled) },
        )
        SessionIconButton(
          icon = if (state.selectedPaused) Icons.Filled.PlayArrow else Icons.Filled.Pause,
          contentDescription = if (state.selectedPaused) "Resume thread" else "Pause thread",
          containerColor =
            if (state.selectedPaused) MaterialTheme.colorScheme.secondaryContainer
            else MaterialTheme.colorScheme.surfaceContainerHighest,
          contentColor =
            if (state.selectedPaused) MaterialTheme.colorScheme.onSecondaryContainer
            else MaterialTheme.colorScheme.onSurfaceVariant,
          onClick = actions::togglePause,
        )
        SessionIconButton(
          icon = Icons.Filled.AdminPanelSettings,
          contentDescription =
            if (state.selectedRemoteShieldHosts.isEmpty()) "Remote Shield hosts"
            else "Remote Shield: ${state.selectedRemoteShieldHosts.size} hosts selected",
          indicator = state.selectedRemoteShieldHosts.size.takeIf { it > 0 }?.toString(),
          containerColor =
            if (state.selectedRemoteShieldHosts.isEmpty()) MaterialTheme.colorScheme.surfaceContainerHighest
            else MaterialTheme.colorScheme.errorContainer,
          contentColor =
            if (state.selectedRemoteShieldHosts.isEmpty()) MaterialTheme.colorScheme.onSurfaceVariant
            else MaterialTheme.colorScheme.onErrorContainer,
          onClick = { actions.showRemoteShieldDialog(true) },
        )
        ModeBadge(
          text = "Continue",
          containerColor = MaterialTheme.colorScheme.surfaceContainerHighest,
          contentColor = MaterialTheme.colorScheme.onSurface,
          onClick = actions::continueThread,
        )
      }
      Spacer(Modifier.height(8.dp))
      Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
        OutlinedButton(
          onClick = { actions.showModelPicker(true) },
          enabled = state.models.isNotEmpty(),
          modifier = Modifier.heightIn(min = 52.dp),
        ) {
          val modelTitle =
            state.models.firstOrNull { it.id == state.selectedModelId }?.title
              ?: state.selectedModelId.ifBlank { "Loading…" }
          Text("Model · $modelTitle", maxLines = 1, overflow = TextOverflow.Ellipsis)
        }
        OutlinedButton(
          onClick = { actions.showReasoningPicker(true) },
          enabled =
            state.models.firstOrNull { it.id == state.selectedModelId }?.reasoningEfforts?.isNotEmpty() == true,
          modifier = Modifier.heightIn(min = 52.dp),
        ) {
          Text(
            "Reasoning · ${state.selectedReasoningEffort.ifBlank { "Default" }}",
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
          )
        }
      }
    }
  }
}

@Composable
private fun SessionIconButton(
  icon: ImageVector,
  contentDescription: String,
  containerColor: Color,
  contentColor: Color,
  onClick: () -> Unit,
  indicator: String? = null,
) {
  val shape = RoundedCornerShape(10.dp)
  Surface(
    modifier =
      Modifier
        .size(52.dp)
        .border(1.dp, contentColor.copy(alpha = 0.55f), shape)
        .clickable(role = Role.Button, onClick = onClick),
    color = containerColor,
    contentColor = contentColor,
    shape = shape,
  ) {
    Box(Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
      Icon(icon, contentDescription, Modifier.size(25.dp))
      if (indicator != null) {
        Text(
          indicator,
          Modifier.align(Alignment.TopEnd).padding(top = 2.dp, end = 5.dp),
          style = MaterialTheme.typography.labelSmall,
          fontWeight = FontWeight.Bold,
        )
      }
    }
  }
}

@Composable
private fun ModeBadge(
  text: String,
  containerColor: Color,
  contentColor: Color,
  onClick: () -> Unit,
) {
  val shape = RoundedCornerShape(10.dp)
  Surface(
    modifier =
      Modifier
        .heightIn(min = 52.dp)
        .border(1.dp, contentColor.copy(alpha = 0.55f), shape)
        .clickable(role = Role.Button, onClick = onClick),
    color = containerColor,
    contentColor = contentColor,
    shape = shape,
  ) {
    Box(
      Modifier.padding(horizontal = 12.dp, vertical = 10.dp),
      contentAlignment = Alignment.Center,
    ) {
      Text(
        text,
        style = MaterialTheme.typography.labelMedium,
        fontWeight = FontWeight.Bold,
        maxLines = 1,
        overflow = TextOverflow.Ellipsis,
      )
    }
  }
}

@Composable
private fun YoloConfirmationDialog(actions: ThreadDeckActions) {
  AlertDialog(
    onDismissRequest = { actions.confirmYolo(false) },
    title = { Text("Enable YOLO full access?") },
    text = {
      Text(
        "The next messages in this thread can read and change anything available to your Ubuntu user without approval prompts. Shield sudo remains a separate setting.",
      )
    },
    confirmButton = {
      Button(onClick = { actions.confirmYolo(true) }) { Text("Enable YOLO") }
    },
    dismissButton = {
      TextButton(onClick = { actions.confirmYolo(false) }) { Text("Cancel") }
    },
  )
}

@Composable
private fun SettingChoiceDialog(
  title: String,
  choices: List<Pair<String, String>>,
  selectedId: String,
  onSelect: (String) -> Unit,
  onDismiss: () -> Unit,
) {
  AlertDialog(
    onDismissRequest = onDismiss,
    title = { Text(title) },
    text = {
      LazyColumn(verticalArrangement = Arrangement.spacedBy(6.dp)) {
        items(choices, key = { it.first }) { choice ->
          Surface(
            modifier = Modifier.fillMaxWidth().clickable { onSelect(choice.first) },
            color =
              if (choice.first == selectedId) MaterialTheme.colorScheme.primaryContainer
              else MaterialTheme.colorScheme.surfaceContainer,
            shape = RoundedCornerShape(10.dp),
          ) {
            Row(Modifier.padding(12.dp), verticalAlignment = Alignment.CenterVertically) {
              Text(choice.second, modifier = Modifier.weight(1f))
              if (choice.first == selectedId) Text("✓", fontWeight = FontWeight.Bold)
            }
          }
        }
      }
    },
    confirmButton = { TextButton(onClick = onDismiss) { Text("Close") } },
  )
}

@Composable
private fun DetailsDialog(state: ThreadDeckUiState, actions: ThreadDeckActions) {
  val project = state.projects.firstOrNull { project ->
    project.threads.any { it.id == state.selectedThreadId }
  }
  AlertDialog(
    onDismissRequest = { actions.showDetailsDialog(false) },
    title = { Text("Thread details") },
    text = {
      Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
        DetailRow("Thread", state.selectedThreadTitle)
        DetailRow("Project", project?.title.orEmpty())
        DetailRow("Folder", state.selectedThreadCwd.trimEnd('/').substringAfterLast('/'))
        DetailRow("Path", state.selectedThreadCwd)
        DetailRow("ID", state.selectedThreadId.orEmpty())
        HorizontalDivider()
        Text(state.usageLabel, fontWeight = FontWeight.SemiBold)
        Text(state.usageDetails, style = MaterialTheme.typography.bodySmall)
      }
    },
    confirmButton = {
      TextButton(onClick = { actions.showDetailsDialog(false) }) { Text("Close") }
    },
  )
}

@Composable
private fun SkillPickerDialog(state: ThreadDeckUiState, actions: ThreadDeckActions) {
  AlertDialog(
    onDismissRequest = { actions.showSkillPicker(false) },
    title = { Text("Codex skills") },
    text = {
      LazyColumn(verticalArrangement = Arrangement.spacedBy(6.dp)) {
        items(state.skills, key = { it.name }) { skill ->
          Surface(
            modifier = Modifier.fillMaxWidth().clickable { actions.selectSkill(skill.name) },
            color = MaterialTheme.colorScheme.surfaceContainer,
            shape = RoundedCornerShape(10.dp),
          ) {
            Column(Modifier.padding(12.dp)) {
              Text("\$${skill.name}", fontWeight = FontWeight.SemiBold)
              if (skill.description.isNotBlank()) {
                Text(
                  skill.description,
                  style = MaterialTheme.typography.bodySmall,
                  color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
              }
            }
          }
        }
      }
    },
    confirmButton = {
      TextButton(onClick = { actions.showSkillPicker(false) }) { Text("Close") }
    },
  )
}

@Composable
private fun RemoteShieldDialog(state: ThreadDeckUiState, actions: ThreadDeckActions) {
  AlertDialog(
    onDismissRequest = { actions.showRemoteShieldDialog(false) },
    title = { Text("Remote Shield hosts") },
    text = {
      if (state.remoteHosts.isEmpty()) {
        Text("No remote hosts have been discovered yet. Run an SSH command or add a host in desktop ThreadDeck first.")
      } else {
        LazyColumn(verticalArrangement = Arrangement.spacedBy(6.dp)) {
          items(state.remoteHosts, key = { it.id }) { host ->
            val selected = host.id in state.selectedRemoteShieldHosts
            Surface(
              modifier =
                Modifier
                  .fillMaxWidth()
                  .clickable(enabled = host.credentialSaved) {
                    actions.setRemoteShieldHost(host.id, !selected)
                  },
              color =
                if (selected) MaterialTheme.colorScheme.tertiaryContainer
                else MaterialTheme.colorScheme.surfaceContainer,
              shape = RoundedCornerShape(10.dp),
            ) {
              Row(Modifier.padding(12.dp), verticalAlignment = Alignment.CenterVertically) {
                Column(Modifier.weight(1f)) {
                  Text(host.title, fontWeight = FontWeight.SemiBold)
                  Text(
                    if (host.credentialSaved) host.id else "${host.id} · credential not saved",
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                  )
                }
                if (selected) Text("✓", fontWeight = FontWeight.Bold)
              }
            }
          }
        }
      }
    },
    confirmButton = {
      TextButton(onClick = { actions.showRemoteShieldDialog(false) }) { Text("Close") }
    },
  )
}

@Composable
private fun DetailRow(label: String, value: String) {
  Row(Modifier.fillMaxWidth()) {
    Text(
      label,
      modifier = Modifier.width(72.dp),
      style = MaterialTheme.typography.labelMedium,
      color = MaterialTheme.colorScheme.onSurfaceVariant,
    )
    Text(value.ifBlank { "—" }, modifier = Modifier.weight(1f), style = MaterialTheme.typography.bodySmall)
  }
}

@Composable
private fun Transcript(state: ThreadDeckUiState, modifier: Modifier) {
  val listState = rememberLazyListState()
  LaunchedEffect(state.transcript.size, state.transcript.lastOrNull()?.text?.length) {
    if (state.transcript.isNotEmpty()) listState.animateScrollToItem(state.transcript.lastIndex)
  }

  when {
    state.loadingTranscript ->
      Box(modifier, contentAlignment = Alignment.Center) { CircularProgressIndicator() }
    state.transcript.isEmpty() ->
      Box(modifier.padding(24.dp), contentAlignment = Alignment.Center) {
        Text("Start the conversation from the tablet.", color = MaterialTheme.colorScheme.onSurfaceVariant)
      }
    else ->
      LazyColumn(
        modifier = modifier,
        state = listState,
        contentPadding = androidx.compose.foundation.layout.PaddingValues(18.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp),
      ) {
        items(state.transcript, key = { it.id }) { entry -> TranscriptCard(entry) }
      }
  }
}

@Composable
private fun TranscriptCard(entry: TranscriptEntry) {
  val user = entry.kind == TranscriptKind.USER
  val activity = entry.kind == TranscriptKind.ACTIVITY
  val fullActivityText = if (activity) entry.text.trimEnd('\r', '\n') else ""
  val abbreviatedActivityText =
    remember(fullActivityText) {
      if (activity) abbreviateActivityText(fullActivityText) else ""
    }
  val activityCanToggle = activity && abbreviatedActivityText != fullActivityText
  var activityExpanded by rememberSaveable(entry.id) { mutableStateOf(false) }
  val container =
    when {
      user -> MaterialTheme.colorScheme.primary.copy(alpha = 0.16f)
      activity -> MaterialTheme.colorScheme.surfaceContainer
      else -> MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.62f)
    }

  Row(
    Modifier.fillMaxWidth(),
    horizontalArrangement = if (user) Arrangement.End else Arrangement.Start,
  ) {
    Card(
      modifier = Modifier.fillMaxWidth(if (user) 0.86f else 0.98f),
      colors = CardDefaults.cardColors(containerColor = container),
      shape = RoundedCornerShape(14.dp),
    ) {
      Column(Modifier.padding(14.dp)) {
        Row(verticalAlignment = Alignment.CenterVertically) {
          Text(
            entry.heading,
            style = MaterialTheme.typography.labelLarge,
            fontWeight = FontWeight.SemiBold,
            color = MaterialTheme.colorScheme.primary,
            modifier = Modifier.weight(1f),
          )
          if (entry.running) CircularProgressIndicator(Modifier.size(13.dp), strokeWidth = 1.5.dp)
        }
        if (entry.text.isNotBlank()) {
          Spacer(Modifier.height(7.dp))
          if (activity) {
            SelectionContainer {
              Text(
                if (activityExpanded) fullActivityText else abbreviatedActivityText,
                style = MaterialTheme.typography.bodySmall.copy(fontFamily = FontFamily.Monospace),
              )
            }
            if (activityCanToggle) {
              Row(
                Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.End,
              ) {
                TextButton(onClick = { activityExpanded = !activityExpanded }) {
                  Text(if (activityExpanded) "↑ Re-abbreviate" else "↓ Expand full details")
                }
              }
            }
          } else {
            TranscriptText(entry.text)
          }
        }
      }
    }
  }
}

internal fun abbreviateActivityText(
  text: String,
  maximumLines: Int = 6,
  maximumLineLength: Int = 180,
): String {
  if (text.isEmpty()) return ""

  val lines = text.split('\n').toMutableList()
  if (lines.size > 1 && lines.last().isEmpty()) lines.removeLast()

  var lineWasTruncated = false
  val visibleLines = lines.take(maximumLines).map { line ->
    if (maximumLineLength > 0 && line.length > maximumLineLength) {
      lineWasTruncated = true
      line.take(maximumLineLength) + "…"
    } else {
      line
    }
  }
  val rendered = visibleLines.toMutableList()

  if (lines.size > visibleLines.size) {
    rendered += "… +${lines.size - visibleLines.size} lines (tap to expand)"
  } else if (lineWasTruncated) {
    rendered += "… (tap to expand full details)"
  }

  return rendered.joinToString("\n")
}

@Composable
private fun TranscriptText(text: String) {
  val clipboard = LocalClipboardManager.current
  val matches = Regex("```([^\\n`]*)\\n([\\s\\S]*?)```").findAll(text).toList()
  if (matches.isEmpty()) {
    SelectionContainer {
      Text(text, style = MaterialTheme.typography.bodyLarge)
    }
    return
  }
  Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
    var cursor = 0
    matches.forEach { match ->
      val prose = text.substring(cursor, match.range.first).trimEnd()
      if (prose.isNotBlank()) {
        SelectionContainer {
          Text(prose, style = MaterialTheme.typography.bodyLarge)
        }
      }
      val language = match.groupValues[1].trim().ifBlank { "code" }
      val code = match.groupValues[2].trimEnd()
      Surface(
        color = MaterialTheme.colorScheme.surfaceContainerHighest,
        shape = RoundedCornerShape(10.dp),
      ) {
        Column {
          Row(
            Modifier.fillMaxWidth().padding(start = 12.dp, end = 4.dp, top = 2.dp),
            verticalAlignment = Alignment.CenterVertically,
          ) {
            Text(language, style = MaterialTheme.typography.labelSmall, modifier = Modifier.weight(1f))
            TextButton(onClick = { clipboard.setText(AnnotatedString(code)) }) { Text("Copy") }
          }
          SelectionContainer {
            Text(
              code,
              Modifier.padding(start = 12.dp, end = 12.dp, bottom = 12.dp),
              style = MaterialTheme.typography.bodySmall.copy(fontFamily = FontFamily.Monospace),
            )
          }
        }
      }
      cursor = match.range.last + 1
    }
    val tail = text.substring(cursor).trimStart()
    if (tail.isNotBlank()) {
      SelectionContainer {
        Text(tail, style = MaterialTheme.typography.bodyLarge)
      }
    }
  }
}

@Composable
private fun Composer(state: ThreadDeckUiState, actions: ThreadDeckActions) {
  val focusManager = LocalFocusManager.current
  val keyboardController = LocalSoftwareKeyboardController.current
  val imagePicker =
    rememberLauncherForActivityResult(ActivityResultContracts.GetMultipleContents()) { uris ->
      actions.addAttachments(uris.map { it.toString() }, "image")
    }
  val audioPicker =
    rememberLauncherForActivityResult(ActivityResultContracts.GetMultipleContents()) { uris ->
      actions.addAttachments(uris.map { it.toString() }, "audio")
    }
  Column(
    Modifier.fillMaxWidth().imePadding().padding(12.dp),
  ) {
    if (state.attachments.isNotEmpty()) {
      Row(
        Modifier.fillMaxWidth().padding(bottom = 8.dp),
        horizontalArrangement = Arrangement.spacedBy(6.dp),
        verticalAlignment = Alignment.CenterVertically,
      ) {
        state.attachments.forEach { attachment ->
          Surface(
            color = MaterialTheme.colorScheme.secondaryContainer,
            shape = RoundedCornerShape(10.dp),
          ) {
            Row(
              Modifier.padding(start = 10.dp, end = 4.dp, top = 4.dp, bottom = 4.dp),
              verticalAlignment = Alignment.CenterVertically,
            ) {
              Text(
                "${if (attachment.kind == "audio") "♫" else "▧"} ${attachment.name}${if (attachment.uploading) " · uploading" else ""}",
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
                style = MaterialTheme.typography.labelMedium,
              )
              TextButton(onClick = { actions.removeAttachment(attachment.id) }) { Text("×") }
            }
          }
        }
        TextButton(onClick = actions::clearAttachments) { Text("Clear") }
      }
    }
    Row(
      Modifier.fillMaxWidth(),
      horizontalArrangement = Arrangement.spacedBy(8.dp),
      verticalAlignment = Alignment.Bottom,
    ) {
      OutlinedButton(
        onClick = { imagePicker.launch("image/*") },
        enabled = state.threadWritable,
        modifier = Modifier.height(56.dp),
      ) { Text("▧ Image") }
      OutlinedButton(
        onClick = { audioPicker.launch("audio/*") },
        enabled = state.threadWritable,
        modifier = Modifier.height(56.dp),
      ) { Text("♫ Audio") }
      OutlinedTextField(
        value = state.draft,
        onValueChange = actions::setDraft,
        modifier = Modifier.weight(1f),
        enabled = state.connection == HostConnectionState.CONNECTED,
        minLines = 1,
        maxLines = 6,
        placeholder = {
          Text(
            when {
              !state.threadWritable -> "Waiting for desktop handoff…"
              state.turnRunning -> "Send a follow-up…"
              else -> "Message Codex…"
            },
          )
        },
        keyboardOptions = KeyboardOptions(imeAction = ImeAction.Default),
      )
      if (state.turnRunning && state.activeTurnId != null) {
        OutlinedButton(onClick = actions::stopTurn, modifier = Modifier.height(56.dp)) { Text("■ Stop") }
      }
      Button(
        onClick = {
          actions.send()
          keyboardController?.hide()
          focusManager.clearFocus()
        },
        enabled =
          (state.draft.isNotBlank() || state.attachments.any { !it.uploading }) &&
            state.attachments.none(TabletAttachment::uploading) &&
            state.connection == HostConnectionState.CONNECTED &&
            state.threadWritable,
        modifier = Modifier.height(56.dp),
      ) {
        Text(if (state.turnRunning) "↪ Follow up" else "➤ Send")
      }
    }
  }
}

@Composable
private fun ThemePicker(state: ThreadDeckUiState, actions: ThreadDeckActions) {
  AlertDialog(
    onDismissRequest = { actions.showThemePicker(false) },
    title = { Text("ThreadDeck theme") },
    text = {
      LazyColumn(verticalArrangement = Arrangement.spacedBy(6.dp)) {
        item {
          ThemeChoice("system", "System", state.themeId == "system", MaterialTheme.colorScheme.primary) {
            actions.setTheme("system")
          }
        }
        items(ThreadDeckPalettes, key = { it.id }) { palette ->
          ThemeChoice(palette.id, palette.name, state.themeId == palette.id, palette.accentBackground) {
            actions.setTheme(palette.id)
          }
        }
      }
    },
    confirmButton = {
      TextButton(onClick = { actions.showThemePicker(false) }) { Text("Close") }
    },
  )
}

@Composable
private fun ThemeChoice(
  id: String,
  name: String,
  selected: Boolean,
  swatch: Color,
  onClick: () -> Unit,
) {
  Row(
    Modifier
      .fillMaxWidth()
      .border(
        if (selected) 2.dp else 1.dp,
        if (selected) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.outlineVariant,
        RoundedCornerShape(10.dp),
      )
      .clickable(onClick = onClick)
      .padding(10.dp),
    verticalAlignment = Alignment.CenterVertically,
  ) {
    Box(Modifier.size(30.dp).background(swatch, CircleShape))
    Spacer(Modifier.width(12.dp))
    Text(name, modifier = Modifier.weight(1f))
    if (selected) Text("✓", color = MaterialTheme.colorScheme.primary, fontWeight = FontWeight.Bold)
  }
}

@Composable
private fun ConnectionDialog(state: ThreadDeckUiState, actions: ThreadDeckActions) {
  var endpoint by remember(state.endpoint) { mutableStateOf(state.endpoint) }
  AlertDialog(
    onDismissRequest = { actions.showConnectionDialog(false) },
    title = { Text("ThreadDeck host") },
    text = {
      Column {
        Text(
          "Wireless debugging uses the tablet's localhost tunnel. Codex and your files remain on Ubuntu.",
          style = MaterialTheme.typography.bodySmall,
          color = MaterialTheme.colorScheme.onSurfaceVariant,
        )
        Spacer(Modifier.height(12.dp))
        OutlinedTextField(
          value = endpoint,
          onValueChange = { endpoint = it },
          label = { Text("WebSocket address") },
          singleLine = true,
          modifier = Modifier.fillMaxWidth(),
        )
      }
    },
    confirmButton = { Button(onClick = { actions.setEndpoint(endpoint) }) { Text("Connect") } },
    dismissButton = { TextButton(onClick = { actions.showConnectionDialog(false) }) { Text("Cancel") } },
  )
}

@Composable
private fun NewThreadDialog(state: ThreadDeckUiState, actions: ThreadDeckActions) {
  var cwd by remember(state.selectedThreadCwd) { mutableStateOf(state.selectedThreadCwd) }
  AlertDialog(
    onDismissRequest = { actions.showNewThreadDialog(false) },
    title = { Text("New Codex thread") },
    text = {
      Column {
        Text("Enter a project folder on the Ubuntu host.", style = MaterialTheme.typography.bodySmall)
        Spacer(Modifier.height(10.dp))
        OutlinedTextField(
          value = cwd,
          onValueChange = { cwd = it },
          label = { Text("Host project path") },
          placeholder = { Text("/var/www/MyProject") },
          singleLine = true,
          modifier = Modifier.fillMaxWidth(),
        )
      }
    },
    confirmButton = {
      Button(onClick = { actions.createThread(cwd) }, enabled = cwd.isNotBlank()) { Text("Create") }
    },
    dismissButton = { TextButton(onClick = { actions.showNewThreadDialog(false) }) { Text("Cancel") } },
  )
}

@Composable
private fun NewProjectDialog(actions: ThreadDeckActions) {
  var cwd by remember { mutableStateOf("") }
  AlertDialog(
    onDismissRequest = { actions.showNewProjectDialog(false) },
    title = { Text("Add ThreadDeck project") },
    text = {
      Column {
        Text("Enter an existing project folder on the Ubuntu host.")
        Spacer(Modifier.height(10.dp))
        OutlinedTextField(
          value = cwd,
          onValueChange = { cwd = it },
          label = { Text("Host project path") },
          placeholder = { Text("/var/www/MyProject") },
          singleLine = true,
          modifier = Modifier.fillMaxWidth(),
        )
      }
    },
    confirmButton = {
      Button(onClick = { actions.addProject(cwd) }, enabled = cwd.isNotBlank()) { Text("Add project") }
    },
    dismissButton = {
      TextButton(onClick = { actions.showNewProjectDialog(false) }) { Text("Cancel") }
    },
  )
}

@Composable
private fun RenameDialog(
  title: String,
  initialValue: String,
  onSave: (String) -> Unit,
  onDismiss: () -> Unit,
) {
  var value by remember(initialValue) { mutableStateOf(initialValue) }
  AlertDialog(
    onDismissRequest = onDismiss,
    title = { Text(title) },
    text = {
      OutlinedTextField(
        value = value,
        onValueChange = { value = it },
        label = { Text("Label") },
        singleLine = true,
        modifier = Modifier.fillMaxWidth(),
      )
    },
    confirmButton = { Button(onClick = { onSave(value) }) { Text("Save") } },
    dismissButton = { TextButton(onClick = onDismiss) { Text("Cancel") } },
  )
}

@Composable
private fun ConfirmationDialog(
  title: String,
  message: String,
  confirmLabel: String,
  onConfirm: () -> Unit,
  onDismiss: () -> Unit,
) {
  AlertDialog(
    onDismissRequest = onDismiss,
    title = { Text(title) },
    text = { Text(message) },
    confirmButton = { Button(onClick = onConfirm) { Text(confirmLabel) } },
    dismissButton = { TextButton(onClick = onDismiss) { Text("Cancel") } },
  )
}

@Composable
private fun SortMenu(
  expanded: Boolean,
  onDismiss: () -> Unit,
  onSelect: (String) -> Unit,
) {
  DropdownMenu(expanded = expanded, onDismissRequest = onDismiss) {
    listOf(
      "updated-desc" to "Modified · newest first",
      "updated-asc" to "Modified · oldest first",
      "name-asc" to "Name · A to Z",
      "name-desc" to "Name · Z to A",
    ).forEach { choice ->
      DropdownMenuItem(
        text = { Text(choice.second) },
        onClick = { onSelect(choice.first) },
      )
    }
  }
}

@Composable
private fun ApprovalDialog(approval: ApprovalPrompt, actions: ThreadDeckActions) {
  AlertDialog(
    onDismissRequest = { actions.answerApproval("cancel") },
    title = { Text(approval.title) },
    text = {
      Surface(
        color = MaterialTheme.colorScheme.surfaceContainer,
        shape = RoundedCornerShape(10.dp),
      ) {
        Text(
          approval.details,
          modifier = Modifier.padding(12.dp),
          fontFamily = FontFamily.Monospace,
          style = MaterialTheme.typography.bodySmall,
        )
      }
    },
    confirmButton = {
      Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
        Button(onClick = { actions.answerApproval("accept") }) { Text("Allow once") }
        FilledTonalButton(onClick = { actions.answerApproval("acceptForSession") }) { Text("Allow session") }
      }
    },
    dismissButton = { TextButton(onClick = { actions.answerApproval("decline") }) { Text("Deny") } },
  )
}
