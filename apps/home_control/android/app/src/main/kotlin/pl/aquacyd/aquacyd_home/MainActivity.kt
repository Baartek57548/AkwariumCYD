package pl.aquacyd.aquacyd_home

import android.content.Intent
import android.content.pm.PackageInfo
import android.content.pm.PackageManager
import android.content.pm.Signature
import android.net.Uri
import android.os.Build
import android.provider.Settings
import androidx.core.content.FileProvider
import io.flutter.embedding.android.FlutterActivity
import io.flutter.embedding.engine.FlutterEngine
import io.flutter.plugin.common.MethodCall
import io.flutter.plugin.common.MethodChannel
import java.io.File
import java.security.MessageDigest

class MainActivity : FlutterActivity() {
    companion object {
        private const val CHANNEL = "pl.aquacyd.aquacyd_home/app_update"
        private const val MINIMUM_APK_BYTES = 1024L * 1024L
        private const val MAXIMUM_APK_BYTES = 250L * 1024L * 1024L
        private val APK_NAME_PATTERN = Regex("^AquaCYD-Home-[0-9]+\\.[0-9]+\\.[0-9]+\\.apk$")
    }

    override fun configureFlutterEngine(flutterEngine: FlutterEngine) {
        super.configureFlutterEngine(flutterEngine)
        MethodChannel(flutterEngine.dartExecutor.binaryMessenger, CHANNEL)
            .setMethodCallHandler(::handleAppUpdateCall)
    }

    private fun handleAppUpdateCall(call: MethodCall, result: MethodChannel.Result) {
        try {
            when (call.method) {
                "getInstalledInfo" -> result.success(installedInfo())
                "prepareApkPath" -> {
                    val fileName = call.argument<String>("fileName")
                    if (fileName == null || !APK_NAME_PATTERN.matches(fileName)) {
                        result.error(
                            "invalid_file_name",
                            "Nazwa pliku aktualizacji jest nieprawidłowa.",
                            null,
                        )
                        return
                    }
                    val directory = updateDirectory()
                    if (!directory.exists() && !directory.mkdirs()) {
                        result.error(
                            "cache_unavailable",
                            "Nie można utworzyć katalogu aktualizacji.",
                            null,
                        )
                        return
                    }
                    val target = File(directory, fileName).canonicalFile
                    requireInsideUpdateDirectory(target)
                    result.success(target.absolutePath)
                }
                "installApk" -> {
                    val path = call.argument<String>("path")
                    if (path.isNullOrBlank()) {
                        result.error(
                            "invalid_path",
                            "Ścieżka pliku aktualizacji jest pusta.",
                            null,
                        )
                        return
                    }
                    installVerifiedApk(File(path).canonicalFile, result)
                }
                else -> result.notImplemented()
            }
        } catch (error: SecurityException) {
            result.error(
                "security_error",
                "Android zablokował dostęp do pliku aktualizacji.",
                null,
            )
        } catch (error: IllegalArgumentException) {
            result.error(
                "invalid_update",
                error.message ?: "Pakiet aktualizacji jest nieprawidłowy.",
                null,
            )
        } catch (error: Exception) {
            result.error(
                "update_error",
                "Nie udało się przygotować instalatora Androida.",
                null,
            )
        }
    }

    private fun installedInfo(): Map<String, Any> {
        clearFinishedUpdateFiles()
        val info = installedPackageInfo()
        return mapOf(
            "version" to (info.versionName ?: "0.0.0"),
            "buildNumber" to versionCode(info),
        )
    }

