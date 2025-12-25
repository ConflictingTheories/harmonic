cmake_minimum_required(VERSION 3.15)
project(MusicStreamPlatform VERSION 1.0.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Set policy to suppress CMP0167 warnings - use custom FindBoost
if(POLICY CMP0167)
    cmake_policy(SET CMP0167 OLD)
endif()

# Set Boost root for Homebrew installation
set(Boost_NO_BOOST_CMAKE ON)
set(Boost_ROOT "/opt/homebrew/opt/boost")
set(Boost_USE_STATIC_LIBS OFF)
set(Boost_USE_MULTITHREADED ON)
set(Boost_USE_STATIC_RUNTIME OFF)

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

# WebSocket++ support is optional (disabled by default due to compatibility issues with newer Boost)
# To enable: use -DENABLE_WEBSOCKET=ON when running cmake
option(ENABLE_WEBSOCKET "Enable WebSocket support (requires compatible Boost/WebSocketPP)" OFF)

if(ENABLE_WEBSOCKET)
    find_path(WEBSOCKETPP_INCLUDE_DIR websocketpp/server.hpp 
        PATHS 
            /opt/homebrew/opt/websocketpp/include
            /usr/include
            /usr/local/include
    )
    
    if(WEBSOCKETPP_INCLUDE_DIR)
        message(STATUS "✓ WebSocketPP headers found - WebSocket support ENABLED")
        include_directories(${WEBSOCKETPP_INCLUDE_DIR})
        add_definitions(-DHAS_WEBSOCKETPP)
    else()
        message(WARNING "WebSocketPP headers not found")
    endif()
else()
    message(STATUS "⊘ WebSocket support DISABLED (HTTP streaming via libshout enabled)")
endif()

# Check for Boost (required for WebSocket++)
# Try to find Boost with specific component names
find_package(Boost 1.65.0 COMPONENTS system thread QUIET)

# If components not found, try simpler find without REQUIRED
if(NOT Boost_FOUND)
    find_package(Boost 1.65.0 QUIET)
endif()

# If still not found, try with MODULE mode
if(NOT Boost_FOUND)
    find_package(Boost NO_MODULE QUIET)
endif()

if(Boost_FOUND)
    message(STATUS "✓ Boost found - WebSocket support AVAILABLE (currently disabled)")
    message(STATUS "  Boost include: ${Boost_INCLUDE_DIRS}")
    message(STATUS "  Boost libraries: ${Boost_LIBRARIES}")
    include_directories(${Boost_INCLUDE_DIRS})
    # Only define HAS_WEBSOCKETPP if ENABLE_WEBSOCKET is ON
    if(ENABLE_WEBSOCKET)
        add_definitions(-DHAS_WEBSOCKETPP)
    endif()
else()
    # Try manual fallback for Homebrew
    find_path(BOOST_INCLUDE_DIR boost/version.hpp PATHS /opt/homebrew/opt/boost/include)
    if(BOOST_INCLUDE_DIR)
        message(STATUS "✓ Boost found via manual search - WebSocket support AVAILABLE")
        include_directories(${BOOST_INCLUDE_DIR})
        if(ENABLE_WEBSOCKET)
            add_definitions(-DHAS_WEBSOCKETPP)
        endif()
        set(Boost_FOUND TRUE)
    else()
        message(WARNING "Boost not found - WebSocket not available")
    endif()
endif()

# Find pkg-config for system libraries
find_package(PkgConfig REQUIRED)

# Find libshout (streaming support)
pkg_check_modules(LIBSHOUT REQUIRED shout)
if(LIBSHOUT_FOUND)
    message(STATUS "✓ libshout found - streaming ENABLED")
    include_directories(${LIBSHOUT_INCLUDE_DIRS})
    link_directories(${LIBSHOUT_LIBRARY_DIRS})
else()
    message(FATAL_ERROR "✗ libshout not found! Install via: brew install libshout")
endif()

# Find libvorbis and libvorbisenc (OGG encoding)
pkg_check_modules(LIBVORBIS REQUIRED vorbis vorbisenc)
if(LIBVORBIS_FOUND)
    message(STATUS "✓ libvorbis found - OGG encoding ENABLED")
    include_directories(${LIBVORBIS_INCLUDE_DIRS})
    link_directories(${LIBVORBIS_LIBRARY_DIRS})
else()
    message(FATAL_ERROR "✗ libvorbis not found! Install via: brew install libvorbis")
endif()

# Find libogg (OGG container format)
pkg_check_modules(LIBOGG REQUIRED ogg)
if(LIBOGG_FOUND)
    message(STATUS "✓ libogg found - OGG container support ENABLED")
    include_directories(${LIBOGG_INCLUDE_DIRS})
    link_directories(${LIBOGG_LIBRARY_DIRS})
else()
    message(FATAL_ERROR "✗ libogg not found! Install via: brew install libogg")
endif()

# Find lame (MP3 encoding)
find_path(LAME_INCLUDE_DIR lame/lame.h PATHS /opt/homebrew/include)
find_library(LAME_LIBRARY NAMES mp3lame PATHS /opt/homebrew/lib)
if(LAME_INCLUDE_DIR AND LAME_LIBRARY)
    message(STATUS "✓ lame found - MP3 encoding ENABLED")
    include_directories(${LAME_INCLUDE_DIR})
    link_libraries(${LAME_LIBRARY})
else()
    message(FATAL_ERROR "✗ lame not found! Install via: brew install lame")
endif()

# Source files
set(SOURCES
    src/main.cpp
    src/config.cpp
    src/audio_engine.cpp
    src/playlist_manager.cpp
    src/network_server.cpp
    src/tui_interface.cpp
    src/metadata_parser.cpp
    src/dj_cue_system.cpp
    src/coder_mode.cpp
    src/fft.cpp
    src/miniaudio_impl.cpp
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

# Boost for WebSocket++ (already found above)
target_link_libraries(${PROJECT_NAME} PRIVATE ${Boost_LIBRARIES})

# Link libshout
if(LIBSHOUT_FOUND)
    target_link_libraries(${PROJECT_NAME} PRIVATE ${LIBSHOUT_LIBRARIES})
else()
    # Fallback for macOS/Homebrew
    find_library(SHOUT_LIB shout)
    if(SHOUT_LIB)
        target_link_libraries(${PROJECT_NAME} PRIVATE ${SHOUT_LIB})
    endif()
endif()

# Link Vorbis libraries for OGG encoding
if(LIBVORBIS_FOUND)
    target_link_libraries(${PROJECT_NAME} PRIVATE ${LIBVORBIS_LIBRARIES})
else()
    find_library(VORBIS_LIB vorbis)
    find_library(VORBISENC_LIB vorbisenc)
    if(VORBIS_LIB AND VORBISENC_LIB)
        target_link_libraries(${PROJECT_NAME} PRIVATE ${VORBIS_LIB} ${VORBISENC_LIB})
    endif()
endif()

# Link libogg for OGG container
if(LIBOGG_FOUND)
    target_link_libraries(${PROJECT_NAME} PRIVATE ${LIBOGG_LIBRARIES})
else()
    find_library(OGG_LIB ogg)
    if(OGG_LIB)
        target_link_libraries(${PROJECT_NAME} PRIVATE ${OGG_LIB})
    endif()
endif()

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