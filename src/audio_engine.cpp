#include "audio_engine.h"
#include "config.h"
#include <iostream>

AudioEngine::AudioEngine(const Config &cfg)
    : config(cfg), is_playing(false), live_coding_enabled(false), muted(false)
{
    coder = std::make_unique<CoderMode>(config.sample_rate);

    // Decoder config reserved for future advanced decoding support
    (void)ma_decoder_config_init(
        ma_format_f32,
        2, // stereo
        config.sample_rate);

    ma_device_config device_config = ma_device_config_init(ma_device_type_playback);
    device_config.playback.format = ma_format_f32;
    device_config.playback.channels = 2;
    device_config.sampleRate = config.sample_rate;
    device_config.dataCallback = data_callback;
    device_config.pUserData = this;

    if (ma_device_init(NULL, &device_config, &device) != MA_SUCCESS)
    {
        throw std::runtime_error("Failed to initialize audio device");
    }
}

AudioEngine::~AudioEngine()
{
    stop();
    ma_device_uninit(&device);
    if (decoder_initialized)
    {
        ma_decoder_uninit(&decoder);
    }
}

void AudioEngine::start()
{
    if (ma_device_start(&device) != MA_SUCCESS)
    {
        throw std::runtime_error("Failed to start audio device");
    }
    is_playing = true;
}

void AudioEngine::stop()
{
    is_playing = false;
    ma_device_stop(&device);
}

bool AudioEngine::load_track(const std::string &filepath)
{
    std::lock_guard<std::mutex> lock(audio_mutex);

    if (decoder_initialized)
    {
        ma_decoder_uninit(&decoder);
        decoder_initialized = false;
    }

    if (ma_decoder_init_file(filepath.c_str(), NULL, &decoder) != MA_SUCCESS)
    {
        return false;
    }

    decoder_initialized = true;
    current_track = filepath;
    return true;
}

void AudioEngine::enable_live_coding(bool enable)
{
    live_coding_enabled = enable;
}

CoderMode *AudioEngine::get_coder_mode()
{
    return coder.get();
}

std::vector<float> AudioEngine::get_stream_buffer(size_t frames)
{
    std::unique_lock<std::mutex> lock(stream_mutex);

    size_t samples = frames * 2; // stereo

    // Wait for data if queue is empty (with timeout to avoid blocking forever)
    if (stream_queue.empty())
    {
        stream_cv.wait_for(lock, std::chrono::milliseconds(100));
    }

    if (!stream_queue.empty())
    {
        // Get the front buffer from queue
        std::vector<float> result = std::move(stream_queue.front());
        stream_queue.pop_front();

        // Ensure we return exactly the requested number of samples
        if (result.size() < samples)
        {
            result.resize(samples, 0.0f);
        }
        else if (result.size() > samples)
        {
            result.resize(samples);
        }

        return result;
    }

    // Fallback: return zeros if no data available
    return std::vector<float>(samples, 0.0f);
}

void AudioEngine::add_to_stream_queue(const std::vector<float> &buffer)
{
    std::lock_guard<std::mutex> lock(stream_mutex);
    stream_queue.push_back(buffer);
    if (stream_queue.size() > 10)
    { // Limit queue size to prevent memory issues
        stream_queue.pop_front();
    }
    stream_cv.notify_one();
}

FFTData AudioEngine::get_fft_data()
{
    std::lock_guard<std::mutex> lock(fft_mutex);
    return current_fft;
}

std::string AudioEngine::get_current_track() const
{
    return current_track;
}

void AudioEngine::set_muted(bool mute)
{
    muted = mute;
}

void AudioEngine::data_callback(ma_device *device, void *output, const void *input, ma_uint32 frame_count)
{
    AudioEngine *engine = static_cast<AudioEngine *>(device->pUserData);
    float *out = static_cast<float *>(output);

    std::lock_guard<std::mutex> lock(engine->audio_mutex);

    // Coder mode - generate audio procedurally (EXCLUSIVE MODE - do not play decoder)
    if (engine->live_coding_enabled)
    {
        engine->coder->process(out, frame_count);
        // Debug: Verify coder mode is active
        static bool logged = false;
        if (!logged)
        {
            std::cout << "[AUDIO] Coder mode ACTIVE - generating procedural audio" << std::endl;
            logged = true;
        }
    }
    // Normal playback mode (ONLY if not in coder mode)
    else if (engine->decoder_initialized && engine->is_playing)
    {
        ma_uint64 frames_read;
        ma_decoder_read_pcm_frames(&engine->decoder, out, frame_count, &frames_read);

        static bool logged_decoder = false;
        if (!logged_decoder)
        {
            std::cout << "[AUDIO] Decoder mode ACTIVE - playing MP3 files" << std::endl;
            logged_decoder = true;
        }

        if (frames_read < frame_count)
        {
            // End of track reached - loop back to beginning
            ma_decoder_seek_to_pcm_frame(&engine->decoder, 0);
            // Fill remaining frames with zeros or read from start
            for (ma_uint32 i = frames_read * 2; i < frame_count * 2; ++i)
            {
                out[i] = 0.0f;
            }
        }
    }
    // Silence (when not in coder mode and no playback)
    else
    {
        for (ma_uint32 i = 0; i < frame_count * 2; ++i)
        {
            out[i] = 0.0f;
        }

        // Update buffers and return early
        {
            std::vector<float> stream_data(out, out + frame_count * 2);
            engine->add_to_stream_queue(stream_data);
        }
        engine->calculate_fft(out, frame_count);
        return;
    }

    // Update stream buffer for network streaming BEFORE applying mute
    // This ensures clients hear unmuted audio (including coder mode audio)
    {
        std::vector<float> stream_data(out, out + frame_count * 2);
        engine->add_to_stream_queue(stream_data);
    }

    // Calculate FFT data for visualizer BEFORE applying mute
    // This ensures visualizer shows unmuted audio data
    engine->calculate_fft(out, frame_count);

    // Apply mute if enabled (ONLY affects local speaker output)
    if (engine->muted)
    {
        for (ma_uint32 i = 0; i < frame_count * 2; ++i)
        {
            out[i] = 0.0f;
        }
    }
}

void AudioEngine::calculate_fft(float *samples, size_t frame_count)
{
    std::lock_guard<std::mutex> lock(fft_mutex);

    // Convert stereo to mono
    std::vector<float> mono(frame_count);
    for (size_t i = 0; i < frame_count; ++i)
    {
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
