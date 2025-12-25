#include "network_server.h"
#include <iostream>
#include <sstream>
#include <vector>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <thread>
#include <chrono>
#include <algorithm>
#include <mutex>

// Libshout streaming implementation
void NetworkServer::init_libshout() {
    shout_init();
    shout_conn = shout_new();

    if (!shout_conn) {
        std::cerr << "Failed to create shout connection" << std::endl;
        return;
    }

    // Set shout parameters from config
    shout_set_host(shout_conn, config.stream_host.c_str());
    shout_set_port(shout_conn, config.stream_server_port);
    shout_set_mount(shout_conn, config.stream_mount.c_str());
    shout_set_user(shout_conn, config.stream_user.c_str());
    shout_set_password(shout_conn, config.stream_password.c_str());

    // Set audio format and parameters - CRITICAL: libshout encodes audio based on format setting
    if (config.stream_format == "ogg") {
        shout_set_format(shout_conn, SHOUT_FORMAT_OGG);
        std::cout << "  → Streaming format: OGG/Vorbis" << std::endl;
    } else {
        shout_set_format(shout_conn, SHOUT_FORMAT_MP3);
        std::cout << "  → Streaming format: MP3" << std::endl;
    }

    // Set audio parameters for proper encoding
    shout_set_audio_info(shout_conn, SHOUT_AI_SAMPLERATE, std::to_string(config.sample_rate).c_str());
    shout_set_audio_info(shout_conn, SHOUT_AI_CHANNELS, "2"); // Stereo
    
    // Quality/bitrate settings based on format
    if (config.stream_format == "ogg") {
        shout_set_audio_info(shout_conn, SHOUT_AI_QUALITY, "4.0"); // Vorbis quality (0-10 scale)
    } else {
        shout_set_audio_info(shout_conn, SHOUT_AI_BITRATE, "192"); // MP3 bitrate (128-320 kbps)
    }

    shout_set_protocol(shout_conn, SHOUT_PROTOCOL_HTTP);

    // Set metadata
    shout_set_name(shout_conn, config.stream_name.c_str());
    shout_set_description(shout_conn, config.stream_description.c_str());
    shout_set_genre(shout_conn, config.stream_genre.c_str());

    std::cout << "✓ Libshout initialized for streaming to " << config.stream_host << ":" << config.stream_server_port << config.stream_mount << std::endl;
}

void NetworkServer::start_libshout_streaming() {
    if (!shout_conn) return;

    if (shout_open(shout_conn) != SHOUTERR_SUCCESS) {
        std::cerr << "Failed to open shout connection: " << shout_get_error(shout_conn) << std::endl;
        return;
    }

    shout_streaming_thread = std::thread([this]() {
        // Use buffer size from config for consistent audio processing
        const size_t CHUNK_SIZE = config.buffer_size * 2; // Lower chunk size for less delay
        while (running && audio_engine->is_active()) {
            std::vector<float> buffer = audio_engine->get_stream_buffer(CHUNK_SIZE);

            if (buffer.empty()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }

            encode_and_send_audio(buffer);

            // Calculate delay based on buffer size and sample rate for proper timing
            double buffer_duration_ms = (static_cast<double>(buffer.size()) / 2.0 / config.sample_rate) * 1000.0;
            std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(buffer_duration_ms * 0.5))); // Shorter sleep for lower latency
        }
    });

    std::cout << "✓ Libshout streaming started" << std::endl;
}

void NetworkServer::encode_and_send_audio(const std::vector<float>& buffer) {
    // Convert float32 to int16 PCM
    std::vector<int16_t> pcm_data(buffer.size());
    for (size_t i = 0; i < buffer.size(); ++i) {
        float sample = std::max(-1.0f, std::min(1.0f, buffer[i]));
        pcm_data[i] = static_cast<int16_t>(sample * 32767.0f);
    }

    // Send as PCM data (libshout handles encoding to MP3/OGG)
    int ret = shout_send(shout_conn, reinterpret_cast<unsigned char*>(pcm_data.data()), pcm_data.size() * sizeof(int16_t));
    if (ret != SHOUTERR_SUCCESS) {
        std::cerr << "Shout send error: " << shout_get_error(shout_conn) << std::endl;
        return;
    }

    // Sync with shout server
    shout_sync(shout_conn);
}

void NetworkServer::stop_libshout_streaming() {
    if (shout_streaming_thread.joinable()) {
        shout_streaming_thread.join();
    }
}

