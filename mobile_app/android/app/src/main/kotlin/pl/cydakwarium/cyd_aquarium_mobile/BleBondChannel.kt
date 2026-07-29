package pl.cydakwarium.cyd_aquarium_mobile

import android.Manifest
import android.annotation.SuppressLint
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothManager
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.content.pm.PackageManager
import android.os.Build
import android.os.Handler
import android.os.Looper
import io.flutter.plugin.common.BinaryMessenger
import io.flutter.plugin.common.MethodChannel

class BleBondChannel(
    private val context: Context,
    messenger: BinaryMessenger,
) {
    companion object {
        private const val CHANNEL_NAME = "pl.cydakwarium/ble_bond"
        private const val MIN_TIMEOUT_MS = 5_000L
        private const val MAX_TIMEOUT_MS = 60_000L
    }

    private data class PendingBond(
        val deviceId: String,
        val result: MethodChannel.Result,
        val timeout: Runnable,
    )

    private val mainHandler = Handler(Looper.getMainLooper())
    private val channel = MethodChannel(messenger, CHANNEL_NAME)
    private var pendingBond: PendingBond? = null
    private var receiverRegistered = false

    private val bondReceiver = object : BroadcastReceiver() {
        override fun onReceive(receiverContext: Context?, intent: Intent?) {
            if (intent?.action != BluetoothDevice.ACTION_BOND_STATE_CHANGED) return
            handleBondStateChanged(intent)
        }
    }

    init {
        channel.setMethodCallHandler { call, result ->
            when (call.method) {
                "ensureBonded" -> {
                    val deviceId = call.argument<String>("deviceId")?.trim().orEmpty()
                    val requestedTimeout = call.argument<Number>("timeoutMs")?.toLong()
                        ?: MAX_TIMEOUT_MS
                    ensureBonded(deviceId, requestedTimeout, result)
                }
                else -> result.notImplemented()
            }
        }
    }

    @SuppressLint("MissingPermission")
    private fun ensureBonded(
        deviceId: String,
        requestedTimeoutMs: Long,
        result: MethodChannel.Result,
    ) {
        if (pendingBond != null) {
            result.error(
                "bond_busy",
                "Inne parowanie Bluetooth jest już w toku.",
                null,
            )
            return
        }

        try {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S &&
                context.checkSelfPermission(Manifest.permission.BLUETOOTH_CONNECT) !=
                PackageManager.PERMISSION_GRANTED
            ) {
                result.error(
                    "permission_denied",
                    "Aplikacja nie ma uprawnienia do parowania Bluetooth.",
                    null,
                )
                return
            }

            val manager =
                context.getSystemService(Context.BLUETOOTH_SERVICE) as? BluetoothManager
            val adapter = manager?.adapter
            if (adapter == null || !adapter.isEnabled) {
                result.error(
                    "bluetooth_disabled",
                    "Bluetooth jest wyłączony lub niedostępny.",
                    null,
                )
                return
            }

            adapter.getRemoteDevice(deviceId)
        } catch (_: SecurityException) {
            result.error(
                "permission_denied",
                "System cofnął uprawnienie do parowania Bluetooth.",
                null,
            )
            return
        } catch (_: IllegalArgumentException) {
            result.error(
                "invalid_device_id",
                "System nie rozpoznaje identyfikatora sterownika Bluetooth.",
                null,
            )
            return
        }.also { device ->
            beginBond(device, requestedTimeoutMs, result)
        }
    }

    @SuppressLint("MissingPermission")
    private fun beginBond(
        device: BluetoothDevice,
        requestedTimeoutMs: Long,
        result: MethodChannel.Result,
    ) {
        try {
            if (device.bondState == BluetoothDevice.BOND_BONDED) {
                result.success(
                    mapOf(
                        "state" to "bonded",
                        "alreadyBonded" to true,
                    ),
                )
                return
            }

            val timeoutMs = requestedTimeoutMs.coerceIn(MIN_TIMEOUT_MS, MAX_TIMEOUT_MS)
            val timeout = Runnable {
                finishWithError(
                    code = "bond_timeout",
                    message = "Upłynął czas parowania Bluetooth.",
                )
            }
            pendingBond = PendingBond(device.address, result, timeout)
            registerBondReceiver()
            mainHandler.postDelayed(timeout, timeoutMs)

            when (device.bondState) {
                BluetoothDevice.BOND_BONDED -> finishWithSuccess()
                BluetoothDevice.BOND_BONDING -> Unit
                else -> {
                    if (!device.createBond()) {
                        finishWithError(
                            code = "bond_start_failed",
                            message = "System nie mógł rozpocząć parowania Bluetooth.",
                        )
                    }
                }
            }
        } catch (_: SecurityException) {
            finishInvocationWithError(
                result = result,
                code = "permission_denied",
                message = "System cofnął uprawnienie do parowania Bluetooth.",
            )
        } catch (error: RuntimeException) {
            finishInvocationWithError(
                result = result,
                code = "bond_start_failed",
                message = "System nie mógł rozpocząć parowania Bluetooth.",
                details = error.javaClass.simpleName,
            )
        }
    }

    @SuppressLint("MissingPermission")
    private fun handleBondStateChanged(intent: Intent) {
        val pending = pendingBond ?: return
        val device = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            intent.getParcelableExtra(
                BluetoothDevice.EXTRA_DEVICE,
                BluetoothDevice::class.java,
            )
        } else {
            @Suppress("DEPRECATION")
            intent.getParcelableExtra(BluetoothDevice.EXTRA_DEVICE)
        } ?: return
        try {
            if (!device.address.equals(pending.deviceId, ignoreCase = true)) return

            val state = intent.getIntExtra(
                BluetoothDevice.EXTRA_BOND_STATE,
                BluetoothDevice.ERROR,
            )
            val previousState = intent.getIntExtra(
                BluetoothDevice.EXTRA_PREVIOUS_BOND_STATE,
                BluetoothDevice.ERROR,
            )
            when {
                state == BluetoothDevice.BOND_BONDED &&
                    device.bondState == BluetoothDevice.BOND_BONDED -> finishWithSuccess()
                state == BluetoothDevice.BOND_NONE &&
                    previousState == BluetoothDevice.BOND_BONDING &&
                    device.bondState == BluetoothDevice.BOND_NONE -> finishWithError(
                        code = "bond_rejected",
                        message =
                            "Parowanie zostało odrzucone. Wpisz kod widoczny na ekranie sterownika.",
                    )
            }
        } catch (_: SecurityException) {
            finishWithError(
                code = "permission_denied",
                message = "System cofnął uprawnienie do parowania Bluetooth.",
            )
        }
    }

    private fun finishWithSuccess() {
        val pending = takePendingBond() ?: return
        pending.result.success(
            mapOf(
                "state" to "bonded",
                "alreadyBonded" to false,
            ),
        )
    }

    private fun finishWithError(code: String, message: String) {
        val pending = takePendingBond() ?: return
        pending.result.error(code, message, null)
    }

    private fun finishInvocationWithError(
        result: MethodChannel.Result,
        code: String,
        message: String,
        details: Any? = null,
    ) {
        val pending = pendingBond
        if (pending != null && pending.result === result) {
            takePendingBond()?.result?.error(code, message, details)
        } else {
            result.error(code, message, details)
        }
    }

    private fun takePendingBond(): PendingBond? {
        val pending = pendingBond ?: return null
        pendingBond = null
        mainHandler.removeCallbacks(pending.timeout)
        unregisterBondReceiver()
        return pending
    }

    private fun registerBondReceiver() {
        if (receiverRegistered) return
        val filter = IntentFilter(BluetoothDevice.ACTION_BOND_STATE_CHANGED)
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            context.registerReceiver(bondReceiver, filter, Context.RECEIVER_EXPORTED)
        } else {
            @Suppress("DEPRECATION")
            context.registerReceiver(bondReceiver, filter)
        }
        receiverRegistered = true
    }

    private fun unregisterBondReceiver() {
        if (!receiverRegistered) return
        try {
            context.unregisterReceiver(bondReceiver)
        } catch (_: IllegalArgumentException) {
            // Receiver cleanup remains idempotent during activity teardown.
        }
        receiverRegistered = false
    }

    fun dispose() {
        val pending = takePendingBond()
        pending?.result?.error(
            "bond_cancelled",
            "Parowanie zostało przerwane przez zamknięcie aplikacji.",
            null,
        )
        channel.setMethodCallHandler(null)
        mainHandler.removeCallbacksAndMessages(null)
        unregisterBondReceiver()
    }
}
