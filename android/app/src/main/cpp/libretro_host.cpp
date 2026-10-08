#include <jni.h>
#include <android/log.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <aaudio/AAudio.h>
#include <dlfcn.h>

#include <atomic>
#include <algorithm>
#include <chrono>
#include <cstdarg>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "libretro.h"

#define LOG_TAG "RPGMP-LIBRETRO"

namespace {

struct CoreApi {
    void* handle = nullptr;
    decltype(&retro_set_environment) set_environment = nullptr;
    decltype(&retro_set_video_refresh) set_video_refresh = nullptr;
    decltype(&retro_set_audio_sample) set_audio_sample = nullptr;
    decltype(&retro_set_audio_sample_batch) set_audio_sample_batch = nullptr;
    decltype(&retro_set_input_poll) set_input_poll = nullptr;
    decltype(&retro_set_input_state) set_input_state = nullptr;
    decltype(&retro_init) init = nullptr;
    decltype(&retro_deinit) deinit = nullptr;
    decltype(&retro_api_version) api_version = nullptr;
    decltype(&retro_get_system_info) get_system_info = nullptr;
    decltype(&retro_get_system_av_info) get_system_av_info = nullptr;
    decltype(&retro_load_game) load_game = nullptr;
    decltype(&retro_unload_game) unload_game = nullptr;
    decltype(&retro_run) run = nullptr;
};

JavaVM* g_vm = nullptr;
jobject g_activity = nullptr;
jmethodID g_on_stopped = nullptr;

CoreApi g_core;
std::thread g_core_thread;
std::atomic<bool> g_running{false};
std::atomic<bool> g_stop_requested{false};
std::atomic<bool> g_shutdown_requested{false};
std::atomic<bool> g_notify_java{true};

std::mutex g_window_mutex;
ANativeWindow* g_window = nullptr;

std::atomic<uint32_t> g_buttons{0};
std::atomic<int16_t> g_axes[6];

std::string g_system_dir;
std::string g_save_dir;

AAudioStream* g_audio_stream = nullptr;
retro_audio_callback g_audio_driver{};
retro_frame_time_callback g_frame_time{};
double g_fps = 60.0;
constexpr uint64_t AUDIO_RING_FRAMES = 32768;
std::vector<int16_t> g_audio_ring(AUDIO_RING_FRAMES * 2, 0);
std::atomic<uint64_t> g_audio_read_pos{0};
std::atomic<uint64_t> g_audio_write_pos{0};
std::atomic<bool> g_audio_reopen_requested{false};
std::atomic<int> g_audio_sample_rate{48000};

template <typename T>
bool load_symbol(T& out, const char* name) {
    out = reinterpret_cast<T>(dlsym(g_core.handle, name));
    if (!out) {
        __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "Missing core symbol: %s", name);
        return false;
    }
    return true;
}

void core_log(enum retro_log_level level, const char* fmt, ...) {
    int prio = ANDROID_LOG_DEBUG;
    if (level == RETRO_LOG_INFO) prio = ANDROID_LOG_INFO;
    else if (level == RETRO_LOG_WARN) prio = ANDROID_LOG_WARN;
    else if (level == RETRO_LOG_ERROR) prio = ANDROID_LOG_ERROR;
    va_list ap;
    va_start(ap, fmt);
    __android_log_vprint(prio, "EasyRPG-Core", fmt, ap);
    va_end(ap);
}

