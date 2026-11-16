// The Android step (moved from tools/ci/android.sh): example 01's APK, checked inside, then on the emulator.
module omui.devtools.check;

import std;
import mcpplibs.os;
import nlohmann.json;
import omui.devtools.run;
import :support;

namespace omui::devtools::check {

using namespace support;
namespace fs = std::filesystem;

namespace {

void pause(int seconds) { std::this_thread::sleep_for(std::chrono::seconds(seconds)); }

std::string trimmed(std::string text) {
    std::erase(text, '\r');
    while (!text.empty() && (text.back() == '\n' || text.back() == ' ')) text.pop_back();
    return text;
}

// The emulator, headless, ended however the step ends.
class Emulator {
public:
    Emulator(const fs::path& root, const fs::path& sdk, const fs::path& log) : root_(root) {
        namespace process = mcpplibs::os::process;
        process::spawn_options options;
        options.program = "emulator";
        options.args = {"-avd", "omui24", "-no-window", "-no-audio", "-no-snapshot", "-no-boot-anim", "-gpu", "swiftshader_indirect", "-no-metrics"};
        options.env = {{"ANDROID_SDK_ROOT", mcpplibs::os::fs::utf8(sdk)}, {"ANDROID_HOME", mcpplibs::os::fs::utf8(sdk)}};
        options.in = process::stdio::null;
        options.out = process::stdio::pipe;
        options.err = process::stdio::output;
        auto started = process::process::spawn(options);
        if (!started) return;
        running_ = std::make_unique<process::process>(std::move(*started));
        logger_ = std::jthread([this, log] {
            std::ofstream file(log, std::ios::binary);
            for (;;) {
                auto chunk = running_->read_output();
                if (!chunk || chunk->empty()) break;
                file << *chunk << std::flush;
            }
        });
    }
    ~Emulator() {
        if (!running_) return;
        if (!run::program(root_, "adb", {"emu", "kill"}, {.merged = true})) running_->kill();
        if (!running_->wait_for(std::chrono::seconds(30)).value_or(std::nullopt)) running_->kill();
        static_cast<void>(running_->wait());
    }
    [[nodiscard]] bool started() const { return running_ != nullptr; }

private:
    fs::path root_;
    std::unique_ptr<mcpplibs::os::process::process> running_;
    std::jthread logger_;
};

}  // namespace

int android(const fs::path& root, const fs::path& out) {
    std::error_code error;
    fs::create_directories(out, error);
    // 1. The APK, as mcpp's record names it.
    const auto packed = run::program(root, "mcpp", {"pack", "-p", "omui.example.hello-im", "--target", "x86_64-linux-android", "--pkg-format", "apk",
                                                    "--message-format", "json"});
    std::optional<fs::path> apk;
    if (packed && packed->status == 0)
        for (auto part : std::views::split(std::string_view(packed->out), '\n')) {
            const auto line = nlohmann::json::parse(std::string_view(part.begin(), part.end()), nullptr, false);
            if (line.is_discarded() || !line.contains("data") || !line["data"].contains("artifacts")) continue;
            for (const auto& artifact : line["data"]["artifacts"])
                if (artifact.value("path", "").ends_with(".apk")) apk = fs::path(artifact.value("path", ""));
        }
    if (!apk || !fs::exists(*apk)) return failed("no APK was packed");
    const auto listing = run::output(root, "unzip", {"-l", mcpplibs::os::fs::utf8(*apk)});
    if (!listing) return failed(std::format("the APK cannot be listed: {}", listing.error()));
    for (const std::string_view inside : {"lib/x86_64/libhello-im.so", "lib/x86_64/libc++_shared.so", "assets/fonts/Inter.ttf", "assets/licenses/fonts/OFL-inter.txt"})
        if (!std::ranges::any_of(std::views::split(std::string_view(*listing), '\n'), [&](auto part) {
                       return std::string_view(part.begin(), part.end()).ends_with(std::format(" {}", inside));
                   }))
            return failed(std::format("the APK has no {}", inside));
    std::cout << std::format("android: {} ({} KiB): the library, the C++ runtime, the fonts and their licences\n", mcpplibs::os::fs::utf8(*apk),
                             fs::file_size(*apk, error) / 1024)
              << std::flush;

    // 2. On the emulator, where it can run.
    if (!std::fstream("/dev/kvm", std::ios::in | std::ios::out | std::ios::binary)) {
        std::cout << "::notice::no KVM here: the APK was packed and checked, not run\n";
        return 0;
    }
    if (!run::output(root, "xlings", {"install", "android-emulator", "android-system-image@24-default-x86_64", "android-platform-tools", "-y", "-g"}))
        return failed("the emulator could not be installed");
    const auto home = fs::path(mcpplibs::os::env::get("HOME").value_or(""));
    const fs::path image = home / ".xlings/data/xpkgs/xim-x-android-system-image/24-default-x86_64";
    if (!fs::is_directory(image)) return failed("the system image is not where xlings installs");
    const fs::path sdk = home / ".android-sdk-omui";
    for (const auto& made : {sdk / "system-images/android-24/default", sdk / "platforms", sdk / "platform-tools"}) fs::create_directories(made, error);
    fs::remove(sdk / "system-images/android-24/default/x86_64", error);
    fs::create_directory_symlink(image, sdk / "system-images/android-24/default/x86_64", error);
    const auto room = fs::space("/dev/shm", error);
    const fs::path avd_home = !error && room.available > 8'000'000'000ULL ? fs::path("/dev/shm/omui-avd") : home / ".android/avd";
    fs::create_directories(home / ".android/avd", error);
    fs::create_directories(avd_home / "omui24.avd", error);
    std::ofstream(home / ".android/avd/omui24.ini") << std::format("avd.ini.encoding=UTF-8\npath={}/omui24.avd\ntarget=android-24\n", mcpplibs::os::fs::utf8(avd_home));
    std::ofstream(avd_home / "omui24.avd/config.ini") << "AvdId=omui24\nabi.type=x86_64\nhw.cpu.arch=x86_64\nimage.sysdir.1=system-images/android-24/default/x86_64/\n"
                                                         "tag.id=default\nhw.ramSize=2048\nhw.lcd.width=720\nhw.lcd.height=1280\nhw.lcd.density=320\n"
                                                         "hw.keyboard=yes\nhw.gpu.enabled=yes\nhw.gpu.mode=swiftshader_indirect\n";
    const Emulator emulator(root, sdk, out / "emulator.log");
    if (!emulator.started()) return failed("the emulator did not start");
    const auto adb = [&](std::vector<std::string> args) { return trimmed(run::output(root, "adb", std::move(args)).value_or("")); };
    static_cast<void>(run::program(root, "adb", {"wait-for-device"}));
    for (int look = 0; look < 120 && adb({"shell", "getprop", "sys.boot_completed"}) != "1"; ++look) pause(5);
    if (adb({"shell", "getprop", "sys.boot_completed"}) != "1") {
        const std::string log = read_text(out / "emulator.log").value_or("");
        std::cout << log.substr(log.size() > 2000 ? log.size() - 2000 : 0);
        return failed("the emulator did not boot");
    }
    std::cout << std::format("android: Android {}, {}, {}\n", adb({"shell", "getprop", "ro.build.version.release"}), adb({"shell", "wm", "size"}),
                             adb({"shell", "wm", "density"}));
    adb({"install", "-r", mcpplibs::os::fs::utf8(*apk)});
    adb({"logcat", "-c"});
    adb({"shell", "am", "start", "-n", "org.openmui.hello/android.app.NativeActivity"});
    const auto said = [&](std::string_view wanted, int seconds) {
        for (int look = 0; look < seconds; ++look) {
            if (adb({"logcat", "-d", "-s", "omui:I"}).contains(wanted)) return true;
            pause(1);
        }
        return false;
    };
    if (!said("running", 30)) {
        const std::string log = adb({"logcat", "-d"});
        std::cout << log.substr(log.size() > 4000 ? log.size() - 4000 : 0) << '\n';
        return failed("the application did not start");
    }
    pause(3);
    const auto screen = [&](const fs::path& file) {
        const auto shot = run::program(root, "adb", {"exec-out", "screencap", "-p"});
        if (shot) std::ofstream(file, std::ios::binary) << shot->out;
    };
    screen(out / "hello.png");
    // The button, through the platform's own input: what it says changes.
    adb({"shell", "input", "tap", "360", "346"});
    pause(2);
    screen(out / "tapped.png");
    if (read_text(out / "hello.png") == read_text(out / "tapped.png")) return failed("a tap on the button changed nothing on the screen");
    // Back with nothing open: the activity ends, the program returns.
    adb({"shell", "input", "keyevent", "4"});
    if (!said("the program ended (0)", 15)) {
        std::cout << adb({"logcat", "-d", "-s", "omui:I"}) << '\n';
        return failed("Back did not end the application");
    }
    for (auto part : std::views::split(adb({"logcat", "-d", "-s", "omui:I"}), '\n')) std::cout << "android: " << std::string_view(part.begin(), part.end()) << '\n';
    std::cout << std::format("android: started, drawn ({}), a tap took ({}), Back ended it\n", mcpplibs::os::fs::utf8(out / "hello.png"),
                             mcpplibs::os::fs::utf8(out / "tapped.png"));
    return 0;
}

}  // namespace omui::devtools::check
