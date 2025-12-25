#include "tui_interface.h"
#include <iomanip>

TUIInterface::TUIInterface(Config &cfg,
                           std::shared_ptr<AudioEngine> audio,
                           std::shared_ptr<PlaylistManager> playlist,
                           std::shared_ptr<DJCueSystem> ma_device_job_thread_next)
    : config(cfg), audio_engine(audio), playlist_mgr(playlist), dj(dj), running(true)
{
    setup_terminal();
}

TUIInterface::~TUIInterface()
{
    restore_terminal();
}

void TUIInterface::run()
{
    clear_screen();
    print_header();

    while (running)
    {
        update_display();
        handle_input();
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

void TUIInterface::setup_terminal()
{
    tcgetattr(STDIN_FILENO, &old_term);
    new_term = old_term;
    new_term.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &new_term);
}

void TUIInterface::restore_terminal()
{
    tcsetattr(STDIN_FILENO, TCSANOW, &old_term);
}

void TUIInterface::clear_screen()
{
    std::cout << "\033[2J\033[H" << std::flush;
}

void TUIInterface::move_cursor(int row, int col)
{
    std::cout << "\033[" << row << ";" << col << "H" << std::flush;
}

void TUIInterface::print_header()
{
    std::cout << "╔════════════════════════════════════════════════════════════════╗\n";
    std::cout << "║           🎵 MUSIC STREAMING PLATFORM 🎵                       ║\n";
    std::cout << "╚════════════════════════════════════════════════════════════════╝\n";
    std::cout << "\n";
}

void TUIInterface::update_display()
{
    move_cursor(5, 1);

    std::cout << "Mode: " << config.get_mode_string() << "          \n";
    std::cout << "Theme: " << config.get_theme_string() << "          \n";
    std::cout << "\n";

    Track *current = playlist_mgr->get_current_track();
    if (current)
    {
        std::cout << "Now Playing: " << current->title << "          \n";
    }
    else
    {
        std::cout << "Now Playing: [No track loaded]          \n";
    }

    std::cout << "\n";
    std::cout << "Playlist: " << (playlist_mgr->get_current_index() + 1)
              << " / " << playlist_mgr->get_track_count() << "          \n";
    std::cout << "\n";

    // Audio levels
    FFTData fft = audio_engine->get_fft_data();
    std::cout << "\n";
    std::cout << "Bass:   " << draw_bar(fft.bass, 40) << "\n";
    std::cout << "Mid:    " << draw_bar(fft.mid, 40) << "\n";
    std::cout << "Treble: " << draw_bar(fft.treble, 40) << "\n";
    std::cout << "Energy: " << draw_bar(fft.energy, 40) << "\n";

    std::cout << "\n";
    print_controls();
}

void TUIInterface::print_controls()
{
    std::cout << "╔════════════════════════════════════════════════════════════════╗\n";
    std::cout << "║ Controls:                                                      ║\n";

    if (config.mode == PlaybackMode::CODER)
    {
        std::cout << "║   [1-9] Trigger Sample  [R] Record  [L] Loop                   ║\n";
        std::cout << "║   [Space] Play/Pause    [T] Theme                              ║\n";

        auto coder = audio_engine->get_coder_mode();
        if (coder->is_recording())
        {
            std::cout << "║   🔴 RECORDING                                                  ║\n";
        }
        if (coder->is_looping())
        {
            std::cout << "║   🔁 LOOP ACTIVE                                               ║\n";
        }
    }
    else
    {
        std::cout << "║   [Space] Play/Pause    [N] Next    [P] Previous               ║\n";
        std::cout << "║   [S] Shuffle           [L] List    [T] Theme                  ║\n";
        std::cout << "║   [M] Mute                                                    ║\n";

        if (config.mode == PlaybackMode::DJ)
        {
            std::cout << "║   [N] Cue Next          [C] Queue                              ║\n";
        }
    }

    std::cout << "║   [Esc] or [Q] Quit                                            ║\n";
    std::cout << "╚════════════════════════════════════════════════════════════════╝\n";
}

std::string TUIInterface::draw_bar(float value, int width)
{
    int filled = static_cast<int>(value * width);
    std::string bar = "[";
    for (int i = 0; i < width; ++i)
    {
        if (i < filled)
        {
            bar += "█";
        }
        else
        {
            bar += "░";
        }
    }
    bar += "]";
    return bar;
}

void TUIInterface::handle_input()
{
    fd_set fds;
    struct timeval tv;

    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);

    tv.tv_sec = 0;
    tv.tv_usec = 0;

    if (select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv) > 0)
    {
        char c;
        read(STDIN_FILENO, &c, 1);

        switch (c)
        {
        case ' ': // Play/Pause
            if (audio_engine->is_active())
            {
                audio_engine->stop();
            }
            else
            {
                audio_engine->start();
            }
            break;
        case 'c':
        case 'C': // Cue Next Track
            if ((&config)->mode != PlaybackMode::DJ)
                break;
            dj->get_next_cue();
        case 'k':
        case 'K': // Next
            if ((&config)->mode == PlaybackMode::DJ)
            {
                // dj->cue_next_track(); // todo - implement...
            }
        case 'n':
        case 'N': // Next
            playlist_mgr->next();
            load_current_track();
            break;

        case 'p':
        case 'P': // Previous
            playlist_mgr->previous();
            load_current_track();
            break;

        case 's':
        case 'S': // Shuffle
            playlist_mgr->shuffle();
            load_current_track();
            break;

        case 'l':
        case 'L': // List tracks or toggle loop
            if (config.mode == PlaybackMode::CODER)
            {
                audio_engine->get_coder_mode()->toggle_loop();
            }
            else
            {
                show_track_list();
            }
            break;

        case 't':
        case 'T': // Change theme
            cycle_theme();
            break;

        case 'm':
        case 'M': // Mute toggle
            audio_engine->set_muted(!audio_engine->is_muted());
            break;

        // Coder mode controls
        case '1':
        case '2':
        case '3':
        case '4':
        case '5':
        case '6':
        case '7':
        case '8':
        case '9':
            if (config.mode == PlaybackMode::CODER)
            {
                int sample_id = c - '1';
                audio_engine->get_coder_mode()->trigger_sample(sample_id);
            }
            break;

        case 'r':
        case 'R': // Record toggle
            if (config.mode == PlaybackMode::CODER)
            {
                auto coder = audio_engine->get_coder_mode();
                coder->set_recording(!coder->is_recording());
            }
            break;

        case 'q':
        case 'Q':
        case 27: // Escape, Q, or Escape key
            running = false;
            break;
        }
    }
}