    private fun installVerifiedApk(apk: File, result: MethodChannel.Result) {
        requireInsideUpdateDirectory(apk)
        require(apk.isFile) { "Plik aktualizacji nie istnieje." }
        require(apk.length() in MINIMUM_APK_BYTES..MAXIMUM_APK_BYTES) {
            "Rozmiar pliku aktualizacji jest nieprawidłowy."
        }

        val installed = installedPackageInfo()
        val candidate = archivePackageInfo(apk)
            ?: throw IllegalArgumentException("Android nie rozpoznał pobranego APK.")
        require(candidate.packageName == packageName) {
            "Pakiet aktualizacji należy do innej aplikacji."
        }
        require(versionCode(candidate) > versionCode(installed)) {
            "Numer kompilacji aktualizacji nie jest wyższy od zainstalowanego."
        }
        val installedSigners = signerDigests(installed)
        val candidateSigners = signerDigests(candidate)
        require(installedSigners.isNotEmpty() && candidateSigners == installedSigners) {
            "Certyfikat podpisujący APK nie zgadza się z zainstalowaną aplikacją."
        }

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O &&
            !packageManager.canRequestPackageInstalls()
        ) {
            val settingsIntent = Intent(
                Settings.ACTION_MANAGE_UNKNOWN_APP_SOURCES,
                Uri.parse("package:$packageName"),
            )
            startActivity(settingsIntent)
            result.success(mapOf("status" to "permissionRequired"))
            return
        }

        val apkUri = FileProvider.getUriForFile(
            this,
            "$packageName.fileprovider",
            apk,
        )
        val installIntent = Intent(Intent.ACTION_INSTALL_PACKAGE).apply {
            data = apkUri
            addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
            addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
        }
        if (installIntent.resolveActivity(packageManager) == null) {
            result.error(
                "installer_unavailable",
                "Na urządzeniu nie ma systemowego instalatora APK.",
                null,
            )
            return
        }
        startActivity(installIntent)
        result.success(mapOf("status" to "launched"))
    }

    private fun updateDirectory(): File = File(cacheDir, "updates")

    private fun clearFinishedUpdateFiles() {
        updateDirectory().listFiles()?.forEach { file ->
            if (file.isFile && APK_NAME_PATTERN.matches(file.name)) {
                file.delete()
            }
        }
    }

    private fun requireInsideUpdateDirectory(file: File) {
        val directoryPath = updateDirectory().canonicalFile.path + File.separator
        require(file.path.startsWith(directoryPath)) {
            "Plik aktualizacji znajduje się poza prywatnym cache aplikacji."
        }
    }

    @Suppress("DEPRECATION")
    private fun installedPackageInfo(): PackageInfo =
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            packageManager.getPackageInfo(
                packageName,
                PackageManager.PackageInfoFlags.of(
                    PackageManager.GET_SIGNING_CERTIFICATES.toLong(),
                ),
            )
        } else {
            packageManager.getPackageInfo(
                packageName,
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
                    PackageManager.GET_SIGNING_CERTIFICATES
                } else {
                    PackageManager.GET_SIGNATURES
                },
            )
        }

    @Suppress("DEPRECATION")
    private fun archivePackageInfo(apk: File): PackageInfo? =
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            packageManager.getPackageArchiveInfo(
                apk.absolutePath,
                PackageManager.PackageInfoFlags.of(
                    PackageManager.GET_SIGNING_CERTIFICATES.toLong(),
                ),
            )
        } else {
            packageManager.getPackageArchiveInfo(
                apk.absolutePath,
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
                    PackageManager.GET_SIGNING_CERTIFICATES
                } else {
                    PackageManager.GET_SIGNATURES
                },
            )
        }

    @Suppress("DEPRECATION")
    private fun versionCode(info: PackageInfo): Long =
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
            info.longVersionCode
        } else {
            info.versionCode.toLong()
        }

    @Suppress("DEPRECATION")
    private fun signerDigests(info: PackageInfo): Set<String> {
        val signatures: Array<out Signature> = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
            val signingInfo = info.signingInfo ?: return emptySet()
            if (signingInfo.hasMultipleSigners()) {
                signingInfo.apkContentsSigners ?: return emptySet()
            } else {
                signingInfo.signingCertificateHistory ?: return emptySet()
            }
        } else {
            info.signatures ?: return emptySet()
        }
        return signatures.map { signature ->
            MessageDigest.getInstance("SHA-256")
                .digest(signature.toByteArray())
                .joinToString("") { byte -> "%02x".format(byte) }
        }.toSet()
    }
}
