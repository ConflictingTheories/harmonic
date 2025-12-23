// playlist_manager.h - Complete playlist management
#ifndef PLAYLIST_MANAGER_H
#define PLAYLIST_MANAGER_H

#include <vector>
#include <string>
#include <mutex>
#include <map>
#include "config.h"

struct Track {
    std::string filepath;
    std::string title;
    std::string artist;
    std::string album;
    std::string year;
    std::string genre;
    int duration_ms;
    int bitrate;
    
    Track(const std::string& path) 
        : filepath(path), title(""), artist("Unknown"), album(""), 
          year(""), genre(""), duration_ms(0), bitrate(0) {}
};

enum class PlaylistFormat {
    M3U,
    M3U8,
    PLS
};

enum class SortCriteria {
    TITLE,
    ARTIST,
    ALBUM,
    DURATION
};

class PlaylistManager {
public:
    PlaylistManager(const Config& cfg);
    
    // Scanning and loading
    void scan_music_directory();
    bool load_playlist_file(const std::string& filepath);
    bool save_playlist(const std::string& filepath, PlaylistFormat format = PlaylistFormat::M3U);
    
    // Playback control
    void set_auto_advance(bool enable) { auto_advance_enabled = enable; }
    void enable_cue_system(bool enable) { cue_system_enabled = enable; }
    
    Track* get_current_track() {
        std::lock_guard<std::mutex> lock(playlist_mutex);
        if (tracks.empty()) return nullptr;
        return &tracks[current_index];
    }
    
    Track* get_next_track() {
        std::lock_guard<std::mutex> lock(playlist_mutex);
        if (tracks.empty()) return nullptr;
        size_t next = (current_index + 1) % tracks.size();
        return &tracks[next];
    }
    
    void next() {
        std::lock_guard<std::mutex> lock(playlist_mutex);
        if (tracks.empty()) return;
        current_index = (current_index + 1) % tracks.size();
    }
    
    void previous() {
        std::lock_guard<std::mutex> lock(playlist_mutex);
        if (tracks.empty()) return;
        if (current_index > 0) {
            current_index--;
        } else {
            current_index = tracks.size() - 1;
        }
    }
    
    void jump_to(size_t index) {
        std::lock_guard<std::mutex> lock(playlist_mutex);
        if (index < tracks.size()) {
            current_index = index;
        }
    }
    
    // Queue management
    void add_to_queue(const std::string& filepath) {
        std::lock_guard<std::mutex> lock(queue_mutex);
        queue.push_back(filepath);
    }
    
    Track* get_queued_track() {
        std::lock_guard<std::mutex> lock(queue_mutex);
        if (queue.empty()) return nullptr;
        
        Track* track = new Track(queue.front());
        queue.erase(queue.begin());
        return track;
    }
    
    bool has_queued() {
        std::lock_guard<std::mutex> lock(queue_mutex);
        return !queue.empty();
    }
    
    // Playlist manipulation
    void shuffle();
    void sort_by(SortCriteria criteria);
    
    // Getters
    size_t get_track_count() const { return tracks.size(); }
    size_t get_current_index() const { return current_index; }
    
    std::vector<Track> get_all_tracks() {
        std::lock_guard<std::mutex> lock(playlist_mutex);
        return tracks;
    }
    
private:
    Config config;
    std::vector<Track> tracks;
    std::vector<std::string> queue;
    
    size_t current_index;
    bool auto_advance_enabled;
    bool cue_system_enabled;
    
    std::mutex playlist_mutex;
    std::mutex queue_mutex;
    
    // Playlist format parsers
    bool load_m3u(const std::string& filepath);
    bool load_pls(const std::string& filepath);
    bool save_m3u(const std::string& filepath);
    bool save_pls(const std::string& filepath);
    
    bool is_supported_format(const std::string& ext);
};

#endif // PLAYLIST_MANAGER_H