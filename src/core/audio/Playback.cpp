#include "core/audio/Playback.hpp"

#include "core/Diagnostic.hpp"
#include "core/render/Mixer.hpp"

#ifdef RENDEROJN_EXTERNAL_DEPS
#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>

#include <algorithm>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <mutex>
#endif

namespace renderojn::audio {

#ifdef RENDEROJN_EXTERNAL_DEPS
namespace {

constexpr std::size_t kMaximumQueuedValues = format::kSampleRate * 2U * 2U;

struct PlaybackQueue {
    std::mutex mutex;
    std::condition_variable changed;
    std::deque<float> values;
    bool produced{};
};

void actual_device_callback(ma_device* device, void* output, const void*, ma_uint32 frame_count) {
    auto& queue = *static_cast<PlaybackQueue*>(device->pUserData);
    auto* destination = static_cast<float*>(output);
    std::unique_lock<std::mutex> lock(queue.mutex);
    const auto count = static_cast<std::size_t>(frame_count) * 2U;
    // Windows headers define min as a macro; parentheses keep this portable.
    const auto available = (std::min)(count, queue.values.size());
    for (std::size_t index = 0; index < available; ++index) {
        destination[index] = queue.values.front();
        queue.values.pop_front();
    }
    std::fill(destination + static_cast<std::ptrdiff_t>(available), destination + static_cast<std::ptrdiff_t>(count), 0.0F);
    lock.unlock();
    queue.changed.notify_all();
}

} // namespace
#endif

void play_realtime(const format::Chart& chart, const std::vector<format::DecodedSample>& samples, Diagnostics& diagnostics) {
#ifdef RENDEROJN_EXTERNAL_DEPS
    PlaybackQueue queue;
    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    config.playback.format = ma_format_f32;
    config.playback.channels = 2;
    config.sampleRate = format::kSampleRate;
    config.dataCallback = actual_device_callback;
    config.pUserData = &queue;
    ma_device device{};
    if (ma_device_init(nullptr, &config, &device) != MA_SUCCESS) {
        throw Error(ExitCode::Runtime, "Unable to initialize miniaudio playback device");
    }
    const auto uninitialize = [&]() { ma_device_uninit(&device); };
    try {
        if (ma_device_start(&device) != MA_SUCCESS) throw Error(ExitCode::Runtime, "Unable to start miniaudio playback device");
        render::mix_chart(chart, samples, render::SchedulingMode::Realtime, false,
                          [&](const float* frames, std::size_t frame_count) {
                              std::unique_lock<std::mutex> lock(queue.mutex);
                              queue.changed.wait(lock, [&]() { return queue.values.size() < kMaximumQueuedValues; });
                              queue.values.insert(queue.values.end(), frames, frames + frame_count * 2U);
                              lock.unlock();
                              queue.changed.notify_all();
                          }, diagnostics);
        std::unique_lock<std::mutex> lock(queue.mutex);
        queue.produced = true;
        queue.changed.wait(lock, [&]() { return queue.values.empty(); });
        lock.unlock();
        ma_device_stop(&device);
        uninitialize();
    } catch (...) { ma_device_stop(&device); uninitialize(); throw; }
#else
    static_cast<void>(chart); static_cast<void>(samples); static_cast<void>(diagnostics);
    throw Error(ExitCode::Runtime, "This RenderOJN build has no miniaudio support; configure with declared vcpkg dependencies.");
#endif
}

} // namespace renderojn::audio
