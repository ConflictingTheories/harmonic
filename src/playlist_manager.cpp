// playlist_manager.cpp - Complete playlist management with M3U/PLS support
#include "playlist_manager.h"
#include "metadata_parser.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>

#include <iostream>
#include <random>

namespace fs = std::filesystem;

PlaylistManager::PlaylistManager(const Config& cfg) 
    : config(cfg), current_index(0), auto_advance_enabled(false), cue_system_enabled(false) {
    
    if (!config.playlist_file.empty()) {
        load_playlist_file(config.playlist_file);
    } else {
        scan_music_directory();
    }
}

void PlaylistManager::scan_music_directory() {
    std::lock_guard<std::mutex> lock(playlist_mutex);
    tracks.clear();
    
    if (!fs::exists(config.music_directory)) {
        std::cerr << "Warning: Music directory does not exist: " << config.music_directory << std::endl;
        return;
    }
    
    std::cout << "Scanning music directory: " << config.music_directory << std::endl;
    
    for (const auto& entry : fs::recursive_directory_iterator(config.music_directory)) {
        if (entry.is_regular_file()) {
            std::string ext = entry.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            
            if (is_supported_format(ext)) {
                Track track(entry.path().string());
                
                // Parse metadata
                TrackMetadata meta = MetadataParser::parse(entry.path().string());
                track.title = meta.title.empty() ? entry.path().filename().string() : meta.title;
                track.artist = meta.artist.empty() ? "Unknown" : meta.artist;
                track.album = meta.album;
                track.year = meta.year;
                track.genre = meta.genre;
                track.duration_ms = meta.duration_seconds * 1000;
                track.bitrate = meta.bitrate;
                
                tracks.push_back(track);
            }
        }
    }
    
    std::cout << "Found " << tracks.size() << " tracks" << std::endl;
}

bool PlaylistManager::load_playlist_file(const std::string& filepath) {
    std::string ext = filepath.substr(filepath.find_last_of('.') + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    
    if (ext == "m3u" || ext == "m3u8") {
        return load_m3u(filepath);
    } else if (ext == "pls") {
        return load_pls(filepath);
    }
    
    std::cerr << "Unsupported playlist format: " << ext << std::endl;
    return false;
}

bool PlaylistManager::load_m3u(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        std::cerr << "Failed to open playlist: " << filepath << std::endl;
        return false;
    }
    
    std::lock_guard<std::mutex> lock(playlist_mutex);
    tracks.clear();
    
    std::string line;
    std::string current_title;
    int current_duration = -1;
    
    fs::path playlist_dir = fs::path(filepath).parent_path();
    
    while (std::getline(file, line)) {
        // Trim whitespace
        line.erase(0, line.find_first_not_of(" \t\r\n"));
        line.erase(line.find_last_not_of(" \t\r\n") + 1);
        
        if (line.empty() || line[0] == '#') {
            // Extended M3U info
            if (line.rfind("#EXTINF:", 0) == 0) {
                size_t comma = line.find(',');
                if (comma != std::string::npos) {
                    std::string duration_str = line.substr(8, comma - 8);
                    current_duration = std::stoi(duration_str);
                    current_title = line.substr(comma + 1);
                }
            }
            continue;
        }
        
        // File path
        fs::path track_path(line);
        
        // Handle relative paths
        if (track_path.is_relative()) {
            track_path = playlist_dir / track_path;
        }
        
        if (fs::exists(track_path)) {
            Track track(track_path.string());
            
            // Use EXTINF data if available
            if (!current_title.empty()) {
                // Parse "Artist - Title" format
                size_t dash = current_title.find(" - ");
                if (dash != std::string::npos) {
                    track.artist = current_title.substr(0, dash);
                    track.title = current_title.substr(dash + 3);
                } else {
                    track.title = current_title;
                }
                
                if (current_duration > 0) {
                    track.duration_ms = current_duration * 1000;
                }
                
                current_title.clear();
                current_duration = -1;
            } else {
                // Parse metadata from file
                TrackMetadata meta = MetadataParser::parse(track_path.string());
                track.title = meta.title.empty() ? track_path.filename().string() : meta.title;
                track.artist = meta.artist.empty() ? "Unknown" : meta.artist;
                track.duration_ms = meta.duration_seconds * 1000;
            }
            
            tracks.push_back(track);
        }
    }
    
    std::cout << "Loaded " << tracks.size() << " tracks from playlist" << std::endl;
    return !tracks.empty();
}

