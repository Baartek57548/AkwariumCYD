package pl.cydakwarium.cyd_aquarium_mobile

data class DisplayModeCandidate(
    val modeId: Int,
    val width: Int,
    val height: Int,
    val refreshRate: Float,
)

object DisplayModeSelector {
    fun selectMaximumForResolution(
        modes: List<DisplayModeCandidate>,
        width: Int,
        height: Int,
    ): DisplayModeCandidate? = modes
        .asSequence()
        .filter {
            it.width == width &&
                it.height == height &&
                it.refreshRate.isFinite() &&
                it.refreshRate >= 30f
        }
        .maxWithOrNull(
            compareBy<DisplayModeCandidate> { it.refreshRate }
                .thenBy { it.modeId },
        )
}
