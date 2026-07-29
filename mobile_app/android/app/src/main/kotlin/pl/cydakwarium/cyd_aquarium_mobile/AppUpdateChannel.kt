package pl.cydakwarium.cyd_aquarium_mobile

import android.app.Activity
import android.content.ClipData
import android.content.Intent
import android.content.pm.PackageInfo
import android.content.pm.PackageManager
import android.net.Uri
import android.os.Build
import android.provider.Settings
import androidx.core.content.FileProvider
import io.flutter.plugin.common.BinaryMessenger
import io.flutter.plugin.common.MethodCall
import io.flutter.plugin.common.MethodChannel
import java.io.BufferedInputStream
import java.io.File
import java.io.FileInputStream
import java.security.MessageDigest
import java.util.concurrent.Executors
import java.util.concurrent.atomic.AtomicBoolean

internal class AppUpdateChannel(
    private val activity: Activity,
    messenger: BinaryMessenger,
) {
    companion object {
        private const val CHANNEL_NAME = "pl.cydakwarium/app_update"
    }

    private val channel = MethodChannel(messenger, CHANNEL_NAME)
    private val installer = AppUpdateInstaller(activity)
    private val executor = Executors.newSingleThreadExecutor()
    private val installInProgress = AtomicBoolean(false)

    init {
        channel.setMethodCallHandler(::handleMethodCall)
    }

    fun dispose() {
        channel.setMethodCallHandler(null)
        executor.shutdownNow()
    }

    private fun handleMethodCall(call: MethodCall, result: MethodChannel.Result) {
        when (call.method) {
            "getInstallState" -> runCatching(installer::installState)
                .fold(
                    onSuccess = result::success,
                    onFailure = { fail(result, it) },
                )
            "getUpdateDirectory" -> runCatching(installer::updateDirectory)
                .fold(
                    onSuccess = { result.success(it.absolutePath) },
                    onFailure = { fail(result, it) },
                )
            "openUnknownSourcesSettings" -> runCatching {
                installer.openUnknownSourcesSettings()
            }.fold(
                onSuccess = result::success,
                onFailure = { fail(result, it) },
            )
            "installApk" -> prepareAndInstall(call, result)
            else -> result.notImplemented()
        }
    }

    private fun prepareAndInstall(call: MethodCall, result: MethodChannel.Result) {
        val path = call.argument<String>("path")
        val expectedSha256 = call.argument<String>("sha256")
        val expectedVersionName = call.argument<String>("versionName")
        if (path.isNullOrBlank() ||
            expectedSha256.isNullOrBlank() ||
            expectedVersionName.isNullOrBlank()
        ) {
            result.error(
                "INVALID_ARGUMENT",
                "Wymagane są ścieżka APK i suma SHA-256.",
                null,
            )
            return
        }
        if (!installInProgress.compareAndSet(false, true)) {
            result.error(
                "INSTALL_IN_PROGRESS",
                "Weryfikacja aktualizacji już trwa.",
                null,
            )
            return
        }

        executor.execute {
            try {
                val validated = installer.validateApk(
                    path,
                    expectedSha256,
                    expectedVersionName,
                )
                activity.runOnUiThread {
                    try {
                        installer.launchInstaller(validated)
                        result.success(
                            mapOf(
                                "launched" to true,
                                "packageName" to validated.packageName,
                                "versionName" to validated.versionName,
                                "versionCode" to validated.versionCode,
                            ),
                        )
                    } catch (error: Throwable) {
                        fail(result, error)
                    } finally {
                        installInProgress.set(false)
                    }
                }
            } catch (error: Throwable) {
                activity.runOnUiThread {
                    installInProgress.set(false)
                    fail(result, error)
                }
            }
        }
    }

    private fun fail(result: MethodChannel.Result, error: Throwable) {
        val updateError = error as? AppUpdateInstallException
        result.error(
            updateError?.code ?: "INSTALL_LAUNCH_FAILED",
            updateError?.message ?: "Nie udało się przygotować instalacji.",
            null,
        )
    }
}

private data class ValidatedApk(
    val file: File,
    val packageName: String,
    val versionName: String,
    val versionCode: Long,
)

private class AppUpdateInstaller(private val activity: Activity) {
    companion object {
        private const val UPDATE_DIRECTORY_NAME = "updates"
        private const val FILE_PROVIDER_SUFFIX = ".update_file_provider"
        private const val APK_MIME_TYPE = "application/vnd.android.package-archive"
        private const val MINIMUM_APK_BYTES = 1024L
        private const val MAXIMUM_APK_BYTES = 200L * 1024L * 1024L
        private val SHA256_PATTERN = Regex("^[0-9a-fA-F]{64}$")
        private val VERSION_NAME_PATTERN = Regex(
            "^(0|[1-9]\\d{0,8})\\.(0|[1-9]\\d{0,8})\\.(0|[1-9]\\d{0,8})$",
        )
    }

