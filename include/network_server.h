// network_server.h - COMPLETE HTTP server with audio streaming
#ifndef NETWORK_SERVER_H
#define NETWORK_SERVER_H

#include <string>
#include <memory>
#include <thread>
#include <atomic>
#include <sstream>
#include <vector>
#include <cstring>
#include <fstream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include "config.h"

#include <iostream>
#include "audio_engine.h"
#include "playlist_manager.h"

class NetworkServer {
public:
    NetworkServer(Config& cfg, std::shared_ptr<AudioEngine> audio, std::shared_ptr<PlaylistManager> playlist) 
        : config(cfg), audio_engine(audio), playlist_mgr(playlist), running(false), server_fd(-1) {}
    
    ~NetworkServer() {
        stop();
    }
    
    void start() {
        running = true;
        
        // Create socket
        server_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (server_fd < 0) {
            throw std::runtime_error("Failed to create socket");
        }
        
        // Set socket options
        int opt = 1;
        setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
        
        // Bind to port
        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(config.web_port);
        
        if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            close(server_fd);
            throw std::runtime_error("Failed to bind to port");
        }
        
        // Listen
        if (listen(server_fd, 10) < 0) {
            close(server_fd);
            throw std::runtime_error("Failed to listen");
        }
        
        std::cout << "✓ Network server listening on port " << config.web_port << std::endl;
        
        // Accept connections in loop
        while (running) {
            struct sockaddr_in client_addr;
            socklen_t client_len = sizeof(client_addr);
            
            int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
            if (client_fd < 0) {
                if (running) continue;
                else break;
            }
            
            // Handle in separate thread
            std::thread([this, client_fd]() {
                handle_client(client_fd);
            }).detach();
        }
    }
    
    void stop() {
        running = false;
        if (server_fd >= 0) {
            close(server_fd);
            server_fd = -1;
        }
    }
    