bool environment_cb(unsigned cmd, void* data) {
    switch (cmd) {
        case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: {
            if (!data) return false;
            auto fmt = *reinterpret_cast<retro_pixel_format*>(data);
            return fmt == RETRO_PIXEL_FORMAT_XRGB8888;
        }
        case RETRO_ENVIRONMENT_GET_LOG_INTERFACE: {
            if (!data) return false;
            reinterpret_cast<retro_log_callback*>(data)->log = core_log;
            return true;
        }
        case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:
        case RETRO_ENVIRONMENT_SET_VARIABLES:
        case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
        case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO:
        case RETRO_ENVIRONMENT_SET_SUPPORT_ACHIEVEMENTS:
        case RETRO_ENVIRONMENT_SET_GEOMETRY:
        case RETRO_ENVIRONMENT_SET_MINIMUM_AUDIO_LATENCY:
            return true;
        case RETRO_ENVIRONMENT_GET_VARIABLE: {
            if (!data) return false;
            auto* var = reinterpret_cast<retro_variable*>(data);
            if (var->key && std::strcmp(var->key, "easyrpg_debug_mode") == 0) {
                var->value = "Disabled";
                return true;
            }
            var->value = nullptr;
            return false;
        }
        case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
            if (data) *reinterpret_cast<bool*>(data) = false;
            return true;
        case RETRO_ENVIRONMENT_SET_AUDIO_CALLBACK:
            if (!data) return false;
            g_audio_driver = *reinterpret_cast<retro_audio_callback*>(data);
            return true;
        case RETRO_ENVIRONMENT_SET_FRAME_TIME_CALLBACK:
            if (!data) return false;
            g_frame_time = *reinterpret_cast<retro_frame_time_callback*>(data);
            return true;
        case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
            if (!data) return false;
            *reinterpret_cast<const char**>(data) = g_system_dir.c_str();
            return true;
        case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
            if (!data) return false;
            *reinterpret_cast<const char**>(data) = g_save_dir.c_str();
            return true;
#ifdef RETRO_ENVIRONMENT_GET_CORE_ASSETS_DIRECTORY
        case RETRO_ENVIRONMENT_GET_CORE_ASSETS_DIRECTORY:
            if (!data) return false;
            *reinterpret_cast<const char**>(data) = g_system_dir.c_str();
            return true;
#endif
        case RETRO_ENVIRONMENT_GET_CAN_DUPE:
            if (data) *reinterpret_cast<bool*>(data) = true;
            return true;
#ifdef RETRO_ENVIRONMENT_GET_LANGUAGE
        case RETRO_ENVIRONMENT_GET_LANGUAGE:
            if (data) *reinterpret_cast<unsigned*>(data) = RETRO_LANGUAGE_KOREAN;
            return true;
#endif
#ifdef RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION
        case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION:
            if (data) *reinterpret_cast<unsigned*>(data) = 0;
            return true;
#endif
#ifdef RETRO_ENVIRONMENT_GET_INPUT_BITMASKS
        case RETRO_ENVIRONMENT_GET_INPUT_BITMASKS:
            return true;
#endif
#ifdef RETRO_ENVIRONMENT_GET_AUDIO_VIDEO_ENABLE
        case RETRO_ENVIRONMENT_GET_AUDIO_VIDEO_ENABLE:
            if (data) *reinterpret_cast<int*>(data) = 3;
            return true;
#endif
        case RETRO_ENVIRONMENT_SHUTDOWN:
            g_shutdown_requested.store(true);
            return true;
        case RETRO_ENVIRONMENT_SET_MESSAGE: {
            auto* msg = reinterpret_cast<retro_message*>(data);
            if (msg && msg->msg) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "%s", msg->msg);
            return true;
        }
        default:
            return false;
    }
}

void input_poll_cb() {}

int16_t input_state_cb(unsigned port, unsigned device, unsigned index, unsigned id) {
    if (port != 0) return 0;
    unsigned base = device & RETRO_DEVICE_MASK;
    if (base == RETRO_DEVICE_JOYPAD) {
#ifdef RETRO_DEVICE_ID_JOYPAD_MASK
        if (id == RETRO_DEVICE_ID_JOYPAD_MASK) {
            return static_cast<int16_t>(g_buttons.load() & 0xffffu);
        }
#endif
        if (id < 16) return (g_buttons.load() & (1u << id)) ? 1 : 0;
        return 0;
    }
    if (base == RETRO_DEVICE_ANALOG) {
        if (index == RETRO_DEVICE_INDEX_ANALOG_LEFT) {
            if (id == RETRO_DEVICE_ID_ANALOG_X) return g_axes[0].load();
            if (id == RETRO_DEVICE_ID_ANALOG_Y) return g_axes[1].load();
        } else if (index == RETRO_DEVICE_INDEX_ANALOG_RIGHT) {
            if (id == RETRO_DEVICE_ID_ANALOG_X) return g_axes[2].load();
            if (id == RETRO_DEVICE_ID_ANALOG_Y) return g_axes[3].load();
        } else if (index == RETRO_DEVICE_INDEX_ANALOG_BUTTON) {
            if (id == RETRO_DEVICE_ID_JOYPAD_L2) return g_axes[4].load();
            if (id == RETRO_DEVICE_ID_JOYPAD_R2) return g_axes[5].load();
        }
    }
    return 0;
}

