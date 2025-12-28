#include "audio_engine.h"
#include "config.h"
#include <iostream>

AudioEngine::AudioEngine(const Config &cfg)
    : config(cfg), is_playing(false), live_coding_enabled(false), muted(false), track_ended(false)
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
    track_ended = false;  // Reset track ended flag when loading new track
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

    size_t required_samples = frames * 2; // samples = frames * channels (2 for stereo)

    // Append data from stream_queue to stream_buffer until we have enough
    while (stream_buffer.size() < required_samples)
    {
        // Wait for data if queue is empty
        if (stream_queue.empty())
        {
            // Wait with a timeout. If timeout, we return what we have (potentially zeros).
            // This might still lead to underruns but prevents indefinite blocking.
            if (stream_cv.wait_for(lock, std::chrono::milliseconds(100)) == std::cv_status::timeout) {
                // If timed out and still no data, break and return current buffer (might be empty/partial)
                break;
            }
            if (stream_queue.empty()) { // Check again after wait
                break;
            }
        }
        
        // Take data from the front of the queue
        std::vector<float> chunk = std::move(stream_queue.front());
        stream_queue.pop_front();
        
        stream_buffer.insert(stream_buffer.end(), chunk.begin(), chunk.end());
    }

    // Extract the required number of samples
    std::vector<float> result(required_samples);
    if (stream_buffer.size() >= required_samples)
    {
        std::copy_n(stream_buffer.begin(), required_samples, result.begin());
        stream_buffer.erase(stream_buffer.begin(), stream_buffer.begin() + required_samples);
    }
    else
    {
        // If we still don't have enough after consuming all available queue data
        // (e.g., if wait_for timed out), fill the rest with zeros.
        std::copy(stream_buffer.begin(), stream_buffer.end(), result.begin());
        std::fill(result.begin() + stream_buffer.size(), result.end(), 0.0f);
        stream_buffer.clear(); // Clear the buffer as it's been consumed
    }

    return result;
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
            // End of track reached - signal track ended for auto-advance in all non-CODER modes
            engine->track_ended = true;
            // Fill remaining frames with zeros
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
