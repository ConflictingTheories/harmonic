// audio_engine.h - Core audio processing and streaming
#ifndef AUDIO_ENGINE_H
#define AUDIO_ENGINE_H

#include "miniaudio.h"
#include "fft.h"
#include "coder_mode.h"
#include "config.h"
#include <vector>
#include <mutex>
#include <atomic>
#include <memory>
#include <string>

struct AudioFrame {
    std::vector<float> samples;
    size_t sample_rate;
    size_t channels;
};

struct FFTData {
    std::vector<float> magnitudes;
    float bass;
    float mid;
    float treble;
    float energy;
};

class AudioEngine {
public:
    AudioEngine(const Config& cfg);
    ~AudioEngine();
    
    void start();
    void stop();
    bool load_track(const std::string& filepath);
    void enable_live_coding(bool enable);
    
    CoderMode* get_coder_mode();
    std::vector<float> get_stream_buffer(size_t frames);
    FFTData get_fft_data();
    
    bool is_active() const { return is_playing; }
    std::string get_current_track() const;
    void set_muted(bool mute);
    bool is_muted() const { return muted; }
    
private:
    Config config;
    ma_device device;
    ma_decoder decoder;
    bool decoder_initialized = false;
    
    std::atomic<bool> is_playing;
    std::atomic<bool> live_coding_enabled;
    std::atomic<bool> muted;
    
    std::unique_ptr<CoderMode> coder;
    
    std::mutex audio_mutex;
    std::mutex stream_mutex;
    std::mutex fft_mutex;
    
    std::string current_track;
    std::vector<float> stream_buffer;
    FFTData current_fft;
    
    static void data_callback(ma_device* device, void* output, const void* input, ma_uint32 frame_count);
    void calculate_fft(float* samples, size_t frame_count);
};

#endif // AUDIO_ENGINE_H