uint32_t xrgb_to_rgba(uint32_t p) {
    return 0xff000000u | ((p & 0x000000ffu) << 16) | (p & 0x0000ff00u) |
           ((p & 0x00ff0000u) >> 16);
}

void video_cb(const void* data, unsigned width, unsigned height, size_t pitch) {
    if (!data || width == 0 || height == 0) return;
    std::lock_guard<std::mutex> lock(g_window_mutex);
    if (!g_window) return;

    ANativeWindow_Buffer out{};
    if (ANativeWindow_lock(g_window, &out, nullptr) != 0) return;
    auto* dst = reinterpret_cast<uint32_t*>(out.bits);
    const int dw = out.width;
    const int dh = out.height;
    const int dstride = out.stride;

    for (int y = 0; y < dh; ++y) {
        uint32_t* row = dst + y * dstride;
        for (int x = 0; x < dw; ++x) row[x] = 0xff000000u;
    }

    const double sx = static_cast<double>(dw) / static_cast<double>(width);
    const double sy = static_cast<double>(dh) / static_cast<double>(height);
    const double scale = sx < sy ? sx : sy;
    int rw = static_cast<int>(width * scale);
    int rh = static_cast<int>(height * scale);
    if (rw < 1) rw = 1;
    if (rh < 1) rh = 1;
    const int ox = (dw - rw) / 2;
    const int oy = (dh - rh) / 2;

    for (int y = 0; y < rh; ++y) {
        unsigned src_y = static_cast<unsigned>((static_cast<uint64_t>(y) * height) / rh);
        const auto* src_row = reinterpret_cast<const uint32_t*>(
                reinterpret_cast<const uint8_t*>(data) + src_y * pitch);
        uint32_t* dst_row = dst + (oy + y) * dstride + ox;
        for (int x = 0; x < rw; ++x) {
            unsigned src_x = static_cast<unsigned>((static_cast<uint64_t>(x) * width) / rw);
            dst_row[x] = xrgb_to_rgba(src_row[src_x]);
        }
    }
    ANativeWindow_unlockAndPost(g_window);
}

size_t enqueue_audio(const int16_t* data, size_t frames) {
    if (!data || frames == 0) return 0;
    uint64_t read = g_audio_read_pos.load(std::memory_order_acquire);
    uint64_t write = g_audio_write_pos.load(std::memory_order_relaxed);
    uint64_t used = write >= read ? write - read : 0;
    if (used > AUDIO_RING_FRAMES) used = AUDIO_RING_FRAMES;
    uint64_t free_frames = AUDIO_RING_FRAMES - used;
    size_t accepted = static_cast<size_t>(std::min<uint64_t>(frames, free_frames));
    for (size_t i = 0; i < accepted; ++i) {
        uint64_t slot = (write + i) % AUDIO_RING_FRAMES;
        g_audio_ring[slot * 2] = data[i * 2];
        g_audio_ring[slot * 2 + 1] = data[i * 2 + 1];
    }
    g_audio_write_pos.store(write + accepted, std::memory_order_release);
    return accepted;
}

void audio_sample_cb(int16_t left, int16_t right) {
    int16_t pair[2] = {left, right};
    enqueue_audio(pair, 1);
}

size_t audio_batch_cb(const int16_t* data, size_t frames) {
    enqueue_audio(data, frames);
    // The core should never stall on the Android device clock. If the ring is
    // temporarily full we drop only the newest PCM frames and keep the game loop
    // running; normal operation has ~680 ms of headroom at 48 kHz.
    return frames;
}

