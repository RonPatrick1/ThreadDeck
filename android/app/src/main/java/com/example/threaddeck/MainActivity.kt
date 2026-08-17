package com.threaddeck.tablet

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.runtime.getValue
import androidx.compose.ui.Modifier
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.lifecycle.viewmodel.compose.viewModel
import com.threaddeck.tablet.theme.ThreadDeckTheme
import com.threaddeck.tablet.ui.ThreadDeckScreen
import com.threaddeck.tablet.ui.ThreadDeckViewModel

class MainActivity : ComponentActivity() {
  override fun onCreate(savedInstanceState: Bundle?) {
    super.onCreate(savedInstanceState)

    enableEdgeToEdge()
    setContent {
      val viewModel: ThreadDeckViewModel = viewModel()
      val state by viewModel.uiState.collectAsStateWithLifecycle()

      ThreadDeckTheme(state.themeId) {
        Surface(
          modifier = Modifier.fillMaxSize(),
          color = MaterialTheme.colorScheme.background,
        ) {
          ThreadDeckScreen(
            state = state,
            actions = viewModel,
          )
        }
      }
    }
  }
}
