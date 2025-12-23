cmake_minimum_required(VERSION 3.15)
project(MusicStreamPlatform VERSION 1.0.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Platform detection
if(WIN32)
    add_definitions(-D_WIN32_WINNT=0x0600)
endif()

# Download miniaudio header
set(MINIAUDIO_URL "https://raw.githubusercontent.com/mackron/miniaudio/master/miniaudio.h")
set(MINIAUDIO_PATH "${CMAKE_CURRENT_SOURCE_DIR}/include/miniaudio.h")
if(NOT EXISTS "${MINIAUDIO_PATH}")
    message(STATUS "Downloading miniaudio...")
    file(DOWNLOAD "${MINIAUDIO_URL}" "${MINIAUDIO_PATH}" SHOW_PROGRESS)
endif()

# Download dr_libs for MP3/FLAC
set(DR_MP3_URL "https://raw.githubusercontent.com/mackron/dr_libs/master/dr_mp3.h")
set(DR_FLAC_URL "https://raw.githubusercontent.com/mackron/dr_libs/master/dr_flac.h")
set(DR_WAV_URL "https://raw.githubusercontent.com/mackron/dr_libs/master/dr_wav.h")

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/include/dr_mp3.h")
    file(DOWNLOAD "${DR_MP3_URL}" "${CMAKE_CURRENT_SOURCE_DIR}/include/dr_mp3.h")
endif()
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/include/dr_flac.h")
    file(DOWNLOAD "${DR_FLAC_URL}" "${CMAKE_CURRENT_SOURCE_DIR}/include/dr_flac.h")
endif()
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/include/dr_wav.h")
    file(DOWNLOAD "${DR_WAV_URL}" "${CMAKE_CURRENT_SOURCE_DIR}/include/dr_wav.h")
endif()

# Source files
set(SOURCES
    src/main.cpp
    src/audio_engine.cpp
    src/playlist_manager.cpp
    src/network_server.cpp
    src/tui_interface.cpp
    src/metadata_parser.cpp
    src/dj_cue_system.cpp
    src/coder_mode.cpp
    src/fft.cpp
)

set(HEADERS
    include/config.h
    include/audio_engine.h
    include/playlist_manager.h
    include/network_server.h
    include/tui_interface.h
    include/metadata_parser.h
    include/dj_cue_system.h
    include/coder_mode.h
    include/fft.h
    include/miniaudio.h
    include/dr_mp3.h
    include/dr_flac.h
    include/dr_wav.h
)

add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS})

target_include_directories(${PROJECT_NAME} PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Platform-specific libraries
find_package(Threads REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE Threads::Threads)

if(UNIX AND NOT APPLE)
    target_link_libraries(${PROJECT_NAME} PRIVATE m pthread dl)
    # ALSA for Linux
    find_library(ASOUND_LIB asound)
    if(ASOUND_LIB)
        target_link_libraries(${PROJECT_NAME} PRIVATE ${ASOUND_LIB})
    endif()
endif()

if(APPLE)
    target_link_libraries(${PROJECT_NAME} PRIVATE 
        "-framework CoreAudio"
        "-framework AudioToolbox"
        "-framework AudioUnit"
        "-framework CoreFoundation"
    )
endif()

if(WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE winmm ws2_32)
endif()

# Compiler flags
if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /W4 /WX-)
else()
    target_compile_options(${PROJECT_NAME} PRIVATE 
        -Wall -Wextra -Wpedantic
        -Wno-unused-parameter
        -Wno-missing-field-initializers
    )
endif()

# Optimization
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    if(MSVC)
        target_compile_options(${PROJECT_NAME} PRIVATE /O2)
    else()
        target_compile_options(${PROJECT_NAME} PRIVATE -O3)
    endif()
endif()

# Create music directory
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/music")
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/playlists")

# Install
install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION bin)
install(FILES config.example DESTINATION share/${PROJECT_NAME})

# Create example config
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/config.txt"
"# Music Streaming Platform Configuration
mode=radio
theme=cyberpunk
web_port=8080
stream_port=8081
sample_rate=44100
buffer_size=512
music_directory=./music
")

message(STATUS "")
message(STATUS "═══════════════════════════════════════════")
message(STATUS "  Music Streaming Platform")
message(STATUS "═══════════════════════════════════════════")
message(STATUS "  Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "  Install Prefix: ${CMAKE_INSTALL_PREFIX}")
message(STATUS "═══════════════════════════════════════════")
message(STATUS "")