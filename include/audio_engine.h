// audio_engine.h - Core audio processing and streaming
#ifndef AUDIO_ENGINE_H
#define AUDIO_ENGINE_H

#include "miniaudio.h"
#include "miniaudio.h"
#include "fft.h"
#include "coder_mode.h"
#include <vector>
#include <mutex>
#include <atomic>
#include <queue>
#include <memory>
#include <cmath>
#include "config.h"

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
    AudioEngine(const Config& cfg) : config(cfg), is_playing(false), live_coding_enabled(false) {
        coder = std::make_unique<CoderMode>(config.sample_rate);
        
        ma_decoder_config decoder_config = ma_decoder_config_init(
            ma_format_f32,
            2, // stereo
            config.sample_rate
        );
        
        ma_device_config device_config = ma_device_config_init(ma_device_type_playback);
        device_config.playback.format = ma_format_f32;
        device_config.playback.channels = 2;
        device_config.sampleRate = config.sample_rate;
        device_config.dataCallback = data_callback;
        device_config.pUserData = this;
        
        if (ma_device_init(NULL, &device_config, &device) != MA_SUCCESS) {
            throw std::runtime_error("Failed to initialize audio device");
        }
    }
    
    ~AudioEngine() {
        stop();
        ma_device_uninit(&device);
        if (decoder_initialized) {
            ma_decoder_uninit(&decoder);
        }
    }
    
    void start() {
        if (ma_device_start(&device) != MA_SUCCESS) {
            throw std::runtime_error("Failed to start audio device");
        }
        is_playing = true;
    }
    
    void stop() {
        is_playing = false;
        ma_device_stop(&device);
    }
    
    bool load_track(const std::string& filepath) {
        std::lock_guard<std::mutex> lock(audio_mutex);
        
        if (decoder_initialized) {
            ma_decoder_uninit(&decoder);
            decoder_initialized = false;
        }
        
        if (ma_decoder_init_file(filepath.c_str(), NULL, &decoder) != MA_SUCCESS) {
            return false;
        }
        
        decoder_initialized = true;
        current_track = filepath;
        return true;
    }
    
    void enable_live_coding(bool enable) {
        live_coding_enabled = enable;
    }
    
    CoderMode* get_coder_mode() { return coder.get(); }
    
    // Get audio data for streaming
    std::vector<float> get_stream_buffer(size_t frames) {
        std::lock_guard<std::mutex> lock(stream_mutex);
        
        size_t samples = frames * 2; // stereo
        if (stream_buffer.size() < samples) {
            stream_buffer.resize(samples);
        }
        
        return std::vector<float>(stream_buffer.begin(), stream_buffer.begin() + samples);
        // Always return the buffer for streaming (mute is local only)
    }
    
    // Get FFT data for visualizer
    FFTData get_fft_data() {
        std::lock_guard<std::mutex> lock(fft_mutex);
        return current_fft;
    }
    
    bool is_active() const { return is_playing; }
    std::string get_current_track() const { return current_track; }
    void set_muted(bool mute) { muted = mute; }
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
    
    static void data_callback(ma_device* device, void* output, const void* input, ma_uint32 frame_count) {
        AudioEngine* engine = static_cast<AudioEngine*>(device->pUserData);
        float* out = static_cast<float*>(output);
        
        std::lock_guard<std::mutex> lock(engine->audio_mutex);
        
        // Coder mode - generate audio procedurally
        if (engine->live_coding_enabled) {
            engine->coder->process(out, frame_count);
        }
        // Normal playback mode
        else if (engine->decoder_initialized && engine->is_playing) {
            ma_uint64 frames_read;
            ma_decoder_read_pcm_frames(&engine->decoder, out, frame_count, &frames_read);
            
            if (frames_read < frame_count) {
                for (ma_uint32 i = frames_read * 2; i < frame_count * 2; ++i) {
                    out[i] = 0.0f;
                }
            }
        }
        // Silence
        else {
            for (ma_uint32 i = 0; i < frame_count * 2; ++i) {
                out[i] = 0.0f;
            }

            // Update buffers and return early
            {
                std::lock_guard<std::mutex> stream_lock(engine->stream_mutex);
                engine->stream_buffer.assign(out, out + frame_count * 2);
            }
            engine->calculate_fft(out, frame_count);
            return;
        }

        // Apply mute if enabled
        if (engine->muted) {
            for (ma_uint32 i = 0; i < frame_count * 2; ++i) {
                out[i] = 0.0f;
            }
        }
        
        // Update stream buffer for network streaming
        {
            std::lock_guard<std::mutex> stream_lock(engine->stream_mutex);
            engine->stream_buffer.assign(out, out + frame_count * 2);
        }
        
        // Calculate FFT data for visualizer
        engine->calculate_fft(out, frame_count);
    }
    
    void calculate_fft(float* samples, size_t frame_count) {
        std::lock_guard<std::mutex> lock(fft_mutex);
        
        // Convert stereo to mono
        std::vector<float> mono(frame_count);
        for (size_t i = 0; i < frame_count; ++i) {
            mono[i] = (samples[i * 2] + samples[i * 2 + 1]) * 0.5f;
        }
        
        // Perform real FFT analysis
        current_fft.magnitudes = SimpleFFT::analyze(mono.data(), frame_count, 64);
        
        // Calculate frequency bands
        SimpleFFT::calculate_bands(current_fft.magnitudes, 
                                   current_fft.bass, 
                                   current_fft.mid, 
                                   current_fft.treble);
        
        // Calculate overall energy
        current_fft.energy = (current_fft.bass + current_fft.mid + current_fft.treble) / 3.0f;
    }
};

#endif // AUDIO_ENGINE_H