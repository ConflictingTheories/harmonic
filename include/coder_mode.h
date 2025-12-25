// coder_mode.h - Live music coding system
#ifndef CODER_MODE_H
#define CODER_MODE_H

#include <vector>
#include <map>
#include <string>
#include <mutex>
#include <cmath>
#include <algorithm>

struct Sample
{
    std::vector<float> data;
    int sample_rate;
    std::string name;

    Sample() : sample_rate(44100) {}
};

struct Loop
{
    int start_frame;
    int end_frame;
    bool active;

    Loop() : start_frame(0), end_frame(0), active(false) {}
};

struct Sequence
{
    struct Event
    {
        int frame_offset;
        int sample_id;
        float volume;
    };

    std::vector<Event> events;
    int length_frames;
    bool playing;
    int current_frame;

    Sequence() : length_frames(0), playing(false), current_frame(0) {}
};

class CoderMode
{
public:
    CoderMode(int sample_rate = 44100);

    // Trigger a sample (for keyboard 1-9)
    void trigger_sample(int id, float volume = 1.0f);

    // Start/stop recording
    void set_recording(bool record);
    bool is_recording() const { return recording; }

    // Loop control
    void set_loop(int start_frame, int end_frame);
    void toggle_loop();
    bool is_looping() const { return current_loop.active; }

    // Sequence control
    void add_sequence_event(int sequence_id, int frame_offset, int sample_id, float volume = 1.0f);
    void play_sequence(int sequence_id);
    void stop_sequence(int sequence_id);

    // Process audio for coder mode
    void process(float *output, size_t frame_count);

    // Get recorded audio
    std::vector<float> get_recording();

    // Load custom sample
    void load_sample(int id, const std::vector<float> &data, const std::string &name);

private:
    int sample_rate;
    std::map<int, Sample> samples;

    struct ActiveSample
    {
        int sample_id;
        size_t position;
        float volume;
    };
    std::vector<ActiveSample> active_samples;

    Loop current_loop;
    std::map<int, Sequence> sequences;

    bool recording;
    int record_start_frame;
    int playback_frame;
    std::vector<float> recorded_buffer;

    std::mutex mutex;

    // Generate basic waveforms
    void generate_sine_sample(int id, float frequency);
    void generate_square_sample(int id, float frequency);
    void generate_saw_sample(int id, float frequency);
};

#endif // CODER_MODE_H