// WebSocket implementation
#ifdef HAS_WEBSOCKETPP
void NetworkServer::init_websocket_server() {
    try {
        ws_srv.init_asio();
        ws_srv.set_reuse_addr(true);

        ws_srv.set_open_handler([this](websocketpp::connection_hdl hdl) {
            // Check if the connection is to the correct path
            websocketpp::uri_ptr uri = ws_srv.get_con_from_hdl(hdl)->get_uri();
            if (uri->get_resource() != "/ws/fft") {
                ws_srv.close(hdl, websocketpp::close::status::normal, "Invalid path");
                return;
            }

            std::lock_guard<std::mutex> lock(ws_connections_mutex);
            ws_connections.push_back(hdl);
            std::cout << "WebSocket client connected to " << uri->get_resource() << std::endl;
        });

        ws_srv.set_close_handler([this](websocketpp::connection_hdl hdl) {
            std::lock_guard<std::mutex> lock(ws_connections_mutex);
            ws_connections.erase(std::remove(ws_connections.begin(), ws_connections.end(), hdl), ws_connections.end());
            std::cout << "WebSocket client disconnected" << std::endl;
        });

        ws_srv.set_message_handler([this](websocketpp::connection_hdl hdl, ws_server::message_ptr msg) {
            // Handle incoming messages if needed
        });

    } catch (const std::exception& e) {
        std::cerr << "WebSocket server init error: " << e.what() << std::endl;
    }
}
#endif

#ifdef HAS_WEBSOCKETPP
void NetworkServer::start_websocket_server() {
    ws_server_running = true;

    ws_server_thread = std::thread([this]() {
        try {
            ws_srv.listen(config.web_port + 1); // Use web_port + 1 for WebSocket
            ws_srv.start_accept();
            ws_srv.run();
        } catch (const std::exception& e) {
            std::cerr << "WebSocket server error: " << e.what() << std::endl;
        }
    });

    // Start FFT broadcasting thread
    fft_broadcast_thread = std::thread([this]() {
        while (ws_server_running) {
            broadcast_fft_data();
            std::this_thread::sleep_for(std::chrono::milliseconds(50)); // ~20 FPS
        }
    });

    std::cout << "✓ WebSocket server started on port " << (config.web_port + 1) << std::endl;
}
#endif

#ifdef HAS_WEBSOCKETPP
void NetworkServer::stop_websocket_server() {
    ws_server_running = false;
    if (fft_broadcast_thread.joinable()) {
        fft_broadcast_thread.join();
    }
    if (ws_server_thread.joinable()) {
        ws_server_thread.join();
    }
    ws_srv.stop();
}
#endif

#ifdef HAS_WEBSOCKETPP
void NetworkServer::upgrade_to_websocket(int client_fd, const std::string& request) {
    // For simplicity, we'll handle WebSocket upgrade in the WebSocket++ server
    // This method is called but the actual upgrade is handled by WebSocket++
    close(client_fd);
}
#endif

#ifdef HAS_WEBSOCKETPP
void NetworkServer::broadcast_fft_data() {
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

    std::lock_guard<std::mutex> lock(ws_connections_mutex);
    for (auto& hdl : ws_connections) {
        try {
            ws_srv.send(hdl, json_str, websocketpp::frame::opcode::text);
        } catch (const std::exception& e) {
            std::cerr << "WebSocket send error: " << e.what() << std::endl;
        }
    }
}
#endif

// Stub implementations when WebSocket is disabled
#ifndef HAS_WEBSOCKETPP
void NetworkServer::init_websocket_server() {
    // WebSocket support disabled
}

void NetworkServer::start_websocket_server() {
    // WebSocket support disabled
}

void NetworkServer::stop_websocket_server() {
    // WebSocket support disabled
}

void NetworkServer::upgrade_to_websocket(int client_fd, const std::string& request) {
    // WebSocket support disabled
}

void NetworkServer::broadcast_fft_data() {
    // WebSocket support disabled
}
#endif

void NetworkServer::start() {
    running = true;

    // Initialize libshout
    init_libshout();
    start_libshout_streaming();

    // Initialize WebSocket server (if available)
#ifdef HAS_WEBSOCKETPP
    init_websocket_server();
    start_websocket_server();
#endif

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

void NetworkServer::stop() {
    running = false;
    if (server_fd >= 0) {
        close(server_fd);
        server_fd = -1;
    }

    // Stop libshout streaming
    stop_libshout_streaming();
    if (shout_conn) {
        shout_close(shout_conn);
        shout_shutdown();
        shout_conn = nullptr;
    }

    // Stop WebSocket server (if available)
#ifdef HAS_WEBSOCKETPP
    stop_websocket_server();
#endif
}

void NetworkServer::handle_client(int client_fd) {
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
    } else if (request.find("GET /api/mute") == 0) {
        send_mute_response(client_fd);
    } else if (request.find("POST /api/mute") == 0) {
        handle_mute_toggle(client_fd);
    } else if (request.find("GET /stream") == 0) {
        send_audio_stream(client_fd);
        } else if (request.find("GET /ws/fft") == 0) {
            // Upgrade to WebSocket for FFT (if available)
#ifdef HAS_WEBSOCKETPP
            upgrade_to_websocket(client_fd, request);
#else
            send_404(client_fd);
#endif
        } else {
        send_404(client_fd);
    }

    close(client_fd);
}

