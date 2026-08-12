#!/usr/bin/env bash
set -euo pipefail

repository_root="$(
  cd "$(dirname "${BASH_SOURCE[0]}")/.." &&
    pwd
)"
mobile_directory="$repository_root/apps/aquacyd_service"
diagnostics_directory="$repository_root/artifacts/android-emulator"
package_name="pl.cydakwarium.cyd_aquarium_mobile"
runtime_directory="${RUNNER_TEMP:-${TMPDIR:-/tmp}}"
main_apk="$runtime_directory/aquacyd-main-current-debug-$$.apk"

cd "$mobile_directory"
app_version="$(
  sed -n \
    's/^version: \([0-9][0-9.]*\)+[0-9][0-9]*$/\1/p' \
    pubspec.yaml
)"
test -n "$app_version"
mkdir -p "$diagnostics_directory"

collect_diagnostics_and_cleanup() {
  adb logcat -d > "$diagnostics_directory/logcat.txt" || true
  rm -f -- "$main_apk"
}
trap collect_diagnostics_and_cleanup EXIT

flutter build apk \
  --debug \
  --flavor current \
  --target lib/main.dart

built_main_apk="build/app/outputs/flutter-apk/app-current-debug.apk"
test -s "$built_main_apk"
test -d "$runtime_directory"
cp "$built_main_apk" "$main_apk"
test -s "$main_apk"
adb wait-for-device
adb install -r -t -g "$main_apk"
adb shell pm grant \
  "$package_name" \
  android.permission.POST_NOTIFICATIONS

flutter test \
  integration_test/notification_smoke_test.dart \
  -d emulator-5554 \
  --flavor current

adb install -r -t -g "$main_apk"
adb shell pm grant \
  "$package_name" \
  android.permission.POST_NOTIFICATIONS
adb shell am force-stop "$package_name"
deep_link_output="$diagnostics_directory/deep-link.txt"
adb shell am start \
  -a SELECT_NOTIFICATION \
  -n "$package_name/.MainActivity" \
  --ei notificationId 4242 \
  --es payload "aquacyd://update/mobile-v$app_version" \
  > "$deep_link_output"
grep -F "Starting:" "$deep_link_output" >/dev/null

package_dump="$diagnostics_directory/package.txt"
adb shell dumpsys package "$package_name" > "$package_dump"
grep -E \
  'android\.permission\.POST_NOTIFICATIONS: granted=true([,[:space:]]|$)' \
  "$package_dump" \
  >/dev/null

notifications_dump="$diagnostics_directory/notifications.txt"
activity_dump="$diagnostics_directory/activity.txt"
runtime_ready=false
for _attempt in {1..30}; do
  adb shell dumpsys notification --noredact > "$notifications_dump"
  adb shell dumpsys activity activities > "$activity_dump"

  if grep -F "$package_name/.MainActivity" "$activity_dump" >/dev/null &&
    grep -F "SELECT_NOTIFICATION" "$activity_dump" >/dev/null &&
    grep -F "aquacyd_critical_alarms_v1" "$notifications_dump" >/dev/null &&
    grep -F "aquacyd_warning_alarms_v1" "$notifications_dump" >/dev/null &&
    grep -F "aquacyd_maintenance_v1" "$notifications_dump" >/dev/null &&
    grep -F "aquacyd_app_updates_v1" "$notifications_dump" >/dev/null
  then
    runtime_ready=true
    break
  fi

  sleep 1
done

test "$runtime_ready" = true
