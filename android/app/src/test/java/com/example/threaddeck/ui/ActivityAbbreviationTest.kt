package com.threaddeck.tablet.ui

import org.junit.Assert.assertEquals
import org.junit.Test

class ActivityAbbreviationTest {
  @Test
  fun shortActivityIsNotAbbreviated() {
    assertEquals("first\nsecond", abbreviateActivityText("first\nsecond"))
  }

  @Test
  fun extraLinesAreCountedInAbbreviation() {
    val text = (1..8).joinToString("\n") { "line $it" }

    assertEquals(
      (1..6).joinToString("\n") { "line $it" } + "\n… +2 lines (tap to expand)",
      abbreviateActivityText(text),
    )
  }

  @Test
  fun longLineGetsExpandableMarker() {
    assertEquals(
      "abcde…\n… (tap to expand full details)",
      abbreviateActivityText("abcdefgh", maximumLineLength = 5),
    )
  }
}