void NetworkServer::send_html_response(int client_fd) {
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

void NetworkServer::send_fft_response(int client_fd) {
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

void NetworkServer::send_track_response(int client_fd) {
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

void NetworkServer::send_theme_response(int client_fd) {
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

void NetworkServer::send_mute_response(int client_fd) {
    bool is_muted = audio_engine->is_muted();

    std::stringstream json;
    json << "{\"muted\":" << (is_muted ? "true" : "false") << "}";

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

void NetworkServer::handle_mute_toggle(int client_fd) {
    bool current_mute = audio_engine->is_muted();
    audio_engine->set_muted(!current_mute);

    std::stringstream json;
    json << "{\"muted\":" << (!current_mute ? "true" : "false") << "}";

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



void NetworkServer::send_audio_stream(int client_fd) {
    // Stream the currently playing MP3 file directly from disk
    Track* current_track = playlist_mgr->get_current_track();
    
    if (!current_track || current_track->filepath.empty()) {
        // No track loaded - send 404
        std::string response = 
            "HTTP/1.1 404 Not Found\r\n"
            "Content-Type: text/plain\r\n"
            "Content-Length: 16\r\n"
            "Connection: close\r\n"
            "\r\n"
            "No track loaded.";
        send(client_fd, response.c_str(), response.length(), 0);
        return;
    }

    // Open the MP3 file
    std::ifstream mp3_file(current_track->filepath, std::ios::binary);
    if (!mp3_file.is_open()) {
        std::string response = 
            "HTTP/1.1 404 Not Found\r\n"
            "Content-Type: text/plain\r\n"
            "Content-Length: 19\r\n"
            "Connection: close\r\n"
            "\r\n"
            "File not found.";
        send(client_fd, response.c_str(), response.length(), 0);
        return;
    }

    // Get file size
    mp3_file.seekg(0, std::ios::end);
    std::streampos file_size = mp3_file.tellg();
    mp3_file.seekg(0, std::ios::beg);

    // Send HTTP headers with correct content length
    std::stringstream response;
    response << "HTTP/1.1 200 OK\r\n";
    response << "Content-Type: audio/mpeg\r\n";
    response << "Content-Length: " << file_size << "\r\n";
    response << "Accept-Ranges: bytes\r\n";
    response << "Connection: keep-alive\r\n";
    response << "Cache-Control: no-cache\r\n";
    response << "icy-name: " << current_track->title << "\r\n";
    response << "icy-description: " << current_track->artist << "\r\n";
    response << "\r\n";

    std::string resp_str = response.str();
    send(client_fd, resp_str.c_str(), resp_str.length(), 0);

    // Stream the MP3 file directly
    const size_t BUFFER_SIZE = 65536; // 64KB chunks
    char buffer[BUFFER_SIZE];
    size_t total_sent = 0;

    while (mp3_file.read(buffer, BUFFER_SIZE) || mp3_file.gcount() > 0) {
        size_t bytes_to_send = mp3_file.gcount();
        ssize_t sent = send(client_fd, buffer, bytes_to_send, MSG_NOSIGNAL);
        
        if (sent < 0) {
            // Client disconnected
            break;
        }
        
        total_sent += sent;
    }

    mp3_file.close();
    std::cout << "✓ MP3 stream delivered: " << current_track->title << " (" << total_sent << " bytes)" << std::endl;
}

void NetworkServer::send_404(int client_fd) {
    std::string response =
        "HTTP/1.1 404 Not Found\r\n"
        "Content-Length: 13\r\n"
        "Connection: close\r\n"
        "\r\n"
        "404 Not Found";
    send(client_fd, response.c_str(), response.length(), 0);
}

std::string NetworkServer::escape_json(const std::string& str) {
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

std::string NetworkServer::get_theme_param() {
    switch (config.theme) {
        case VisualizerTheme::CYBERPUNK_COFFEE: return "cyberpunk";
        case VisualizerTheme::PIXEL_FOREST: return "forest";
        case VisualizerTheme::DEMONIC_NETHERWORLD: return "netherworld";
        default: return "cyberpunk";
    }
}

std::string NetworkServer::generate_html() {
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
