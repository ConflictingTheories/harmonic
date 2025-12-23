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
    DJCueSystem(int sample_rate) : sample_rate(sample_rate), 
                                    crossfade_duration(3.0f),
                                    is_crossfading(false),
                                    crossfade_progress(0.0f) {}
    
    // Set next track to cue
    void cue_next_track(const std::string& track_path, float fade_in = 2.0f) {
        std::lock_guard<std::mutex> lock(mutex);
        
        next_cue.track_path = track_path;
        next_cue.position_frames = 0;
        next_cue.fade_in_seconds = fade_in;
        next_cue.active = true;
    }
    
    // Trigger crossfade to cued track
    void trigger_crossfade() {
        std::lock_guard<std::mutex> lock(mutex);
        
        if (next_cue.active) {
            is_crossfading = true;
            crossfade_progress = 0.0f;
            current_fade_frames = (uint64_t)(crossfade_duration * sample_rate);
        }
    }
    
    // Set crossfade duration
    void set_crossfade_duration(float seconds) {
        std::lock_guard<std::mutex> lock(mutex);
        crossfade_duration = seconds;
    }
    
    // Check if we should start auto-crossfade
    bool should_auto_crossfade(uint64_t current_position, uint64_t track_length) {
        std::lock_guard<std::mutex> lock(mutex);
        
        if (!next_cue.active || is_crossfading) return false;
        
        // Start crossfade N seconds before track ends
        uint64_t crossfade_start = track_length - (uint64_t)(crossfade_duration * sample_rate);
        
        return current_position >= crossfade_start;
    }
    
    // Process crossfade mixing
    void process_crossfade(float* current_audio, const float* next_audio, 
                          size_t frame_count, uint64_t& current_pos) {
        std::lock_guard<std::mutex> lock(mutex);
        
        if (!is_crossfading) return;
        
        for (size_t i = 0; i < frame_count; ++i) {
            float progress = crossfade_progress / current_fade_frames;
            progress = std::min(1.0f, std::max(0.0f, progress));
            
            // Apply crossfade curve (equal power)
            float fade_out = std::cos(progress * M_PI * 0.5f);
            float fade_in = std::sin(progress * M_PI * 0.5f);
            
            // Mix left and right channels
            current_audio[i * 2] = current_audio[i * 2] * fade_out + next_audio[i * 2] * fade_in;
            current_audio[i * 2 + 1] = current_audio[i * 2 + 1] * fade_out + next_audio[i * 2 + 1] * fade_in;
            
            crossfade_progress++;
            
            // Check if crossfade complete
            if (crossfade_progress >= current_fade_frames) {
                is_crossfading = false;
                next_cue.active = false;
                // Signal track switch needed
                current_pos = 0; // Will be handled by caller
                break;
            }
        }
    }
    
    // Get fade envelope for current position
    float get_fade_envelope(uint64_t position, uint64_t track_length, 
                           float fade_in_sec, float fade_out_sec) {
        uint64_t fade_in_frames = (uint64_t)(fade_in_sec * sample_rate);
        uint64_t fade_out_frames = (uint64_t)(fade_out_sec * sample_rate);
        
        float envelope = 1.0f;
        
        // Fade in
        if (position < fade_in_frames) {
            envelope = (float)position / fade_in_frames;
        }
        // Fade out
        else if (position > track_length - fade_out_frames) {
            uint64_t remaining = track_length - position;
            envelope = (float)remaining / fade_out_frames;
        }
        
        return envelope;
    }
    
    // Beat matching helpers
    void set_bpm(float bpm) {
        std::lock_guard<std::mutex> lock(mutex);
        current_bpm = bpm;
    }
    
    float get_bpm() const {
        return current_bpm;
    }
    
    // Calculate optimal mix point based on BPM
    uint64_t calculate_mix_point(float next_track_bpm) {
        std::lock_guard<std::mutex> lock(mutex);
        
        if (current_bpm <= 0.0f || next_track_bpm <= 0.0f) {
            return 0;
        }
        
        // Find a phrase boundary (typically 16 or 32 beats)
        float beat_duration = 60.0f / current_bpm;
        float phrase_duration = beat_duration * 32.0f;
        
        uint64_t phrase_frames = (uint64_t)(phrase_duration * sample_rate);
        
        return phrase_frames;
    }
    
    bool is_crossfading_active() const {
        return is_crossfading;
    }
    
    CuePoint get_next_cue() const {
        std::lock_guard<std::mutex> lock(mutex);
        return next_cue;
    }
    
    void clear_cue() {
        std::lock_guard<std::mutex> lock(mutex);
        next_cue.active = false;
    }
    
    // EQ curves for mixing
    struct EQCurve {
        float bass_gain;    // <250 Hz
        float mid_gain;     // 250-2000 Hz
        float treble_gain;  // >2000 Hz
        
        EQCurve() : bass_gain(1.0f), mid_gain(1.0f), treble_gain(1.0f) {}
    };
    
    void apply_eq_curve(float* audio, size_t frame_count, const EQCurve& eq) {
        // Simplified EQ - in production would use proper filters
        for (size_t i = 0; i < frame_count * 2; ++i) {
            audio[i] *= (eq.bass_gain + eq.mid_gain + eq.treble_gain) / 3.0f;
        }
    }
    
    // Hot cue system (instant jump points)
    struct HotCue {
        uint64_t position;
        std::string label;
        bool active;
        
        HotCue() : position(0), active(false) {}
    };
    
    void set_hot_cue(int slot, uint64_t position, const std::string& label = "") {
        std::lock_guard<std::mutex> lock(mutex);
        
        if (slot >= 0 && slot < 8) {
            hot_cues[slot].position = position;
            hot_cues[slot].label = label;
            hot_cues[slot].active = true;
        }
    }
    
    HotCue get_hot_cue(int slot) const {
        std::lock_guard<std::mutex> lock(mutex);
        
        if (slot >= 0 && slot < 8) {
            return hot_cues[slot];
        }
        return HotCue();
    }
    
    void clear_hot_cue(int slot) {
        std::lock_guard<std::mutex> lock(mutex);
        
        if (slot >= 0 && slot < 8) {
            hot_cues[slot].active = false;
        }
    }
    
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