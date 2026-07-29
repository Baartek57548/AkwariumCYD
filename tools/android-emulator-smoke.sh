#!/usr/bin/env bash
set -euo pipefail

repository_root="$(
  cd "$(dirname "${BASH_SOURCE[0]}")/.." &&
    pwd
)"
mobile_directory="$repository_root/mobile_app"
diagnostics_directory="$repository_root/artifacts/android-emulator"
package_name="pl.cydakwarium.cyd_aquarium_mobile"

cd "$mobile_directory"
app_version="$(
  sed -n \
    's/^version: \([0-9][0-9.]*\)+[0-9][0-9]*$/\1/p' \
    pubspec.yaml
)"
test -n "$app_version"
mkdir -p "$diagnostics_directory"

collect_logcat() {
  adb logcat -d > "$diagnostics_directory/logcat.txt" || true
}
trap collect_logcat EXIT

flutter build apk \
  --debug \
  --flavor current \
  --target lib/main.dart

main_apk="build/app/outputs/flutter-apk/app-current-debug.apk"
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
  -W \
  -a SELECT_NOTIFICATION \
  -n "$package_name/.MainActivity" \
  --ei notificationId 4242 \
  --es payload "aquacyd://update/mobile-v$app_version" \
  > "$deep_link_output"
grep -F "Status: ok" "$deep_link_output" >/dev/null
sleep 2

package_dump="$diagnostics_directory/package.txt"
adb shell dumpsys package "$package_name" > "$package_dump"
grep -E \
  'android\.permission\.POST_NOTIFICATIONS: granted=true([,[:space:]]|$)' \
  "$package_dump" \
  >/dev/null

notifications_dump="$diagnostics_directory/notifications.txt"
adb shell dumpsys notification --noredact > "$notifications_dump"
for channel in \
  aquacyd_critical_alarms_v1 \
  aquacyd_warning_alarms_v1 \
  aquacyd_maintenance_v1 \
  aquacyd_app_updates_v1
do
  grep -F "$channel" "$notifications_dump" >/dev/null
done

activity_dump="$diagnostics_directory/activity.txt"
adb shell dumpsys activity activities > "$activity_dump"
grep -F "$package_name/.MainActivity" "$activity_dump" >/dev/null
