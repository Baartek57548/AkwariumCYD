package pl.cydakwarium.cyd_aquarium_mobile

import android.content.Context
import android.hardware.display.DisplayManager
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.view.Display
import io.flutter.embedding.android.FlutterActivity
import io.flutter.embedding.engine.FlutterEngine
import io.flutter.plugin.common.MethodChannel
import kotlin.math.abs

class MainActivity : FlutterActivity(), DisplayManager.DisplayListener {
    companion object {
        private const val CHANNEL_NAME = "pl.cydakwarium/display_mode"
        private const val REFRESH_TOLERANCE_HZ = 0.5f
    }

    private val mainHandler = Handler(Looper.getMainLooper())
    private lateinit var displayManager: DisplayManager
    private var methodChannel: MethodChannel? = null
    private var appUpdateChannel: AppUpdateChannel? = null
    private var requestedMode: Display.Mode? = null

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        displayManager = getSystemService(Context.DISPLAY_SERVICE) as DisplayManager
        displayManager.registerDisplayListener(this, mainHandler)
        window.decorView.post { requestMaximumRefreshRate() }
    }

    override fun configureFlutterEngine(flutterEngine: FlutterEngine) {
        super.configureFlutterEngine(flutterEngine)
        appUpdateChannel = AppUpdateChannel(
            this,
            flutterEngine.dartExecutor.binaryMessenger,
        )
        methodChannel = MethodChannel(
            flutterEngine.dartExecutor.binaryMessenger,
            CHANNEL_NAME,
        ).also { channel ->
            channel.setMethodCallHandler { call, result ->
                when (call.method) {
                    "requestMaximumRefreshRate" -> result.success(requestMaximumRefreshRate())
                    "getDisplayInfo" -> result.success(buildDisplayInfo())
                    else -> result.notImplemented()
                }
            }
        }
    }

    override fun onResume() {
        super.onResume()
        window.decorView.post { publishDisplayInfo(requestMaximumRefreshRate()) }
    }

    override fun onDestroy() {
        displayManager.unregisterDisplayListener(this)
        methodChannel?.setMethodCallHandler(null)
        methodChannel = null
        appUpdateChannel?.dispose()
        appUpdateChannel = null
        mainHandler.removeCallbacksAndMessages(null)
        super.onDestroy()
    }

    override fun onDisplayAdded(displayId: Int) = Unit

    override fun onDisplayRemoved(displayId: Int) = Unit

    override fun onDisplayChanged(displayId: Int) {
        val current = currentDisplay() ?: return
        if (current.displayId != displayId) return
        mainHandler.removeCallbacksAndMessages(null)
        mainHandler.postDelayed({
            requestMaximumRefreshRate()
            publishDisplayInfo(buildDisplayInfo())
        }, 120L)
    }

    private fun currentDisplay(): Display? {
        val displayId = window.decorView.display?.displayId ?: Display.DEFAULT_DISPLAY
        return displayManager.getDisplay(displayId)
    }

    private fun selectMaximumMode(display: Display): Display.Mode? {
        val active = display.mode
        val selected = DisplayModeSelector.selectMaximumForResolution(
            modes = display.supportedModes.map {
                DisplayModeCandidate(
                    modeId = it.modeId,
                    width = it.physicalWidth,
                    height = it.physicalHeight,
                    refreshRate = it.refreshRate,
                )
            },
            width = active.physicalWidth,
            height = active.physicalHeight,
        ) ?: return null
        return display.supportedModes.firstOrNull { it.modeId == selected.modeId }
    }

    private fun requestMaximumRefreshRate(): Map<String, Any?> {
        val display = currentDisplay() ?: return buildDisplayInfo(
            error = "Nie można odczytać aktywnego wyświetlacza.",
        )
        val selected = selectMaximumMode(display) ?: return buildDisplayInfo(
            display = display,
            error = "Brak zgodnego trybu dla aktualnej rozdzielczości.",
        )

        val attributes = window.attributes
        val modeChanged = attributes.preferredDisplayModeId != selected.modeId
        val rateChanged = abs(attributes.preferredRefreshRate - selected.refreshRate) >=
            REFRESH_TOLERANCE_HZ
        if (modeChanged || rateChanged) {
            attributes.preferredDisplayModeId = selected.modeId
            attributes.preferredRefreshRate = selected.refreshRate
            window.attributes = attributes
        }
        requestedMode = selected
        return buildDisplayInfo(display = display, requested = selected)
    }

    private fun buildDisplayInfo(
        display: Display? = currentDisplay(),
        requested: Display.Mode? = requestedMode,
        error: String? = null,
    ): Map<String, Any?> {
        val active = display?.mode
        val supported = display?.supportedModes
            ?.filter {
                active != null &&
                    it.physicalWidth == active.physicalWidth &&
                    it.physicalHeight == active.physicalHeight &&
                    it.refreshRate.isFinite()
            }
            ?.sortedBy { it.refreshRate }
            ?.map(::modeToMap)
            .orEmpty()

        return mapOf(
            "supportedModes" to supported,
            "requestedMode" to requested?.let(::modeToMap),
            "activeMode" to active?.let(::modeToMap),
            "error" to error,
        )
    }

    private fun modeToMap(mode: Display.Mode): Map<String, Any> = mapOf(
        "modeId" to mode.modeId,
        "width" to mode.physicalWidth,
        "height" to mode.physicalHeight,
        "refreshRate" to mode.refreshRate.toDouble(),
    )

    private fun publishDisplayInfo(info: Map<String, Any?>) {
        methodChannel?.invokeMethod("displayInfoChanged", info)
    }
}
