// config.h - Configuration management
#ifndef CONFIG_H
#define CONFIG_H

#include <string>
#include <fstream>
#include <map>

enum class PlaybackMode {
    RADIO,
    DJ,
    CODER
};

enum class VisualizerTheme {
    CYBERPUNK_COFFEE,
    PIXEL_FOREST,
    DEMONIC_NETHERWORLD
};

class Config {
public:
    PlaybackMode mode = PlaybackMode::RADIO;
    VisualizerTheme theme = VisualizerTheme::CYBERPUNK_COFFEE;
    
    int web_port = 8080;
    int stream_port = 8081;
    int sample_rate = 44100;
    int buffer_size = 512;

    // Streaming server configuration
    std::string stream_host = "localhost";
    int stream_server_port = 8000;
    std::string stream_mount = "/stream";
    std::string stream_user = "source";
    std::string stream_password = "hackme";
    std::string stream_name = "Music Stream Platform";
    std::string stream_description = "Live music streaming";
    std::string stream_genre = "Various";
    std::string stream_format = "mp3"; // "mp3" or "ogg"

    std::string music_directory = "./music";
    std::string playlist_file = "";
    
    void load_defaults() {
        // Already initialized with defaults
    }
    
    void load_from_file(const std::string& filename) {
        std::ifstream file(filename);
        if (!file.is_open()) {
            throw std::runtime_error("Cannot open config file: " + filename);
        }
        
        std::string line;
        while (std::getline(file, line)) {
            parse_line(line);
        }
    }
    
    std::string get_mode_string() const {
        switch (mode) {
            case PlaybackMode::RADIO: return "Radio Mode";
            case PlaybackMode::DJ: return "DJ Mode";
            case PlaybackMode::CODER: return "Coder Mode";
            default: return "Unknown";
        }
    }
    
    std::string get_theme_string() const {
        switch (theme) {
            case VisualizerTheme::CYBERPUNK_COFFEE: return "Cyberpunk Coffee Shop";
            case VisualizerTheme::PIXEL_FOREST: return "Pixel Forest";
            case VisualizerTheme::DEMONIC_NETHERWORLD: return "Demonic Netherworld";
            default: return "Unknown";
        }
    }
    
private:
    void parse_line(const std::string& line) {
        if (line.empty() || line[0] == '#') return;
        
        size_t eq_pos = line.find('=');
        if (eq_pos == std::string::npos) return;
        
        std::string key = trim(line.substr(0, eq_pos));
        std::string value = trim(line.substr(eq_pos + 1));
        
        if (key == "mode") {
            if (value == "radio") mode = PlaybackMode::RADIO;
            else if (value == "dj") mode = PlaybackMode::DJ;
            else if (value == "coder") mode = PlaybackMode::CODER;
        } else if (key == "theme") {
            if (value == "cyberpunk") theme = VisualizerTheme::CYBERPUNK_COFFEE;
            else if (value == "forest") theme = VisualizerTheme::PIXEL_FOREST;
            else if (value == "netherworld") theme = VisualizerTheme::DEMONIC_NETHERWORLD;
        } else if (key == "web_port") {
            web_port = std::stoi(value);
        } else if (key == "stream_port") {
            stream_port = std::stoi(value);
        } else if (key == "music_directory") {
            music_directory = value;
        } else if (key == "stream_host") {
            stream_host = value;
        } else if (key == "stream_server_port") {
            stream_server_port = std::stoi(value);
        } else if (key == "stream_mount") {
            stream_mount = value;
        } else if (key == "stream_user") {
            stream_user = value;
        } else if (key == "stream_password") {
            stream_password = value;
        } else if (key == "stream_name") {
            stream_name = value;
        } else if (key == "stream_description") {
            stream_description = value;
        } else if (key == "stream_genre") {
            stream_genre = value;
        } else if (key == "stream_format") {
            stream_format = value;
        }
    }
    
    std::string trim(const std::string& str) {
        size_t first = str.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) return "";
        size_t last = str.find_last_not_of(" \t\r\n");
        return str.substr(first, last - first + 1);
    }
};

#endif // CONFIG_H