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
        std::stringstream html;
        
        html << R"(<!DOCTYPE html>
<html>
<head>
    <title>Music Visualizer</title>
    <meta charset="utf-8">
    <style>
        * { margin: 0; padding: 0; box-sizing: border-box; }
        body { overflow: hidden; background: #000; font-family: 'Courier New', monospace; }
        canvas { display: block; width: 100vw; height: 100vh; }
        #info {
            position: absolute;
            top: 20px;
            left: 20px;
            color: #fff;
            background: rgba(0,0,0,0.8);
            padding: 15px 20px;
            border-radius: 8px;
            font-size: 14px;
            line-height: 1.6;
            border: 2px solid #333;
            box-shadow: 0 4px 20px rgba(0,0,0,0.5);
        }
        #info div { margin: 5px 0; }
        .label { color: #888; display: inline-block; width: 80px; }
        .value { color: #0ff; font-weight: bold; }
        #audio { display: none; }
    </style>
</head>
<body>
    <canvas id="visualizer"></canvas>
    <div id="info">
        <div><span class="label">Track:</span><span class="value" id="track">Loading...</span></div>
        <div><span class="label">Mode:</span><span class="value">)" << config.get_mode_string() << R"(</span></div>
        <div><span class="label">Theme:</span><span class="value">)" << config.get_theme_string() << R"(</span></div>
        <div><span class="label">FPS:</span><span class="value" id="fps">--</span></div>
    </div>

    <!-- Technical Control Panel -->
    <div id="controls">
        <button id="playPauseBtn" class="control-btn">▶ PLAY</button>
        <input type="range" id="volumeSlider" class="volume-slider" min="0" max="1" step="0.01" value="1">
        <button id="muteBtn" class="control-btn">🔊 MUTE</button>
    </div>

    <audio id="audio" controls>
        <source src="/stream" type="audio/wav">
    </audio>
    <script>
        const canvas = document.getElementById('visualizer');
        const ctx = canvas.getContext('2d');
        const audio = document.getElementById('audio');
        
        function resize() {
            canvas.width = window.innerWidth;
            canvas.height = window.innerHeight;
        }
        resize();
        window.addEventListener('resize', resize);
        
        let audioData = { bass: 0, mid: 0, treble: 0, energy: 0, magnitudes: new Array(64).fill(0) };
        let trackData = { title: "Loading...", artist: "", album: "", duration: 0 };
        let currentTheme = ')" << get_theme_param() << R"(';
        let frameCount = 0;
        let lastFpsUpdate = Date.now();
        let fps = 0;
        
        // Fetch FFT data from server
        function updateAudioData() {
            fetch('/api/fft')
                .then(r => r.json())
                .then(data => {
                    audioData = data;
                })
                .catch(e => {});
        }
        setInterval(updateAudioData, 50); // 20Hz update rate
        
        // Fetch track data from server
        function updateTrackData() {
            fetch('/api/track')
                .then(r => r.json())
                .then(data => {
                    trackData = data;
                    document.getElementById('track').textContent = trackData.title;
                })
                .catch(e => {});
        }
        setInterval(updateTrackData, 1000); // Update every second
        
        // Fetch theme data from server
        function updateTheme() {
            fetch('/api/theme')
                .then(r => r.json())
                .then(data => {
                    currentTheme = data.theme;
                })
                .catch(e => {});
        }
        setInterval(updateTheme, 500); // Update every 500ms
        
        // Control panel functionality
        const playPauseBtn = document.getElementById('playPauseBtn');
        const volumeSlider = document.getElementById('volumeSlider');
        const muteBtn = document.getElementById('muteBtn');

        let isPlaying = false;
        let isMuted = false;
        let lastVolume = 1;

        playPauseBtn.addEventListener('click', () => {
            if (isPlaying) {
                audio.pause();
                playPauseBtn.textContent = '▶ PLAY';
                isPlaying = false;
            } else {
                audio.play().catch(e => console.log('Playback failed:', e));
                playPauseBtn.textContent = '⏸ PAUSE';
                isPlaying = true;
            }
        });

        volumeSlider.addEventListener('input', (e) => {
            const volume = parseFloat(e.target.value);
            audio.volume = volume;
            if (volume === 0) {
                muteBtn.textContent = '🔇 MUTED';
                isMuted = true;
            } else if (isMuted) {
                muteBtn.textContent = '🔊 MUTE';
                isMuted = false;
            }
        });

        muteBtn.addEventListener('click', () => {
            if (isMuted) {
                audio.volume = lastVolume;
                volumeSlider.value = lastVolume;
                muteBtn.textContent = '🔊 MUTE';
                isMuted = false;
            } else {
                lastVolume = audio.volume;
                audio.volume = 0;
                volumeSlider.value = 0;
                muteBtn.textContent = '🔇 MUTED';
                isMuted = true;
            }
        });

        // Initialize audio playback (no autoplay)
        // User must click PLAY to start
        
        // FPS counter
        function updateFPS() {
            frameCount++;
            const now = Date.now();
            if (now - lastFpsUpdate >= 1000) {
                fps = frameCount;
                document.getElementById('fps').textContent = fps;
                frameCount = 0;
                lastFpsUpdate = now;
            }
        }
        
        // Main render loop
        function render() {
            updateFPS();
            
            if (currentTheme === 'cyberpunk') {
                renderCyberpunk();
            } else if (currentTheme === 'forest') {
                renderForest();
            } else if (currentTheme === 'netherworld') {
                renderNetherworld();
            }
            
            requestAnimationFrame(render);
        }
        
        // Cyberpunk Coffee Shop Theme
        function renderCyberpunk() {
            ctx.fillStyle = 'rgba(0, 0, 0, 0.15)';
            ctx.fillRect(0, 0, canvas.width, canvas.height);
            
            const bars = audioData.magnitudes.length;
            const barWidth = canvas.width / bars;
            
            for (let i = 0; i < bars; i++) {
                const height = audioData.magnitudes[i] * canvas.height * 0.7;
                const x = i * barWidth;
                const y = canvas.height - height;
                
                const gradient = ctx.createLinearGradient(x, y, x, canvas.height);
                gradient.addColorStop(0, '#ff00ff');
                gradient.addColorStop(0.5, '#00ffff');
                gradient.addColorStop(1, '#ff00ff');
                
                ctx.fillStyle = gradient;
                ctx.fillRect(x + 1, y, barWidth - 2, height);
                
                // Glow effect
                ctx.shadowBlur = 20;
                ctx.shadowColor = '#0ff';
                ctx.fillRect(x + 1, y, barWidth - 2, height);
                ctx.shadowBlur = 0;
            }
            
            // Coffee cup
            const cupX = canvas.width - 100;
            const cupY = canvas.height - 80;
            const steam = audioData.energy * 3;
            
            ctx.fillStyle = '#8b4513';
            ctx.fillRect(cupX, cupY, 60, 50);
            ctx.fillStyle = '#654321';
            ctx.fillRect(cupX + 5, cupY + 5, 50, 40);
            
            // Steam particles
            for (let i = 0; i < 3; i++) {
                ctx.globalAlpha = 0.4 * steam;
                ctx.fillStyle = '#fff';
                const steamX = cupX + 20 + i * 10;
                const steamY = cupY - 10 - steam * 15 + Math.sin(Date.now() * 0.003 + i) * 10;
                ctx.beginPath();
                ctx.arc(steamX, steamY, 5, 0, Math.PI * 2);
                ctx.fill();
            }
            ctx.globalAlpha = 1;
        }
        
        // Pixel Forest Theme
        let treeOffsets = new Array(30).fill(0).map(() => Math.random() * Math.PI * 2);
        let fireflies = [];
        
        function renderForest() {
            ctx.fillStyle = 'rgba(0, 20, 10, 0.2)';
            ctx.fillRect(0, 0, canvas.width, canvas.height);
            
            const treeCount = 30;
            const time = Date.now() * 0.001;
            
            for (let i = 0; i < treeCount; i++) {
                const x = (i / treeCount) * canvas.width;
                const sway = Math.sin(time + treeOffsets[i]) * audioData.bass * 15;
                const treeX = x + sway;
                
                const baseHeight = canvas.height * 0.4;
                const reactiveHeight = audioData.magnitudes[i % audioData.magnitudes.length] * 150;
                const height = baseHeight + reactiveHeight;
                
                // Tree trunk
                ctx.fillStyle = '#2d5016';
                ctx.fillRect(treeX - 8, canvas.height - height, 16, height);
                
                // Leaves
                ctx.fillStyle = '#4a7c2c';
                ctx.fillRect(treeX - 25, canvas.height - height - 40, 50, 40);
                ctx.fillRect(treeX - 20, canvas.height - height - 60, 40, 25);
            }
            
            // Fireflies
            if (Math.random() < 0.15 && fireflies.length < 100) {
                fireflies.push({
                    x: Math.random() * canvas.width,
                    y: Math.random() * canvas.height * 0.7,
                    vx: (Math.random() - 0.5) * 2,
                    vy: (Math.random() - 0.5) * 2,
                    brightness: Math.random(),
                    life: 100
                });
            }
            
            fireflies = fireflies.filter(f => f.life > 0);
            fireflies.forEach(f => {
                f.x += f.vx;
                f.y += f.vy;
                f.life -= 0.5;
                f.brightness = 0.5 + Math.sin(Date.now() * 0.01 + f.x) * 0.5;
                
                const alpha = (f.life / 100) * f.brightness;
                ctx.fillStyle = `rgba(255, 255, 150, ${alpha})`;
                ctx.beginPath();
                ctx.arc(f.x, f.y, 3, 0, Math.PI * 2);
                ctx.fill();
                
                ctx.shadowBlur = 10;
                ctx.shadowColor = 'rgba(255, 255, 150, 0.8)';
                ctx.fill();
                ctx.shadowBlur = 0;
            });
        }
        
        // Demonic Netherworld Theme
        let flames = [];
        
        function renderNetherworld() {
            ctx.fillStyle = 'rgba(15, 0, 0, 0.2)';
            ctx.fillRect(0, 0, canvas.width, canvas.height);
            
            // Waveform
            ctx.strokeStyle = '#ff0000';
            ctx.lineWidth = 4;
            ctx.shadowBlur = 15;
            ctx.shadowColor = '#ff0000';
            ctx.beginPath();
            
            for (let i = 0; i < audioData.magnitudes.length; i++) {
                const x = (i / audioData.magnitudes.length) * canvas.width;
                const y = canvas.height / 2 + (audioData.magnitudes[i] - 0.5) * canvas.height * 0.5;
                
                if (i === 0) ctx.moveTo(x, y);
                else ctx.lineTo(x, y);
            }
            ctx.stroke();
            ctx.shadowBlur = 0;
            
            // Pulsing pentagram
            const cx = canvas.width / 2;
            const cy = canvas.height / 2;
            const radius = 80 + audioData.energy * 80;
            
            ctx.strokeStyle = '#ff0000';
            ctx.lineWidth = 3;
            ctx.shadowBlur = 20;
            ctx.shadowColor = '#ff0000';
            ctx.beginPath();
            
            for (let i = 0; i <= 5; i++) {
                const angle = (i * 144 - 90) * Math.PI / 180;
                const x = cx + radius * Math.cos(angle);
                const y = cy + radius * Math.sin(angle);
                if (i === 0) ctx.moveTo(x, y);
                else ctx.lineTo(x, y);
            }
            ctx.stroke();
            ctx.shadowBlur = 0;
            
            // Flames
            if (audioData.bass > 0.2) {
                for (let i = 0; i < 8; i++) {
                    flames.push({
                        x: Math.random() * canvas.width,
                        y: canvas.height,
                        vy: -(3 + Math.random() * 4),
                        vx: (Math.random() - 0.5) * 2,
                        size: 8 + Math.random() * 15,
                        life: 60
                    });
                }
            }
            
            flames = flames.filter(f => f.life > 0);
            flames.forEach(f => {
                f.y += f.vy;
                f.x += f.vx;
                f.vy += 0.1; // gravity
                f.life--;
                
                const gradient = ctx.createRadialGradient(f.x, f.y, 0, f.x, f.y, f.size);
                gradient.addColorStop(0, `rgba(255, 150, 0, ${f.life / 60})`);
                gradient.addColorStop(0.5, `rgba(255, 50, 0, ${f.life / 80})`);
                gradient.addColorStop(1, 'rgba(255, 0, 0, 0)');
                
                ctx.fillStyle = gradient;
                ctx.beginPath();
                ctx.arc(f.x, f.y, f.size, 0, Math.PI * 2);
                ctx.fill();
            });
        }
        
        // Start rendering
        render();
        
        console.log('Visualizer initialized - Theme:', theme);
    </script>
</body>
</html>)";
        
        return html.str();
    }
};

#endif // NETWORK_SERVER_H