    private val packageManager: PackageManager
        get() = activity.packageManager

    fun installState(): Map<String, Any> {
        val packageInfo = installedPackageInfo()
        return mapOf(
            "packageName" to activity.packageName,
            "versionName" to packageInfo.versionName.orEmpty(),
            "versionCode" to packageInfo.longVersionCodeCompat(),
            "sdkInt" to Build.VERSION.SDK_INT,
            "canRequestPackageInstalls" to canRequestPackageInstalls(),
        )
    }

    fun updateDirectory(): File {
        val directory = File(activity.filesDir, UPDATE_DIRECTORY_NAME)
        if ((!directory.exists() && !directory.mkdirs()) || !directory.isDirectory) {
            throw AppUpdateInstallException(
                "UPDATE_DIRECTORY_ERROR",
                "Nie można utworzyć prywatnego katalogu aktualizacji.",
            )
        }
        return directory.canonicalFile
    }

    fun openUnknownSourcesSettings(): Boolean {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.O) return false
        val intent = Intent(
            Settings.ACTION_MANAGE_UNKNOWN_APP_SOURCES,
            Uri.parse("package:${activity.packageName}"),
        )
        if (intent.resolveActivity(packageManager) == null) {
            throw AppUpdateInstallException(
                "NO_PACKAGE_INSTALLER",
                "Android nie udostępnia ustawień instalowania z tego źródła.",
            )
        }
        activity.startActivity(intent)
        return true
    }

    fun validateApk(
        path: String,
        expectedSha256: String,
        expectedVersionName: String,
    ): ValidatedApk {
        if (!SHA256_PATTERN.matches(expectedSha256) ||
            !VERSION_NAME_PATTERN.matches(expectedVersionName)
        ) {
            throw AppUpdateInstallException(
                "INVALID_ARGUMENT",
                "Suma SHA-256 ma nieprawidłowy format.",
            )
        }

        val directory = updateDirectory()
        val apk = try {
            File(path).canonicalFile
        } catch (error: Exception) {
            throw AppUpdateInstallException(
                "PATH_OUTSIDE_UPDATE_DIR",
                "Nie można rozpoznać ścieżki aktualizacji.",
                error,
            )
        }
        if (apk.parentFile?.canonicalFile != directory) {
            throw AppUpdateInstallException(
                "PATH_OUTSIDE_UPDATE_DIR",
                "Plik znajduje się poza prywatnym katalogiem aktualizacji.",
            )
        }
        if (!apk.isFile || !apk.name.endsWith(".apk", ignoreCase = true)) {
            throw AppUpdateInstallException(
                "FILE_NOT_FOUND",
                "Plik APK nie istnieje.",
            )
        }
        if (apk.length() !in MINIMUM_APK_BYTES..MAXIMUM_APK_BYTES) {
            throw AppUpdateInstallException(
                "APK_TOO_LARGE",
                "Plik APK ma nieprawidłowy rozmiar.",
            )
        }

        val actualSha256 = calculateSha256(apk)
        if (!actualSha256.equals(expectedSha256, ignoreCase = true)) {
            throw AppUpdateInstallException(
                "CHECKSUM_MISMATCH",
                "Suma SHA-256 pobranego APK nie zgadza się z GitHub Release.",
            )
        }

        val archiveInfo = archivePackageInfo(apk)
            ?: throw AppUpdateInstallException(
                "INVALID_APK",
                "Android nie rozpoznaje pobranego pliku jako APK.",
            )
        if (archiveInfo.packageName != activity.packageName) {
            throw AppUpdateInstallException(
                "PACKAGE_MISMATCH",
                "APK jest przeznaczony dla pakietu ${archiveInfo.packageName}.",
            )
        }
        if (archiveInfo.versionName != expectedVersionName) {
            throw AppUpdateInstallException(
                "VERSION_NAME_MISMATCH",
                "Wersja APK ${archiveInfo.versionName} nie zgadza się z wydaniem $expectedVersionName.",
            )
        }

        val currentInfo = installedPackageInfo()
        val archiveVersionCode = archiveInfo.longVersionCodeCompat()
        if (archiveVersionCode <= currentInfo.longVersionCodeCompat()) {
            throw AppUpdateInstallException(
                "DOWNGRADE_NOT_ALLOWED",
                "Kod wersji APK nie jest wyższy od zainstalowanego.",
            )
        }

        val currentSigners = signerDigests(currentInfo)
        val archiveSigners = signerDigests(archiveInfo)
        if (currentSigners.isEmpty() ||
            archiveSigners.isEmpty() ||
            currentSigners != archiveSigners
        ) {
            throw AppUpdateInstallException(
                "SIGNATURE_MISMATCH",
                "Certyfikat podpisu APK nie pasuje do zainstalowanej aplikacji.",
            )
        }

        return ValidatedApk(
            file = apk,
            packageName = archiveInfo.packageName,
            versionName = archiveInfo.versionName.orEmpty(),
            versionCode = archiveVersionCode,
        )
    }

    fun launchInstaller(apk: ValidatedApk) {
        if (!canRequestPackageInstalls()) {
            throw AppUpdateInstallException(
                "INSTALL_PERMISSION_REQUIRED",
                "Android wymaga zgody na instalowanie z AquaCYD.",
            )
        }
        val contentUri = try {
            FileProvider.getUriForFile(
                activity,
                activity.packageName + FILE_PROVIDER_SUFFIX,
                apk.file,
            )
        } catch (error: IllegalArgumentException) {
            throw AppUpdateInstallException(
                "PATH_OUTSIDE_UPDATE_DIR",
                "FileProvider odrzucił ścieżkę APK.",
                error,
            )
        }
        val intent = Intent(Intent.ACTION_VIEW).apply {
            setDataAndType(contentUri, APK_MIME_TYPE)
            clipData = ClipData.newRawUri("AquaCYD update", contentUri)
            addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
        }
        if (intent.resolveActivity(packageManager) == null) {
            throw AppUpdateInstallException(
                "NO_PACKAGE_INSTALLER",
                "Na urządzeniu nie znaleziono instalatora APK.",
            )
        }
        try {
            activity.startActivity(intent)
        } catch (error: Exception) {
            throw AppUpdateInstallException(
                "INSTALL_LAUNCH_FAILED",
                "Android nie uruchomił instalatora APK.",
                error,
            )
        }
    }

    private fun canRequestPackageInstalls(): Boolean {
        return Build.VERSION.SDK_INT < Build.VERSION_CODES.O ||
            packageManager.canRequestPackageInstalls()
    }

    private fun installedPackageInfo(): PackageInfo {
        return getPackageInfo(activity.packageName)
            ?: throw AppUpdateInstallException(
                "INVALID_APK",
                "Nie można odczytać informacji o zainstalowanej aplikacji.",
            )
    }

    private fun archivePackageInfo(apk: File): PackageInfo? {
        return if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            packageManager.getPackageArchiveInfo(
                apk.absolutePath,
                PackageManager.PackageInfoFlags.of(signingFlags().toLong()),
            )
        } else {
            @Suppress("DEPRECATION")
            packageManager.getPackageArchiveInfo(apk.absolutePath, signingFlags())
        }
    }

    private fun getPackageInfo(packageName: String): PackageInfo? {
        return try {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                packageManager.getPackageInfo(
                    packageName,
                    PackageManager.PackageInfoFlags.of(signingFlags().toLong()),
                )
            } else {
                @Suppress("DEPRECATION")
                packageManager.getPackageInfo(packageName, signingFlags())
            }
        } catch (_: PackageManager.NameNotFoundException) {
            null
        }
    }

    private fun signingFlags(): Int {
        return if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
            PackageManager.GET_SIGNING_CERTIFICATES
        } else {
            @Suppress("DEPRECATION")
            PackageManager.GET_SIGNATURES
        }
    }

    private fun signerDigests(packageInfo: PackageInfo): Set<String> {
        val signatures = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
            packageInfo.signingInfo?.apkContentsSigners.orEmpty()
        } else {
            @Suppress("DEPRECATION")
            packageInfo.signatures.orEmpty()
        }
        return signatures.mapTo(mutableSetOf()) { signature ->
            MessageDigest.getInstance("SHA-256")
                .digest(signature.toByteArray())
                .toHex()
        }
    }

    private fun calculateSha256(file: File): String {
        val digest = MessageDigest.getInstance("SHA-256")
        BufferedInputStream(FileInputStream(file), 64 * 1024).use { input ->
            val buffer = ByteArray(64 * 1024)
            while (true) {
                if (Thread.currentThread().isInterrupted) {
                    throw AppUpdateInstallException(
                        "INSTALL_LAUNCH_FAILED",
                        "Weryfikacja APK została przerwana.",
                    )
                }
                val read = input.read(buffer)
                if (read < 0) break
                if (read > 0) digest.update(buffer, 0, read)
            }
        }
        return digest.digest().toHex()
    }
}

private class AppUpdateInstallException(
    val code: String,
    message: String,
    cause: Throwable? = null,
) : Exception(message, cause)

private fun PackageInfo.longVersionCodeCompat(): Long {
    return if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
        longVersionCode
    } else {
        @Suppress("DEPRECATION")
        versionCode.toLong()
    }
}

private fun ByteArray.toHex(): String = joinToString(separator = "") { byte ->
    "%02x".format(byte.toInt() and 0xff)
}