aaudio_data_callback_result_t audio_data_callback(AAudioStream*, void*,
                                                  void* audio_data, int32_t num_frames) {
    auto* out = static_cast<int16_t*>(audio_data);
    if (!out || num_frames <= 0) return AAUDIO_CALLBACK_RESULT_CONTINUE;
    uint64_t read = g_audio_read_pos.load(std::memory_order_relaxed);
    uint64_t write = g_audio_write_pos.load(std::memory_order_acquire);
    uint64_t available = write >= read ? write - read : 0;
    if (available > AUDIO_RING_FRAMES) available = AUDIO_RING_FRAMES;
    int32_t consume = static_cast<int32_t>(
            std::min<uint64_t>(available, static_cast<uint64_t>(num_frames)));
    for (int32_t i = 0; i < consume; ++i) {
        uint64_t slot = (read + static_cast<uint64_t>(i)) % AUDIO_RING_FRAMES;
        out[i * 2] = g_audio_ring[slot * 2];
        out[i * 2 + 1] = g_audio_ring[slot * 2 + 1];
    }
    if (consume < num_frames) {
        std::memset(out + consume * 2, 0,
                static_cast<size_t>(num_frames - consume) * 2 * sizeof(int16_t));
    }
    g_audio_read_pos.store(read + static_cast<uint64_t>(consume), std::memory_order_release);
    return AAUDIO_CALLBACK_RESULT_CONTINUE;
}

void audio_error_callback(AAudioStream*, void*, aaudio_result_t error) {
    __android_log_print(ANDROID_LOG_WARN, LOG_TAG,
            "AAudio error callback: %s", AAudio_convertResultToText(error));
    if (error == AAUDIO_ERROR_DISCONNECTED) g_audio_reopen_requested.store(true);
}

bool open_audio(int sample_rate) {
    g_audio_sample_rate.store(sample_rate > 0 ? sample_rate : 48000);
    g_audio_read_pos.store(0, std::memory_order_release);
    g_audio_write_pos.store(0, std::memory_order_release);
    std::fill(g_audio_ring.begin(), g_audio_ring.end(), 0);
    AAudioStreamBuilder* builder = nullptr;
    if (AAudio_createStreamBuilder(&builder) != AAUDIO_OK || !builder) return false;
    AAudioStreamBuilder_setDirection(builder, AAUDIO_DIRECTION_OUTPUT);
    AAudioStreamBuilder_setPerformanceMode(builder, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
    AAudioStreamBuilder_setSharingMode(builder, AAUDIO_SHARING_MODE_SHARED);
    AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_I16);
    AAudioStreamBuilder_setChannelCount(builder, 2);
    AAudioStreamBuilder_setSampleRate(builder, g_audio_sample_rate.load());
    AAudioStreamBuilder_setDataCallback(builder, audio_data_callback, nullptr);
    AAudioStreamBuilder_setErrorCallback(builder, audio_error_callback, nullptr);
    aaudio_result_t result = AAudioStreamBuilder_openStream(builder, &g_audio_stream);
    AAudioStreamBuilder_delete(builder);
    if (result != AAUDIO_OK || !g_audio_stream) {
        g_audio_stream = nullptr;
        return false;
    }
    int32_t burst = AAudioStream_getFramesPerBurst(g_audio_stream);
    if (burst > 0) {
        int32_t target = burst * 4;
        aaudio_result_t buffer_result = AAudioStream_setBufferSizeInFrames(g_audio_stream, target);
        if (buffer_result < 0) {
            __android_log_print(ANDROID_LOG_WARN, LOG_TAG,
                    "AAudio buffer size request failed: %s",
                    AAudio_convertResultToText(buffer_result));
        }
    }
    result = AAudioStream_requestStart(g_audio_stream);
    if (result != AAUDIO_OK) {
        AAudioStream_close(g_audio_stream);
        g_audio_stream = nullptr;
        return false;
    }
    g_audio_reopen_requested.store(false);
    return true;
}

void close_audio() {
    if (!g_audio_stream) return;
    AAudioStream_requestStop(g_audio_stream);
    AAudioStream_close(g_audio_stream);
    g_audio_stream = nullptr;
}

void recover_audio_if_needed() {
    if (!g_audio_reopen_requested.exchange(false)) return;
    __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "Reopening disconnected AAudio stream");
    close_audio();
    if (!open_audio(g_audio_sample_rate.load())) {
        __android_log_print(ANDROID_LOG_ERROR, LOG_TAG,
                "AAudio reopen failed; will retry on next frame");
        g_audio_reopen_requested.store(true);
    }
}

