package com.threaddeck.tablet.theme

import android.os.Build
import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.dynamicDarkColorScheme
import androidx.compose.material3.dynamicLightColorScheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext

data class ThreadDeckPalette(
  val id: String,
  val name: String,
  val dark: Boolean,
  val accent: Color,
  val accentBackground: Color,
  val accentForeground: Color,
  val windowBackground: Color,
  val viewBackground: Color,
  val headerBackground: Color,
  val sidebarBackground: Color,
  val cardBackground: Color,
  val foreground: Color,
)

private fun color(value: Long) = Color(value)

val ThreadDeckPalettes =
  listOf(
    ThreadDeckPalette("neutral-light", "Neutral Light", false, color(0xFF3F6F98), color(0xFF4B82B1), Color.White, color(0xFFF3F4F5), Color.White, color(0xFFE7E9EB), color(0xFFECEEEF), Color.White, color(0xFF202327)),
    ThreadDeckPalette("neutral-dark", "Neutral Dark", true, color(0xFF83B6E4), color(0xFF3F78A8), Color.White, color(0xFF202226), color(0xFF181A1D), color(0xFF292C31), color(0xFF24272B), color(0xFF2D3036), color(0xFFF1F3F5)),
    ThreadDeckPalette("winter-frost", "Winter Frost", false, color(0xFF547F99), color(0xFF5D8EAA), Color.White, color(0xFFEDF3F7), color(0xFFF9FCFD), color(0xFFDFEAF1), color(0xFFE6EEF3), Color.White, color(0xFF263743)),
    ThreadDeckPalette("spring-moss", "Spring Moss", false, color(0xFF607C57), color(0xFF6F8F63), Color.White, color(0xFFEEF2EA), color(0xFFFAFCF7), color(0xFFE1E8DC), color(0xFFE7ECE2), color(0xFFFCFDFB), color(0xFF293429)),
    ThreadDeckPalette("summer-coast", "Summer Coast", false, color(0xFF367679), color(0xFF42898D), Color.White, color(0xFFEAF4F4), color(0xFFF9FDFD), color(0xFFDCECEC), color(0xFFE3F0F0), Color.White, color(0xFF203637)),
    ThreadDeckPalette("autumn-ember", "Autumn Ember", true, color(0xFFE0A06F), color(0xFFA85F34), Color.White, color(0xFF2A221E), color(0xFF211B18), color(0xFF352A24), color(0xFF302721), color(0xFF3A2E27), color(0xFFF3E9E2)),
    ThreadDeckPalette("midnight-ocean", "Midnight Ocean", true, color(0xFF82B4E0), color(0xFF3B73A5), Color.White, color(0xFF171D29), color(0xFF111722), color(0xFF202839), color(0xFF1C2433), color(0xFF263045), color(0xFFEEF4FB)),
    ThreadDeckPalette("forest-rain", "Forest Rain", true, color(0xFF88B99D), color(0xFF4F7D63), Color.White, color(0xFF18231F), color(0xFF111A17), color(0xFF213029), color(0xFF1D2A25), color(0xFF27382F), color(0xFFEDF5F0)),
    ThreadDeckPalette("lavender-calm", "Lavender Calm", false, color(0xFF725F98), color(0xFF806DA5), Color.White, color(0xFFF1EEF6), color(0xFFFBF9FD), color(0xFFE6E0EF), color(0xFFEBE6F2), Color.White, color(0xFF332D3E)),
    ThreadDeckPalette("storm-slate", "Storm Slate", true, color(0xFF9BB1C8), color(0xFF617C98), Color.White, color(0xFF23272E), color(0xFF1A1E24), color(0xFF2D323B), color(0xFF282D35), color(0xFF343A44), color(0xFFEEF1F5)),
  )

@Composable
fun ThreadDeckTheme(
  themeId: String,
  content: @Composable () -> Unit,
) {
  val palette = ThreadDeckPalettes.firstOrNull { it.id == themeId }
  val colorScheme =
    if (palette == null) {
      val context = LocalContext.current
      if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
        if (isSystemInDarkTheme()) dynamicDarkColorScheme(context) else dynamicLightColorScheme(context)
      } else if (isSystemInDarkTheme()) {
        darkColorScheme()
      } else {
        lightColorScheme()
      }
    } else {
      if (palette.dark) {
        darkColorScheme(
          primary = palette.accent,
          onPrimary = palette.accentForeground,
          primaryContainer = palette.accentBackground,
          onPrimaryContainer = palette.accentForeground,
          background = palette.windowBackground,
          onBackground = palette.foreground,
          surface = palette.viewBackground,
          onSurface = palette.foreground,
          surfaceVariant = palette.cardBackground,
          onSurfaceVariant = palette.foreground.copy(alpha = 0.82f),
          surfaceContainer = palette.cardBackground,
          surfaceContainerLow = palette.sidebarBackground,
          surfaceContainerHigh = palette.headerBackground,
          outline = palette.foreground.copy(alpha = 0.24f),
          outlineVariant = palette.foreground.copy(alpha = 0.14f),
        )
      } else {
        lightColorScheme(
          primary = palette.accent,
          onPrimary = palette.accentForeground,
          primaryContainer = palette.accentBackground,
          onPrimaryContainer = palette.accentForeground,
          background = palette.windowBackground,
          onBackground = palette.foreground,
          surface = palette.viewBackground,
          onSurface = palette.foreground,
          surfaceVariant = palette.cardBackground,
          onSurfaceVariant = palette.foreground.copy(alpha = 0.82f),
          surfaceContainer = palette.cardBackground,
          surfaceContainerLow = palette.sidebarBackground,
          surfaceContainerHigh = palette.headerBackground,
          outline = palette.foreground.copy(alpha = 0.24f),
          outlineVariant = palette.foreground.copy(alpha = 0.14f),
        )
      }
    }

  MaterialTheme(
    colorScheme = colorScheme,
    typography = Typography,
    content = content,
  )
}