private:
    Config& config;
    std::shared_ptr<AudioEngine> audio_engine;
    std::shared_ptr<PlaylistManager> playlist_mgr;
    std::atomic<bool> running;
    int server_fd;
    
    void handle_client(int client_fd) {
        char buffer[8192];
        ssize_t bytes_read = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
        
        if (bytes_read <= 0) {
            close(client_fd);
            return;
        }
        
        buffer[bytes_read] = '\0';
        std::string request(buffer);
        
        // Parse HTTP request
        if (request.find("GET / ") == 0 || request.find("GET /index.html") == 0) {
            send_html_response(client_fd);
        } else if (request.find("GET /api/track") == 0) {
            send_track_response(client_fd);
        } else if (request.find("GET /api/fft") == 0) {
            send_fft_response(client_fd);
        } else if (request.find("GET /api/theme") == 0) {
            send_theme_response(client_fd);
        } else if (request.find("GET /stream") == 0) {
            send_audio_stream(client_fd);
        } else {
            send_404(client_fd);
        }
        
        close(client_fd);
    }
    
    void send_html_response(int client_fd) {
        std::string html = generate_html();
        std::stringstream response;
        
        response << "HTTP/1.1 200 OK\r\n";
        response << "Content-Type: text/html\r\n";
        response << "Content-Length: " << html.length() << "\r\n";
        response << "Connection: close\r\n";
        response << "\r\n";
        response << html;
        
        std::string resp_str = response.str();
        send(client_fd, resp_str.c_str(), resp_str.length(), 0);
    }
    
    void send_fft_response(int client_fd) {
        FFTData fft = audio_engine->get_fft_data();
        
        std::stringstream json;
        json << "{";
        json << "\"bass\":" << fft.bass << ",";
        json << "\"mid\":" << fft.mid << ",";
        json << "\"treble\":" << fft.treble << ",";
        json << "\"energy\":" << fft.energy << ",";
        json << "\"magnitudes\":[";
        
        for (size_t i = 0; i < fft.magnitudes.size(); ++i) {
            if (i > 0) json << ",";
            json << fft.magnitudes[i];
        }
        json << "]}";
        
        std::string json_str = json.str();
        std::stringstream response;
        
        response << "HTTP/1.1 200 OK\r\n";
        response << "Content-Type: application/json\r\n";
        response << "Content-Length: " << json_str.length() << "\r\n";
        response << "Access-Control-Allow-Origin: *\r\n";
        response << "Connection: close\r\n";
        response << "\r\n";
        response << json_str;
        
        std::string resp_str = response.str();
        send(client_fd, resp_str.c_str(), resp_str.length(), 0);
    }
    
    void send_track_response(int client_fd) {
        Track* current_track = playlist_mgr->get_current_track();
        
        std::stringstream json;
        json << "{";
        if (current_track) {
            json << "\"title\":\"" << escape_json(current_track->title) << "\",";
            json << "\"artist\":\"" << escape_json(current_track->artist) << "\",";
            json << "\"album\":\"" << escape_json(current_track->album) << "\",";
            json << "\"duration\":" << current_track->duration_ms;
        } else {
            json << "\"title\":\"No track loaded\",";
            json << "\"artist\":\"\",";
            json << "\"album\":\"\",";
            json << "\"duration\":0";
        }
        json << "}";
        
        std::string json_str = json.str();
        std::stringstream response;
        
        response << "HTTP/1.1 200 OK\r\n";
        response << "Content-Type: application/json\r\n";
        response << "Content-Length: " << json_str.length() << "\r\n";
        response << "Access-Control-Allow-Origin: *\r\n";
        response << "Connection: close\r\n";
        response << "\r\n";
        response << json_str;
        
        std::string resp_str = response.str();
        send(client_fd, resp_str.c_str(), resp_str.length(), 0);
    }
    
    void send_theme_response(int client_fd) {
        std::string theme_str = get_theme_param();
        
        std::stringstream json;
        json << "{\"theme\":\"" << theme_str << "\"}";
        
        std::string json_str = json.str();
        std::stringstream response;
        
        response << "HTTP/1.1 200 OK\r\n";
        response << "Content-Type: application/json\r\n";
        response << "Content-Length: " << json_str.length() << "\r\n";
        response << "Access-Control-Allow-Origin: *\r\n";
        response << "Connection: close\r\n";
        response << "\r\n";
        response << json_str;
        
        std::string resp_str = response.str();
        send(client_fd, resp_str.c_str(), resp_str.length(), 0);
    }
    
    void send_audio_stream(int client_fd) {
        // WAV header structure
        struct WAVHeader {
            char riff[4] = {'R', 'I', 'F', 'F'};
            uint32_t file_size = 0xFFFFFFFF; // Unknown size for streaming
            char wave[4] = {'W', 'A', 'V', 'E'};
            char fmt[4] = {'f', 'm', 't', ' '};
            uint32_t fmt_size = 16;
            uint16_t audio_format = 1; // PCM
            uint16_t num_channels = 2; // Stereo
            uint32_t sample_rate = 44100;
            uint32_t byte_rate = 44100 * 2 * 2; // sample_rate * channels * bytes_per_sample
            uint16_t block_align = 4; // channels * bytes_per_sample
            uint16_t bits_per_sample = 16;
            char data[4] = {'d', 'a', 't', 'a'};
            uint32_t data_size = 0xFFFFFFFF; // Unknown size for streaming
        } __attribute__((packed));
        
        WAVHeader header;
        header.sample_rate = config.sample_rate;
        header.byte_rate = config.sample_rate * 2 * 2;
        
        // Send HTTP headers
        std::stringstream response;
        response << "HTTP/1.1 200 OK\r\n";
        response << "Content-Type: audio/wav\r\n";
        response << "Connection: keep-alive\r\n";
        response << "Cache-Control: no-cache\r\n";
        response << "Accept-Ranges: none\r\n";
        response << "\r\n";
        
        std::string resp_str = response.str();
        send(client_fd, resp_str.c_str(), resp_str.length(), 0);
        
        // Send WAV header
        send(client_fd, &header, sizeof(header), 0);
        
        // Stream audio data
        const size_t CHUNK_FRAMES = 1024;
        while (running && audio_engine->is_active()) {
            std::vector<float> buffer = audio_engine->get_stream_buffer(CHUNK_FRAMES);
            
            if (buffer.empty()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
            
            // Convert float32 to int16 PCM
            std::vector<int16_t> pcm_data(buffer.size());
            for (size_t i = 0; i < buffer.size(); ++i) {
                float sample = std::max(-1.0f, std::min(1.0f, buffer[i]));
                pcm_data[i] = static_cast<int16_t>(sample * 32767.0f);
            }
            
            // Send PCM data
            ssize_t sent = send(client_fd, pcm_data.data(), pcm_data.size() * sizeof(int16_t), MSG_NOSIGNAL);
            if (sent <= 0) break; // Client disconnected
            
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    
    void send_404(int client_fd) {
        std::string response = 
            "HTTP/1.1 404 Not Found\r\n"
            "Content-Length: 13\r\n"
            "Connection: close\r\n"
            "\r\n"
            "404 Not Found";
        send(client_fd, response.c_str(), response.length(), 0);
    }
    
    std::string escape_json(const std::string& str) {
        std::string result;
        for (char c : str) {
            switch (c) {
                case '"': result += "\\\""; break;
                case '\\': result += "\\\\"; break;
                case '\n': result += "\\n"; break;
                case '\r': result += "\\r"; break;
                case '\t': result += "\\t"; break;
                default: result += c; break;
            }
        }
        return result;
    }
    
    std::string get_theme_param() {
        switch (config.theme) {
            case VisualizerTheme::CYBERPUNK_COFFEE: return "cyberpunk";
            case VisualizerTheme::PIXEL_FOREST: return "forest";
            case VisualizerTheme::DEMONIC_NETHERWORLD: return "netherworld";
            default: return "cyberpunk";
        }
    }
    
    std::string generate_html() {
        std::ifstream file("templates/index.html");
        if (!file.is_open()) {
            std::cerr << "Error: Could not open templates/index.html" << std::endl;
            return "<html><body><h1>Error: Template not found</h1></body></html>";
        }

        std::stringstream buffer;
        buffer << file.rdbuf();
        std::string html = buffer.str();

        // Replace placeholders with actual values
        size_t pos;
        while ((pos = html.find("{{MODE}}")) != std::string::npos) {
            html.replace(pos, 8, config.get_mode_string());
        }
        while ((pos = html.find("{{THEME}}")) != std::string::npos) {
            html.replace(pos, 9, config.get_theme_string());
        }
        while ((pos = html.find("{{THEME_PARAM}}")) != std::string::npos) {
            html.replace(pos, 14, get_theme_param());
        }

        return html;
    }
};

#endif // NETWORK_SERVER_H