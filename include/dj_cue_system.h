// dj_cue_system.h - Professional DJ cueing with crossfading
#ifndef DJ_CUE_SYSTEM_H
#define DJ_CUE_SYSTEM_H

#include <vector>
#include <mutex>
#include <memory>
#include <cmath>

struct CuePoint {
    std::string track_path;
    uint64_t position_frames;
    float fade_in_seconds;
    float fade_out_seconds;
    bool active;
    
    CuePoint() : position_frames(0), fade_in_seconds(2.0f), fade_out_seconds(2.0f), active(false) {}
};

class DJCueSystem {
public:
    DJCueSystem(int sample_rate);
    
    // Set next track to cue
    void cue_next_track(const std::string& track_path, float fade_in = 2.0f);
    
    // Trigger crossfade to cued track
    void trigger_crossfade();
    
    // Set crossfade duration
    void set_crossfade_duration(float seconds);
    
    // Check if we should start auto-crossfade
    bool should_auto_crossfade(uint64_t current_position, uint64_t track_length);
    
    // Process crossfade mixing
    void process_crossfade(float* current_audio, const float* next_audio, 
                          size_t frame_count, uint64_t& current_pos);
    
    // Get fade envelope for current position
    float get_fade_envelope(uint64_t position, uint64_t track_length, 
                           float fade_in_sec, float fade_out_sec);
    
    // Beat matching helpers
    void set_bpm(float bpm);
    float get_bpm() const;
    
    // Calculate optimal mix point based on BPM
    uint64_t calculate_mix_point(float next_track_bpm);
    
    bool is_crossfading_active() const;
    
    CuePoint get_next_cue() const;
    void clear_cue();
    
    // EQ curves for mixing
    struct EQCurve {
        float bass_gain;    // <250 Hz
        float mid_gain;     // 250-2000 Hz
        float treble_gain;  // >2000 Hz
        
        EQCurve() : bass_gain(1.0f), mid_gain(1.0f), treble_gain(1.0f) {}
    };
    
    void apply_eq_curve(float* audio, size_t frame_count, const EQCurve& eq);
    
    // Hot cue system (instant jump points)
    struct HotCue {
        uint64_t position;
        std::string label;
        bool active;
        
        HotCue() : position(0), active(false) {}
    };
    
    void set_hot_cue(int slot, uint64_t position, const std::string& label = "");
    HotCue get_hot_cue(int slot) const;
    void clear_hot_cue(int slot);
    
private:
    int sample_rate;
    float crossfade_duration;
    float current_bpm;
    
    bool is_crossfading;
    float crossfade_progress;
    uint64_t current_fade_frames;
    
    CuePoint next_cue;
    HotCue hot_cues[8]; // 8 hot cue slots
    
    mutable std::mutex mutex;
};

#endif // DJ_CUE_SYSTEM_H