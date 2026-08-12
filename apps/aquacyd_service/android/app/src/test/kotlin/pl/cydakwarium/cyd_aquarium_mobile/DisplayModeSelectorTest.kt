package pl.cydakwarium.cyd_aquarium_mobile

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class DisplayModeSelectorTest {
    @Test
    fun selectsHighestRefreshRateWithoutChangingResolution() {
        val modes = listOf(
            DisplayModeCandidate(1, 1080, 2400, 59.94f),
            DisplayModeCandidate(2, 1080, 2400, 120f),
            DisplayModeCandidate(3, 1440, 3200, 144f),
        )

        val selected = DisplayModeSelector.selectMaximumForResolution(
            modes,
            width = 1080,
            height = 2400,
        )

        assertEquals(2, selected?.modeId)
        assertEquals(120f, selected?.refreshRate)
    }

    @Test
    fun rejectsInvalidAndTooLowRefreshRates() {
        val modes = listOf(
            DisplayModeCandidate(1, 1080, 2400, Float.NaN),
            DisplayModeCandidate(2, 1080, 2400, 24f),
        )

        val selected = DisplayModeSelector.selectMaximumForResolution(
            modes,
            width = 1080,
            height = 2400,
        )

        assertNull(selected)
    }

    @Test
    fun returnsNullForEmptyOrDifferentResolution() {
        val selected = DisplayModeSelector.selectMaximumForResolution(
            listOf(DisplayModeCandidate(1, 1440, 3200, 144f)),
            width = 1080,
            height = 2400,
        )

        assertNull(selected)
    }
}