void notify_java_stopped(const std::string& error) {
    if (!g_notify_java.load() || !g_vm || !g_activity || !g_on_stopped) return;
    JNIEnv* env = nullptr;
    bool attached = false;
    if (g_vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
        if (g_vm->AttachCurrentThread(&env, nullptr) != JNI_OK) return;
        attached = true;
    }
    jstring msg = env->NewStringUTF(error.c_str());
    env->CallVoidMethod(g_activity, g_on_stopped, msg);
    env->DeleteLocalRef(msg);
    if (attached) g_vm->DetachCurrentThread();
}

void cleanup_core(bool loaded_game) {
    if (g_audio_driver.set_state) g_audio_driver.set_state(false);
    close_audio();
    if (g_core.handle) {
        if (loaded_game && g_core.unload_game) g_core.unload_game();
        if (g_core.deinit) g_core.deinit();
        dlclose(g_core.handle);
    }
    g_core = CoreApi{};
    g_audio_driver = retro_audio_callback{};
    g_frame_time = retro_frame_time_callback{};
}

void core_main(std::string core_path, std::string game_path) {
    std::string error;
    bool loaded_game = false;
    retro_game_info game{};
    retro_system_av_info av{};
    int sample_rate = 48000;
    g_core.handle = dlopen(core_path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!g_core.handle) {
        const char* dlerr = dlerror();
        error = std::string("EasyRPG core load failed: ") + (dlerr ? dlerr : "unknown");
        goto done;
    }

    if (!load_symbol(g_core.set_environment, "retro_set_environment") ||
        !load_symbol(g_core.set_video_refresh, "retro_set_video_refresh") ||
        !load_symbol(g_core.set_audio_sample, "retro_set_audio_sample") ||
        !load_symbol(g_core.set_audio_sample_batch, "retro_set_audio_sample_batch") ||
        !load_symbol(g_core.set_input_poll, "retro_set_input_poll") ||
        !load_symbol(g_core.set_input_state, "retro_set_input_state") ||
        !load_symbol(g_core.init, "retro_init") ||
        !load_symbol(g_core.deinit, "retro_deinit") ||
        !load_symbol(g_core.api_version, "retro_api_version") ||
        !load_symbol(g_core.get_system_info, "retro_get_system_info") ||
        !load_symbol(g_core.get_system_av_info, "retro_get_system_av_info") ||
        !load_symbol(g_core.load_game, "retro_load_game") ||
        !load_symbol(g_core.unload_game, "retro_unload_game") ||
        !load_symbol(g_core.run, "retro_run")) {
        error = "EasyRPG core ABI incomplete";
        goto done;
    }

    if (g_core.api_version() != RETRO_API_VERSION) {
        error = "EasyRPG core libretro API mismatch";
        goto done;
    }

    g_core.set_environment(environment_cb);
    g_core.set_video_refresh(video_cb);
    g_core.set_audio_sample(audio_sample_cb);
    g_core.set_audio_sample_batch(audio_batch_cb);
    g_core.set_input_poll(input_poll_cb);
    g_core.set_input_state(input_state_cb);
    g_core.init();

    game.path = game_path.c_str();
    if (!g_core.load_game(&game)) {
        error = "EasyRPG core rejected the selected game";
        goto done;
    }
    loaded_game = true;

    g_core.get_system_av_info(&av);
    if (av.timing.fps > 1.0) g_fps = av.timing.fps;
    sample_rate = av.timing.sample_rate > 1.0
            ? static_cast<int>(av.timing.sample_rate) : 48000;
    if (!open_audio(sample_rate)) {
        __android_log_print(ANDROID_LOG_WARN, LOG_TAG, "AAudio open failed; continuing without audio");
    }
    if (g_audio_driver.set_state) g_audio_driver.set_state(true);

    {
        using clock = std::chrono::steady_clock;
        auto frame = std::chrono::duration<double>(1.0 / (g_fps > 1.0 ? g_fps : 60.0));
        auto next = clock::now();
        retro_usec_t frame_us = static_cast<retro_usec_t>(1000000.0 / (g_fps > 1.0 ? g_fps : 60.0));
        while (!g_stop_requested.load() && !g_shutdown_requested.load()) {
            next += std::chrono::duration_cast<clock::duration>(frame);
            if (g_frame_time.callback) g_frame_time.callback(frame_us);
            g_core.run();
            if (g_audio_driver.callback) g_audio_driver.callback();
            recover_audio_if_needed();
            std::this_thread::sleep_until(next);
            auto now = clock::now();
            if (now - next > std::chrono::milliseconds(250)) next = now;
        }
    }

done:
    cleanup_core(loaded_game);
    g_running.store(false);
    notify_java_stopped(error);
}

