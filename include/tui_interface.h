// tui_interface.h - Terminal User Interface
#ifndef TUI_INTERFACE_H
#define TUI_INTERFACE_H

#include <iostream>
#include <iomanip>
#include <thread>
#include <chrono>
#include <termios.h>
#include <unistd.h>
#include <sys/select.h>
#include "config.h"
#include "audio_engine.h"
#include "playlist_manager.h"
#include "dj_cue_system.h"

class TUIInterface
{
public:
    TUIInterface(Config &cfg,
                 std::shared_ptr<AudioEngine> audio,
                 std::shared_ptr<PlaylistManager> playlist,
                 std::shared_ptr<DJCueSystem> dj);

    ~TUIInterface();

    void run();

private:
    Config &config;
    std::shared_ptr<AudioEngine> audio_engine;
    std::shared_ptr<PlaylistManager> playlist_mgr;
    std::shared_ptr<DJCueSystem> dj;
    bool running;

    struct termios old_term, new_term;

    void setup_terminal();
    void restore_terminal();
    void clear_screen();
    void move_cursor(int row, int col);
    void print_header();
    void update_display();
    void print_controls();
    std::string draw_bar(float value, int width);
    void handle_input();
    void load_current_track();
    void show_track_list();
    void cycle_theme();
};

#endif // TUI_INTERFACE_H