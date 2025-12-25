// config.h - Configuration management
#ifndef CONFIG_H
#define CONFIG_H

#include <string>
#include <fstream>
#include <map>

enum class PlaybackMode
{
    RADIO,
    DJ,
    CODER
};

enum class VisualizerTheme
{
    CYBERPUNK_COFFEE,
    PIXEL_FOREST,
    DEMONIC_NETHERWORLD
};

class Config
{
public:
    PlaybackMode mode = PlaybackMode::RADIO;
    VisualizerTheme theme = VisualizerTheme::CYBERPUNK_COFFEE;

    int web_port = 8080;
    int stream_port = 8081;
    int sample_rate = 44100;
    int buffer_size = 512;
    int stream_server_port = 8000;

    // Streaming server configuration
    std::string stream_host = "localhost";
    std::string stream_mount = "/stream";
    std::string stream_user = "source";
    std::string stream_password = "hackme";
    std::string stream_name = "Harmonic Audio Streamer";
    std::string stream_description = "Live music streaming";
    std::string stream_genre = "Various";
    std::string stream_format = "mp3"; // "mp3" or "ogg"

    std::string music_directory = "./music";
    std::string playlist_file = "";

    void load_defaults();
    void load_from_file(const std::string &filename);
    std::string get_mode_string() const;
    std::string get_theme_string() const;

private:
    void parse_line(const std::string &line);
    std::string trim(const std::string &str);
};

#endif // CONFIG_H