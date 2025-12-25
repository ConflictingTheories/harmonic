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
#include <mutex>
#include <iostream>

#include "config.h"
#include "audio_engine.h"
#include "playlist_manager.h"

// Libshout for streaming (MP3/OGG)
#include <shout/shout.h>

// WebSocket++ includes (optional, disabled by default)
#ifdef HAS_WEBSOCKETPP
    #include <websocketpp/server.hpp>
    #include <websocketpp/config/asio.hpp>
#endif

class NetworkServer {
public:
    NetworkServer(Config& cfg, std::shared_ptr<AudioEngine> audio, std::shared_ptr<PlaylistManager> playlist) 
        : config(cfg), audio_engine(audio), playlist_mgr(playlist), running(false), server_fd(-1) {}
    
    ~NetworkServer() {
        stop();
    }
    
    void start();
    void stop();
    
private:
    Config& config;
    std::shared_ptr<AudioEngine> audio_engine;
    std::shared_ptr<PlaylistManager> playlist_mgr;
    std::atomic<bool> running;
    int server_fd;

    // Libshout members
    shout_t* shout_conn = nullptr;
    std::thread shout_streaming_thread;

    // WebSocket members (optional)
#ifdef HAS_WEBSOCKETPP
    typedef websocketpp::server<websocketpp::config::asio> ws_server;
    ws_server ws_srv;
    std::thread ws_server_thread;
    std::atomic<bool> ws_server_running = false;
    std::vector<websocketpp::connection_hdl> ws_connections;
    std::mutex ws_connections_mutex;
    std::thread fft_broadcast_thread;
#else
    typedef int ws_server;  // Dummy type when WebSocket disabled
#endif
    
    void handle_client(int client_fd);
    void send_html_response(int client_fd);
    void send_fft_response(int client_fd);
    void send_track_response(int client_fd);
    void send_theme_response(int client_fd);
    void send_mute_response(int client_fd);
    void send_mode_response(int client_fd);
    void handle_mute_toggle(int client_fd);
    void send_audio_stream(int client_fd);
    void send_404(int client_fd);

    std::string escape_json(const std::string& str);
    std::string get_theme_param();
    std::string generate_html();

    // Libshout streaming methods
    void init_libshout();
    void start_libshout_streaming();
    void stop_libshout_streaming();

    // WebSocket methods (stubs when disabled)
    void init_websocket_server();
    void start_websocket_server();
    void stop_websocket_server();
    void upgrade_to_websocket(int client_fd, const std::string& request);
    void broadcast_fft_data();

    // Audio encoding for MP3/OGG
    void encode_and_send_audio(const std::vector<float>& buffer);
};

#endif // NETWORK_SERVER_H