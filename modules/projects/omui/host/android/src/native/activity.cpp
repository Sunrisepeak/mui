// The platform's NativeActivity for openmui.host.android:native: the NDK's
// headers here and nowhere else. The activity's callbacks run on the
// platform's thread; they hand what happened to the program's thread, which
// runs the program's own `main` (the host's loop is in it) — found in this
// library by name, as the platform finds ANativeActivity_onCreate. The input
// queue is read on the platform's thread (its looper), its events handed over
// the same way. The window is the program's to draw in until the platform
// takes it back, which waits until a frame being shown is done.
module;
#include <android/asset_manager.h>
#include <android/configuration.h>
#include <android/input.h>
#include <android/keycodes.h>
#include <android/log.h>
#include <android/looper.h>
#include <android/native_activity.h>
#include <android/native_window.h>
#include <jni.h>
module omui.host.android;

import std;
import mcpplibs.os;
import :native;

namespace omui::android::native {

namespace {

constexpr int input_ident = 1;

ANativeActivity*        activity = nullptr;
std::mutex              handed;          // what is handed over: pending, woken
std::condition_variable arrived;
std::deque<Event>       pending;
bool                    woken = false;
std::mutex              window_guard;    // the window, while a frame is shown in it
ANativeWindow*          window = nullptr;
int                     geometry_width = 0, geometry_height = 0;
std::atomic<float>      density{1.0f};
AInputQueue*            queue = nullptr;
std::atomic<bool>       program_running{false};

void hand(Event event) {
    {
        const std::scoped_lock lock(handed);
        pending.push_back(event);
    }
    arrived.notify_all();
}

void read_density() {
    if (!activity) return;
    AConfiguration* config = AConfiguration_new();
    AConfiguration_fromAssetManager(config, activity->assetManager);
    const std::int32_t dpi = AConfiguration_getDensity(config);
    AConfiguration_delete(config);
    // 0 (ACONFIGURATION_DENSITY_DEFAULT) and "any": the baseline, 160 dpi.
    density.store(dpi > 0 && dpi < 0xfffe ? static_cast<float>(dpi) / 160.0f : 1.0f);
}

// A touch's event: each pointer it names, as the platform numbers them.
void touched(const AInputEvent* event) {
    const std::int32_t action = AMotionEvent_getAction(event);
    const std::int32_t masked = action & AMOTION_EVENT_ACTION_MASK;
    const auto index = static_cast<std::size_t>((action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT);
    const auto pointer_event = [&](Event::Kind kind, std::size_t at) {
        hand({.kind = kind, .pointer = AMotionEvent_getPointerId(event, at), .x = AMotionEvent_getX(event, at), .y = AMotionEvent_getY(event, at)});
    };
    switch (masked) {
        case AMOTION_EVENT_ACTION_DOWN: pointer_event(Event::Kind::touch_down, 0); break;
        case AMOTION_EVENT_ACTION_POINTER_DOWN: pointer_event(Event::Kind::touch_down, index); break;
        case AMOTION_EVENT_ACTION_UP: pointer_event(Event::Kind::touch_up, 0); break;
        case AMOTION_EVENT_ACTION_POINTER_UP: pointer_event(Event::Kind::touch_up, index); break;
        case AMOTION_EVENT_ACTION_CANCEL:
            for (std::size_t at = 0; at < AMotionEvent_getPointerCount(event); ++at) pointer_event(Event::Kind::touch_cancel, at);
            break;
        case AMOTION_EVENT_ACTION_MOVE:
            for (std::size_t at = 0; at < AMotionEvent_getPointerCount(event); ++at) pointer_event(Event::Kind::touch_move, at);
            break;
        default: break;
    }
}

// The input queue's events, on the platform's thread: each handed over, each finished.
int read_input(int, int, void*) {
    AInputEvent* event = nullptr;
    while (queue && AInputQueue_getEvent(queue, &event) >= 0) {
        if (AInputQueue_preDispatchEvent(queue, event)) continue;  // the platform's own (an IME's)
        int handled = 0;
        if (AInputEvent_getType(event) == AINPUT_EVENT_TYPE_MOTION) {
            touched(event);
            handled = 1;
        } else if (AInputEvent_getType(event) == AINPUT_EVENT_TYPE_KEY) {
            const std::int32_t action = AKeyEvent_getAction(event);
            if (action == AKEY_EVENT_ACTION_DOWN || action == AKEY_EVENT_ACTION_UP) {
                hand({.kind = action == AKEY_EVENT_ACTION_DOWN ? Event::Kind::key_down : Event::Kind::key_up, .key = AKeyEvent_getKeyCode(event),
                      .meta = static_cast<std::uint32_t>(AKeyEvent_getMetaState(event))});
                // Back is the program's to answer (it closes what is open, else ends the activity), not the platform's.
                handled = 1;
            }
        }
        AInputQueue_finishEvent(queue, event, handled);
    }
    return 1;
}

void on_window_created(ANativeActivity*, ANativeWindow* made) {
    {
        const std::scoped_lock lock(window_guard);
        window = made;
        geometry_width = geometry_height = 0;
    }
    hand({.kind = Event::Kind::window});
}

void on_window_destroyed(ANativeActivity*, ANativeWindow*) {
    {
        const std::scoped_lock lock(window_guard);  // a frame being shown is done first
        window = nullptr;
    }
    hand({.kind = Event::Kind::no_window});
}

void on_window_resized(ANativeActivity*, ANativeWindow*) { hand({.kind = Event::Kind::resized}); }

void on_input_created(ANativeActivity*, AInputQueue* made) {
    queue = made;
    AInputQueue_attachLooper(queue, ALooper_forThread(), input_ident, read_input, nullptr);
}

void on_input_destroyed(ANativeActivity*, AInputQueue* gone) {
    AInputQueue_detachLooper(gone);
    if (queue == gone) queue = nullptr;
}

void on_configuration_changed(ANativeActivity*) {
    read_density();
    hand({.kind = Event::Kind::resized});
}

void on_focus(ANativeActivity*, int focused) { hand({.kind = Event::Kind::focus, .pointer = focused}); }
void on_pause(ANativeActivity*) { hand({.kind = Event::Kind::paused}); }
void on_resume(ANativeActivity*) { hand({.kind = Event::Kind::resumed}); }

void on_destroy(ANativeActivity* gone) {
    hand({.kind = Event::Kind::destroyed});
    if (activity == gone) activity = nullptr;
}

// The library the activity loaded, as NativeActivity itself finds it: the activity's
// `android.app.lib_name` meta-data ("main" without one). Asked through JNI on the
// platform's thread, while its environment is this activity's.
std::string library_name(ANativeActivity* made) {
    JNIEnv* env = made->env;
    const auto cleared = [&] {
        if (!env->ExceptionCheck()) return false;
        env->ExceptionClear();
        return true;
    };
    jclass activity_class = env->GetObjectClass(made->clazz);
    jobject manager = env->CallObjectMethod(made->clazz, env->GetMethodID(activity_class, "getPackageManager", "()Landroid/content/pm/PackageManager;"));
    jobject component = env->CallObjectMethod(made->clazz, env->GetMethodID(activity_class, "getComponentName", "()Landroid/content/ComponentName;"));
    if (cleared() || !manager || !component) return "main";
    constexpr jint get_meta_data = 128;  // PackageManager.GET_META_DATA
    jobject info = env->CallObjectMethod(
        manager, env->GetMethodID(env->GetObjectClass(manager), "getActivityInfo", "(Landroid/content/ComponentName;I)Landroid/content/pm/ActivityInfo;"), component,
        get_meta_data);
    if (cleared() || !info) return "main";
    jobject bundle = env->GetObjectField(info, env->GetFieldID(env->GetObjectClass(info), "metaData", "Landroid/os/Bundle;"));
    if (cleared() || !bundle) return "main";
    jstring key = env->NewStringUTF("android.app.lib_name");
    auto value = static_cast<jstring>(
        env->CallObjectMethod(bundle, env->GetMethodID(env->GetObjectClass(bundle), "getString", "(Ljava/lang/String;)Ljava/lang/String;"), key));
    if (cleared() || !value) return "main";
    const char* chars = env->GetStringUTFChars(value, nullptr);
    std::string name = chars ? chars : "main";
    if (chars) env->ReleaseStringUTFChars(value, chars);
    return name;
}

std::string loaded_library;  // "lib<name>.so", from the first activity

// The program's own entry, in this library: its `main`, run on a thread of its own.
void run_program() {
    namespace library = mcpplibs::os::library;
    using Entry = int(int, char**);
    const auto self = library::loaded(loaded_library);
    Entry* entry = self ? self->function<Entry>("main") : nullptr;
    if (!entry) {
        __android_log_write(ANDROID_LOG_ERROR, "omui", "the program has no `main` to run (it must be in the application's library)");
        program_running = false;
        if (activity) ANativeActivity_finish(activity);
        return;
    }
    static char name[] = "omui";
    char* arguments[] = {name, nullptr};
    const int code = entry(1, arguments);
    __android_log_print(ANDROID_LOG_INFO, "omui", "the program ended (%d)", code);
    program_running = false;
    if (activity) ANativeActivity_finish(activity);
}

}  // namespace

bool started_by_activity() { return activity != nullptr; }

std::vector<Event> next(std::chrono::milliseconds wait) {
    std::unique_lock lock(handed);
    const auto ready = [] { return !pending.empty() || woken; };
    if (wait.count() < 0) arrived.wait(lock, ready);
    else arrived.wait_for(lock, wait, ready);
    woken = false;
    std::vector<Event> out(pending.begin(), pending.end());
    pending.clear();
    return out;
}

Display display() {
    const std::scoped_lock lock(window_guard);
    Display out;
    out.density = density.load();
    if (window) {
        out.width = ANativeWindow_getWidth(window);
        out.height = ANativeWindow_getHeight(window);
    }
    return out;
}

bool present(const std::uint8_t* pixels, int width, int height, std::size_t stride) {
    const std::scoped_lock lock(window_guard);
    if (!window || width <= 0 || height <= 0) return false;
    if (width != geometry_width || height != geometry_height) {
        ANativeWindow_setBuffersGeometry(window, width, height, WINDOW_FORMAT_RGBA_8888);
        geometry_width = width;
        geometry_height = height;
    }
    ANativeWindow_Buffer buffer{};
    if (ANativeWindow_lock(window, &buffer, nullptr) != 0) return false;
    const int rows = std::min(height, buffer.height);
    const auto bytes = static_cast<std::size_t>(std::min(width, buffer.width)) * 4;
    auto* to = static_cast<std::uint8_t*>(buffer.bits);
    for (int row = 0; row < rows; ++row)
        std::memcpy(to + static_cast<std::size_t>(row) * static_cast<std::size_t>(buffer.stride) * 4, pixels + static_cast<std::size_t>(row) * stride, bytes);
    ANativeWindow_unlockAndPost(window);
    return true;
}

void wake() {
    {
        const std::scoped_lock lock(handed);
        woken = true;
    }
    arrived.notify_all();
}

std::filesystem::path files_directory() {
    return activity && activity->internalDataPath ? std::filesystem::path(activity->internalDataPath) : std::filesystem::path();
}

std::filesystem::path unpack_assets(std::string_view directory) {
    const std::filesystem::path into = files_directory() / directory;
    if (!activity || !activity->assetManager || into.empty()) return {};
    std::error_code error;
    std::filesystem::create_directories(into, error);
    const std::string from(directory);
    AAssetDir* listing = AAssetManager_openDir(activity->assetManager, from.c_str());
    if (!listing) return {};
    while (const char* name = AAssetDir_getNextFileName(listing)) {
        const std::string asset = from + "/" + name;
        AAsset* file = AAssetManager_open(activity->assetManager, asset.c_str(), AASSET_MODE_STREAMING);
        if (!file) continue;
        const auto length = static_cast<std::uintmax_t>(AAsset_getLength64(file));
        const std::filesystem::path to = into / name;
        if (std::filesystem::file_size(to, error) != length || error) {
            error.clear();
            std::ofstream out(to, std::ios::binary | std::ios::trunc);
            std::array<char, 65536> chunk{};
            for (int read = 0; (read = AAsset_read(file, chunk.data(), chunk.size())) > 0;) out.write(chunk.data(), read);
        }
        AAsset_close(file);
    }
    AAssetDir_close(listing);
    return into;
}

void finish() {
    if (activity) ANativeActivity_finish(activity);
}

void log(std::string_view text) {
    const std::string line(text);
    __android_log_write(ANDROID_LOG_INFO, "omui", line.c_str());
}

}  // namespace omui::android::native

// The platform's entry: the activity's callbacks, and the program started once.
extern "C" [[gnu::visibility("default")]] void ANativeActivity_onCreate(ANativeActivity* made, void*, std::size_t) {
    using namespace omui::android::native;
    activity = made;
    made->callbacks->onNativeWindowCreated = on_window_created;
    made->callbacks->onNativeWindowDestroyed = on_window_destroyed;
    made->callbacks->onNativeWindowResized = on_window_resized;
    made->callbacks->onInputQueueCreated = on_input_created;
    made->callbacks->onInputQueueDestroyed = on_input_destroyed;
    made->callbacks->onConfigurationChanged = on_configuration_changed;
    made->callbacks->onWindowFocusChanged = on_focus;
    made->callbacks->onPause = on_pause;
    made->callbacks->onResume = on_resume;
    made->callbacks->onDestroy = on_destroy;
    read_density();
    if (!program_running.exchange(true)) {
        loaded_library = std::format("lib{}.so", library_name(made));
        std::thread(run_program).detach();
    }
}
