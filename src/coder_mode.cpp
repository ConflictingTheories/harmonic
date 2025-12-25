#include "coder_mode.h"

CoderMode::CoderMode(int sr)
    : sample_rate(sr), recording(false), record_start_frame(0), playback_frame(0)
{
    // Initialize with some basic waveforms
    generate_sine_sample(0, 440.0f);   // A4
    generate_sine_sample(1, 523.25f);  // C5
    generate_sine_sample(2, 659.25f);  // E5
    generate_square_sample(3, 220.0f); // A3 square
    generate_saw_sample(4, 110.0f);    // A2 saw
}

void CoderMode::trigger_sample(int id, float volume)
{
    std::lock_guard<std::mutex> lock(mutex);

    if (samples.find(id) != samples.end())
    {
        ActiveSample active;
        active.sample_id = id;
        active.position = 0;
        active.volume = volume;
        active_samples.push_back(active);
    }
}

void CoderMode::set_recording(bool record)
{
    std::lock_guard<std::mutex> lock(mutex);
    recording = record;

    if (record)
    {
        record_start_frame = playback_frame;
        recorded_buffer.clear();
    }
}

void CoderMode::set_loop(int start_frame, int end_frame)
{
    std::lock_guard<std::mutex> lock(mutex);
    current_loop.start_frame = start_frame;
    current_loop.end_frame = end_frame;
    current_loop.active = true;
}

void CoderMode::toggle_loop()
{
    std::lock_guard<std::mutex> lock(mutex);
    current_loop.active = !current_loop.active;
}

void CoderMode::add_sequence_event(int sequence_id, int frame_offset, int sample_id, float volume)
{
    std::lock_guard<std::mutex> lock(mutex);

    if (sequences.find(sequence_id) == sequences.end())
    {
        sequences[sequence_id] = Sequence();
    }

    Sequence::Event event;
    event.frame_offset = frame_offset;
    event.sample_id = sample_id;
    event.volume = volume;

    sequences[sequence_id].events.push_back(event);

    // Sort by frame offset
    std::sort(sequences[sequence_id].events.begin(),
              sequences[sequence_id].events.end(),
              [](const Sequence::Event &a, const Sequence::Event &b)
              {
                  return a.frame_offset < b.frame_offset;
              });
}

void CoderMode::play_sequence(int sequence_id)
{
    std::lock_guard<std::mutex> lock(mutex);

    if (sequences.find(sequence_id) != sequences.end())
    {
        sequences[sequence_id].playing = true;
        sequences[sequence_id].current_frame = 0;
    }
}

void CoderMode::stop_sequence(int sequence_id)
{
    std::lock_guard<std::mutex> lock(mutex);

    if (sequences.find(sequence_id) != sequences.end())
    {
        sequences[sequence_id].playing = false;
    }
}

void CoderMode::process(float *output, size_t frame_count)
{
    std::lock_guard<std::mutex> lock(mutex);

    // Clear output
    for (size_t i = 0; i < frame_count * 2; ++i)
    {
        output[i] = 0.0f;
    }

    // Process active samples
    for (auto it = active_samples.begin(); it != active_samples.end();)
    {
        const Sample &sample = samples[it->sample_id];

        for (size_t i = 0; i < frame_count; ++i)
        {
            if (it->position < sample.data.size())
            {
                float value = sample.data[it->position] * it->volume;
                output[i * 2] += value;     // Left
                output[i * 2 + 1] += value; // Right
                it->position++;
            }
            else
            {
                break;
            }
        }

        if (it->position >= sample.data.size())
        {
            it = active_samples.erase(it);
        }
        else
        {
            ++it;
        }
    }

    // Process sequences
    for (auto &seq_pair : sequences)
    {
        Sequence &seq = seq_pair.second;
        if (!seq.playing)
            continue;

        for (const auto &event : seq.events)
        {
            if (event.frame_offset >= seq.current_frame &&
                event.frame_offset < seq.current_frame + (int)frame_count)
            {

                // Trigger sample at specific frame
                ActiveSample active;
                active.sample_id = event.sample_id;
                active.position = 0;
                active.volume = event.volume;
                active_samples.push_back(active);
            }
        }

        seq.current_frame += frame_count;

        // Loop sequence
        if (seq.length_frames > 0 && seq.current_frame >= seq.length_frames)
        {
            seq.current_frame = 0;
        }
    }

    // Apply loop
    if (current_loop.active)
    {
        playback_frame += frame_count;

        if (playback_frame >= current_loop.end_frame)
        {
            playback_frame = current_loop.start_frame;
        }
    }

    // Record if active
    if (recording)
    {
        for (size_t i = 0; i < frame_count * 2; ++i)
        {
            recorded_buffer.push_back(output[i]);
        }
    }

    // Normalize output
    for (size_t i = 0; i < frame_count * 2; ++i)
    {
        output[i] = std::max(-1.0f, std::min(1.0f, output[i]));
    }
}

