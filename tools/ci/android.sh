#!/usr/bin/env bash
# omui on Android (W10): example 01 packed into an APK (mcpp.plugins'
# dist-apk; the platform's NativeActivity, no Java) and checked inside — the
# application's library, the C++ runtime, the default fonts with their
# licences; then, where the machine can run an emulator (KVM), installed on
# Android 7.0 (API 24, x86_64), started, and driven through the platform's own
# input: it says it runs, a tap on its button changes what it shows, Back with
# nothing open ends it. Screenshots go to $OMUI_ANDROID_OUT (default
# /tmp/omui-android).
#
#   tools/ci/android.sh              MCPP=<path to mcpp> (default: mcpp on PATH)
#
# The emulator, its system image and adb are xlings packages; an AVD is made
# here (no avdmanager), its data partition in /dev/shm where /dev/shm has room
# (the emulator asks for 7 GB free, whatever it writes).
set -euo pipefail
cd "$(dirname "$0")/../.."
mcpp=${MCPP:-mcpp}
out=${OMUI_ANDROID_OUT:-/tmp/omui-android}
mkdir -p "$out"

# 1. The APK.
"$mcpp" pack -p omui.example.hello-im --target x86_64-linux-android --pkg-format apk 2>&1 | tee "$out/pack.log"
apk=$(sed -n 's/.*Packed \(.*hello-im\.apk\).*/\1/p' "$out/pack.log" | tail -1)
apk=${apk/#\~/$HOME}
[ -f "$apk" ] || { echo "::error::no APK was packed"; exit 1; }
listing=$(unzip -l "$apk")
for inside in lib/x86_64/libhello-im.so lib/x86_64/libc++_shared.so assets/fonts/Inter.ttf assets/licenses/fonts/OFL-inter.txt; do
  grep -q " $inside$" <<< "$listing" || { echo "::error::the APK has no $inside"; exit 1; }
done
echo "android: $apk ($(du -h "$apk" | cut -f1)): the library, the C++ runtime, the fonts and their licences"

# 2. On the emulator, where it can run.
if [ ! -w /dev/kvm ]; then
  echo "::notice::no KVM here: the APK was packed and checked, not run"
  exit 0
fi
xlings install android-emulator android-system-image@24-default-x86_64 android-platform-tools -y -g > /dev/null
image=$(ls -d "$HOME"/.xlings/data/xpkgs/xim-x-android-system-image/24-default-x86_64 2>/dev/null | head -1)
[ -n "$image" ] || { echo "::error::the system image is not where xlings installs"; exit 1; }
sdk="$HOME/.android-sdk-omui"
mkdir -p "$sdk/system-images/android-24/default" "$sdk/platforms" "$sdk/platform-tools"
ln -sfn "$image" "$sdk/system-images/android-24/default/x86_64"
avd_home=/dev/shm/omui-avd
[ "$(df --output=avail -k /dev/shm 2>/dev/null | tail -1)" -gt 8000000 ] 2>/dev/null || avd_home="$HOME/.android/avd"
mkdir -p "$HOME/.android/avd" "$avd_home/omui24.avd"
printf 'avd.ini.encoding=UTF-8\npath=%s/omui24.avd\ntarget=android-24\n' "$avd_home" > "$HOME/.android/avd/omui24.ini"
cat > "$avd_home/omui24.avd/config.ini" <<'EOF'
AvdId=omui24
abi.type=x86_64
hw.cpu.arch=x86_64
image.sysdir.1=system-images/android-24/default/x86_64/
tag.id=default
hw.ramSize=2048
hw.lcd.width=720
hw.lcd.height=1280
hw.lcd.density=320
hw.keyboard=yes
hw.gpu.enabled=yes
hw.gpu.mode=swiftshader_indirect
EOF
ANDROID_SDK_ROOT="$sdk" ANDROID_HOME="$sdk" emulator -avd omui24 -no-window -no-audio -no-snapshot -no-boot-anim -gpu swiftshader_indirect -no-metrics \
  > "$out/emulator.log" 2>&1 &
emulator_pid=$!
trap 'adb emu kill > /dev/null 2>&1 || kill $emulator_pid 2>/dev/null || true' EXIT
adb wait-for-device
for _ in $(seq 1 120); do
  [ "$(adb shell getprop sys.boot_completed 2>/dev/null | tr -d '\r')" = 1 ] && break
  sleep 5
done
[ "$(adb shell getprop sys.boot_completed | tr -d '\r')" = 1 ] || { echo "::error::the emulator did not boot"; tail -20 "$out/emulator.log"; exit 1; }
echo "android: Android $(adb shell getprop ro.build.version.release | tr -d '\r'), $(adb shell wm size | tr -d '\r'), $(adb shell wm density | tr -d '\r')"

adb install -r "$apk" > /dev/null
adb logcat -c
adb shell am start -n org.openmui.hello/android.app.NativeActivity > /dev/null
for _ in $(seq 1 30); do adb logcat -d -s omui:I | grep -q "running" && break; sleep 1; done
adb logcat -d -s omui:I | grep -q "running" || { echo "::error::the application did not start"; adb logcat -d | tail -40; exit 1; }
sleep 3
adb exec-out screencap -p > "$out/hello.png"
# The button, through the platform's own input: what it says changes.
adb shell input tap 360 346
sleep 2
adb exec-out screencap -p > "$out/tapped.png"
if cmp -s "$out/hello.png" "$out/tapped.png"; then echo "::error::a tap on the button changed nothing on the screen"; exit 1; fi
# Back with nothing open: the activity ends, the program returns.
adb shell input keyevent 4
for _ in $(seq 1 15); do adb logcat -d -s omui:I | grep -q "the program ended (0)" && break; sleep 1; done
adb logcat -d -s omui:I | grep -q "the program ended (0)" || { echo "::error::Back did not end the application"; adb logcat -d -s omui:I; exit 1; }
adb logcat -d -s omui:I | sed 's/^/android: /'
echo "android: started, drawn ($out/hello.png), a tap took ($out/tapped.png), Back ended it"