void set_window(JNIEnv* env, jobject surface) {
    std::lock_guard<std::mutex> lock(g_window_mutex);
    if (g_window) {
        ANativeWindow_release(g_window);
        g_window = nullptr;
    }
    if (surface) {
        g_window = ANativeWindow_fromSurface(env, surface);
        if (g_window) ANativeWindow_setBuffersGeometry(g_window, 0, 0, WINDOW_FORMAT_RGBA_8888);
    }
}

std::string jstr(JNIEnv* env, jstring value) {
    if (!value) return {};
    const char* p = env->GetStringUTFChars(value, nullptr);
    std::string out = p ? p : "";
    if (p) env->ReleaseStringUTFChars(value, p);
    return out;
}

} // namespace

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void*) {
    g_vm = vm;
    return JNI_VERSION_1_6;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_dj5927_rpgmakerplayer_LibretroEasyRpgActivity_nativeStart(
        JNIEnv* env, jobject thiz, jstring core_path, jstring game_path,
        jstring save_dir, jstring system_dir, jobject surface) {
    if (g_running.load()) return JNI_TRUE;
    if (g_core_thread.joinable()) g_core_thread.join();

    if (g_activity) {
        env->DeleteGlobalRef(g_activity);
        g_activity = nullptr;
    }
    g_activity = env->NewGlobalRef(thiz);
    jclass cls = env->GetObjectClass(thiz);
    g_on_stopped = env->GetMethodID(cls, "onNativeCoreStopped", "(Ljava/lang/String;)V");
    env->DeleteLocalRef(cls);

    g_system_dir = jstr(env, system_dir);
    g_save_dir = jstr(env, save_dir);
    g_stop_requested.store(false);
    g_shutdown_requested.store(false);
    g_notify_java.store(true);
    g_buttons.store(0);
    for (auto& axis : g_axes) axis.store(0);
    set_window(env, surface);

    std::string core = jstr(env, core_path);
    std::string game = jstr(env, game_path);
    g_running.store(true);
    g_core_thread = std::thread(core_main, std::move(core), std::move(game));
    return JNI_TRUE;
}

extern "C" JNIEXPORT void JNICALL
Java_com_dj5927_rpgmakerplayer_LibretroEasyRpgActivity_nativeSetSurface(
        JNIEnv* env, jobject, jobject surface) {
    set_window(env, surface);
}

extern "C" JNIEXPORT void JNICALL
Java_com_dj5927_rpgmakerplayer_LibretroEasyRpgActivity_nativeSetButton(
        JNIEnv*, jobject, jint id, jboolean down) {
    if (id < 0 || id >= 16) return;
    uint32_t bit = 1u << static_cast<unsigned>(id);
    if (down) g_buttons.fetch_or(bit);
    else g_buttons.fetch_and(~bit);
}

extern "C" JNIEXPORT void JNICALL
Java_com_dj5927_rpgmakerplayer_LibretroEasyRpgActivity_nativeSetAxis(
        JNIEnv*, jobject, jint axis, jint value) {
    if (axis < 0 || axis >= 6) return;
    if (value < -32768) value = -32768;
    if (value > 32767) value = 32767;
    g_axes[axis].store(static_cast<int16_t>(value));
}

extern "C" JNIEXPORT void JNICALL
Java_com_dj5927_rpgmakerplayer_LibretroEasyRpgActivity_nativeStop(
        JNIEnv* env, jobject) {
    g_notify_java.store(false);
    g_stop_requested.store(true);
    if (g_core_thread.joinable()) g_core_thread.join();
    set_window(env, nullptr);
    if (g_activity) {
        env->DeleteGlobalRef(g_activity);
        g_activity = nullptr;
    }
    g_on_stopped = nullptr;
}

