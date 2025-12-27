// main.cpp - Entry point and core architecture
#include <iostream>
#include <memory>
#include <thread>
#include <atomic>
#include <signal.h>
#include <fstream>

#include "audio_engine.h"
#include "playlist_manager.h"
#include "network_server.h"
#include "tui_interface.h"
#include "config.h"

std::atomic<bool> g_running(true);

void signal_handler(int signal)
{
    g_running = false;
}

int main(int argc, char **argv)
{
    // Setup signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    // Load configuration
    Config config;
    if (argc > 1)
    {
        config.load_from_file(argv[1]);
    }
    else
    {
        // Try to load default config file
        std::ifstream test_file("config.txt");
        if (test_file.good())
        {
            test_file.close();
            config.load_from_file("config.txt");
        }
        else
        {
            config.load_defaults();
        }
    }

    std::cout << "🎵 Music Streaming Platform Starting...\n";
    std::cout << "Mode: " << config.get_mode_string() << "\n";
    std::cout << "Web UI: http://localhost:" << config.web_port << "\n\n";

    try
    {
        // Initialize core components
        auto audio_engine = std::make_shared<AudioEngine>(config);
        auto playlist_mgr = std::make_shared<PlaylistManager>(config);
        auto tui = std::make_shared<TUIInterface>(config, audio_engine, playlist_mgr);
        // Use new/shared_ptr instead of make_shared due to reference member
        auto network_srv = std::shared_ptr<NetworkServer>(new NetworkServer(config, audio_engine, playlist_mgr));

        // Set up mode-specific behavior
        switch (config.mode)
        {
        case PlaybackMode::RADIO:
            playlist_mgr->set_auto_advance(true);
            break;
        case PlaybackMode::DJ:
            playlist_mgr->set_auto_advance(true);
            playlist_mgr->enable_cue_system(true);
            break;
        case PlaybackMode::CODER:
            audio_engine->enable_live_coding(true);
            break;
        }

        // Enable auto-advance for all modes except CODER
        if (config.mode != PlaybackMode::CODER) {
            playlist_mgr->set_auto_advance(true);
        }

        // Automatically play the first sample in the music directory
        Track *first_track = playlist_mgr->get_current_track();
        if (first_track && !first_track->filepath.empty())
        {
            if (audio_engine->load_track(first_track->filepath))
            {
                std::cout << "Now playing: " << first_track->title << " by " << first_track->artist << std::endl;
            }
            else
            {
                std::cerr << "Failed to load track: " << first_track->filepath << std::endl;
            }
        }
        else
        {
            std::cerr << "No tracks found in music directory." << std::endl;
        }

        // Start audio engine
        audio_engine->start();

        // Start network server in separate thread
        std::thread server_thread([&network_srv]()
                                  {
            try {
                network_srv->start();
            } catch (const std::exception& e) {
                std::cerr << "Network server failed to start: " << e.what() << std::endl;
                std::cerr << "Continuing without web interface. TUI mode active." << std::endl;
            } });

        // Run TUI on main thread
        tui->run();

        // Cleanup
        g_running = false;
        network_srv->stop();
        audio_engine->stop();

        if (server_thread.joinable())
        {
            server_thread.join();
        }
    }
    catch (const std::exception &e)
    {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }

    std::cout << "\n👋 Goodbye!\n";
    return 0;
}