bool PlaylistManager::load_pls(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        std::cerr << "Failed to open playlist: " << filepath << std::endl;
        return false;
    }
    
    std::lock_guard<std::mutex> lock(playlist_mutex);
    tracks.clear();
    
    std::map<int, std::string> file_paths;
    std::map<int, std::string> titles;
    std::map<int, int> lengths;
    
    std::string line;
    fs::path playlist_dir = fs::path(filepath).parent_path();
    
    while (std::getline(file, line)) {
        // Trim whitespace
        line.erase(0, line.find_first_not_of(" \t\r\n"));
        line.erase(line.find_last_not_of(" \t\r\n") + 1);
        
        if (line.empty() || line == "[playlist]") continue;
        
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        
        std::string key = line.substr(0, eq);
        std::string value = line.substr(eq + 1);
        
        if (key.rfind("File", 0) == 0) {
            int num = std::stoi(key.substr(4));
            file_paths[num] = value;
        } else if (key.rfind("Title", 0) == 0) {
            int num = std::stoi(key.substr(5));
            titles[num] = value;
        } else if (key.rfind("Length", 0) == 0) {
            int num = std::stoi(key.substr(6));
            lengths[num] = std::stoi(value);
        }
    }
    
    // Build track list
    for (const auto& pair : file_paths) {
        int num = pair.first;
        fs::path track_path(pair.second);
        
        if (track_path.is_relative()) {
            track_path = playlist_dir / track_path;
        }
        
        if (fs::exists(track_path)) {
            Track track(track_path.string());
            
            if (titles.count(num)) {
                track.title = titles[num];
            } else {
                TrackMetadata meta = MetadataParser::parse(track_path.string());
                track.title = meta.title.empty() ? track_path.filename().string() : meta.title;
                track.artist = meta.artist;
            }
            
            if (lengths.count(num)) {
                track.duration_ms = lengths[num] * 1000;
            }
            
            tracks.push_back(track);
        }
    }
    
    std::cout << "Loaded " << tracks.size() << " tracks from PLS playlist" << std::endl;
    return !tracks.empty();
}

bool PlaylistManager::save_playlist(const std::string& filepath, PlaylistFormat format) {
    std::lock_guard<std::mutex> lock(playlist_mutex);
    
    if (format == PlaylistFormat::M3U || format == PlaylistFormat::M3U8) {
        return save_m3u(filepath);
    } else if (format == PlaylistFormat::PLS) {
        return save_pls(filepath);
    }
    
    return false;
}

bool PlaylistManager::save_m3u(const std::string& filepath) {
    std::ofstream file(filepath);
    if (!file.is_open()) return false;
    
    file << "#EXTM3U\n";
    
    for (const auto& track : tracks) {
        int duration_sec = track.duration_ms / 1000;
        file << "#EXTINF:" << duration_sec << "," << track.artist << " - " << track.title << "\n";
        file << track.filepath << "\n";
    }
    
    return true;
}

bool PlaylistManager::save_pls(const std::string& filepath) {
    std::ofstream file(filepath);
    if (!file.is_open()) return false;
    
    file << "[playlist]\n";
    file << "NumberOfEntries=" << tracks.size() << "\n\n";
    
    for (size_t i = 0; i < tracks.size(); ++i) {
        file << "File" << (i + 1) << "=" << tracks[i].filepath << "\n";
        file << "Title" << (i + 1) << "=" << tracks[i].artist << " - " << tracks[i].title << "\n";
        file << "Length" << (i + 1) << "=" << (tracks[i].duration_ms / 1000) << "\n\n";
    }
    
    file << "Version=2\n";
    return true;
}

void PlaylistManager::shuffle() {
    std::lock_guard<std::mutex> lock(playlist_mutex);
    if (tracks.empty()) return;
    
    std::random_device rd;
    std::mt19937 g(rd());
    std::shuffle(tracks.begin(), tracks.end(), g);
    current_index = 0;
}

void PlaylistManager::sort_by(SortCriteria criteria) {
    std::lock_guard<std::mutex> lock(playlist_mutex);
    
    switch (criteria) {
        case SortCriteria::TITLE:
            std::sort(tracks.begin(), tracks.end(), 
                [](const Track& a, const Track& b) { return a.title < b.title; });
            break;
        case SortCriteria::ARTIST:
            std::sort(tracks.begin(), tracks.end(),
                [](const Track& a, const Track& b) { return a.artist < b.artist; });
            break;
        case SortCriteria::ALBUM:
            std::sort(tracks.begin(), tracks.end(),
                [](const Track& a, const Track& b) { return a.album < b.album; });
            break;
        case SortCriteria::DURATION:
            std::sort(tracks.begin(), tracks.end(),
                [](const Track& a, const Track& b) { return a.duration_ms < b.duration_ms; });
            break;
    }
}

bool PlaylistManager::is_supported_format(const std::string& ext) {
    std::string lower_ext = ext;
    std::transform(lower_ext.begin(), lower_ext.end(), lower_ext.begin(), ::tolower);
    
    return lower_ext == ".mp3" || lower_ext == ".wav" || lower_ext == ".ogg" || 
           lower_ext == ".flac" || lower_ext == ".m4a" || lower_ext == ".aac";
}