std::vector<float> CoderMode::get_recording()
{
    std::lock_guard<std::mutex> lock(mutex);
    return recorded_buffer;
}

void CoderMode::load_sample(int id, const std::vector<float> &data, const std::string &name)
{
    std::lock_guard<std::mutex> lock(mutex);
    Sample sample;
    sample.data = data;
    sample.name = name;
    sample.sample_rate = sample_rate;
    samples[id] = sample;
}

void CoderMode::generate_sine_sample(int id, float frequency)
{
    Sample sample;
    sample.name = "Sine " + std::to_string((int)frequency) + "Hz";
    sample.sample_rate = sample_rate;

    int duration_frames = sample_rate / 2; // 0.5 seconds
    sample.data.resize(duration_frames);

    for (int i = 0; i < duration_frames; ++i)
    {
        float t = (float)i / sample_rate;
        float value = std::sin(2.0f * M_PI * frequency * t);

        // Apply envelope (ADSR-like)
        float envelope = 1.0f;
        if (i < sample_rate * 0.01f)
        { // Attack
            envelope = (float)i / (sample_rate * 0.01f);
        }
        else if (i > duration_frames - sample_rate * 0.1f)
        { // Release
            envelope = (float)(duration_frames - i) / (sample_rate * 0.1f);
        }

        sample.data[i] = value * envelope * 0.3f;
    }

    samples[id] = sample;
}

void CoderMode::generate_square_sample(int id, float frequency)
{
    Sample sample;
    sample.name = "Square " + std::to_string((int)frequency) + "Hz";
    sample.sample_rate = sample_rate;

    int duration_frames = sample_rate / 2;
    sample.data.resize(duration_frames);

    for (int i = 0; i < duration_frames; ++i)
    {
        float t = (float)i / sample_rate;
        float phase = std::fmod(frequency * t, 1.0f);
        float value = (phase < 0.5f) ? 1.0f : -1.0f;

        float envelope = 1.0f;
        if (i < sample_rate * 0.01f)
        {
            envelope = (float)i / (sample_rate * 0.01f);
        }
        else if (i > duration_frames - sample_rate * 0.1f)
        {
            envelope = (float)(duration_frames - i) / (sample_rate * 0.1f);
        }

        sample.data[i] = value * envelope * 0.2f;
    }

    samples[id] = sample;
}

void CoderMode::generate_saw_sample(int id, float frequency)
{
    Sample sample;
    sample.name = "Saw " + std::to_string((int)frequency) + "Hz";
    sample.sample_rate = sample_rate;

    int duration_frames = sample_rate / 2;
    sample.data.resize(duration_frames);

    for (int i = 0; i < duration_frames; ++i)
    {
        float t = (float)i / sample_rate;
        float phase = std::fmod(frequency * t, 1.0f);
        float value = 2.0f * phase - 1.0f;

        float envelope = 1.0f;
        if (i < sample_rate * 0.01f)
        {
            envelope = (float)i / (sample_rate * 0.01f);
        }
        else if (i > duration_frames - sample_rate * 0.1f)
        {
            envelope = (float)(duration_frames - i) / (sample_rate * 0.1f);
        }

        sample.data[i] = value * envelope * 0.2f;
    }

    samples[id] = sample;
}