void TUIInterface::load_current_track()
{
    Track *track = playlist_mgr->get_current_track();
    if (track)
    {
        audio_engine->load_track(track->filepath);
    }
}

void TUIInterface::show_track_list()
{
    clear_screen();
    std::cout << "╔════════════════════════════════════════════════════════════════╗\n";
    std::cout << "║                        TRACK LIST                              ║\n";
    std::cout << "╚════════════════════════════════════════════════════════════════╝\n\n";

    auto tracks = playlist_mgr->get_all_tracks();
    size_t current_idx = playlist_mgr->get_current_index();

    for (size_t i = 0; i < tracks.size() && i < 20; ++i)
    {
        if (i == current_idx)
        {
            std::cout << " ► ";
        }
        else
        {
            std::cout << "   ";
        }
        std::cout << std::setw(3) << (i + 1) << ". " << tracks[i].title << "\n";
    }

    if (tracks.size() > 20)
    {
        std::cout << "\n   ... and " << (tracks.size() - 20) << " more tracks\n";
    }

    std::cout << "\nPress any key to return...\n";

    char c;
    read(STDIN_FILENO, &c, 1);
    clear_screen();
    print_header();
}

void TUIInterface::cycle_theme()
{
    int theme_val = static_cast<int>(config.theme);
    theme_val = (theme_val + 1) % 3;
    config.theme = static_cast<VisualizerTheme>(theme_val);
}
