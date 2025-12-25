// metadata_parser.h - Complete ID3v1/ID3v2 and metadata parsing
#ifndef METADATA_PARSER_H
#define METADATA_PARSER_H

#include <string>
#include <fstream>
#include <cstring>
#include <algorithm>

struct TrackMetadata {
    std::string title;
    std::string artist;
    std::string album;
    std::string year;
    std::string genre;
    int duration_seconds;
    int bitrate;
    
    TrackMetadata() : duration_seconds(0), bitrate(0) {}
};

class MetadataParser {
public:
    static TrackMetadata parse(const std::string& filepath);
    
private:
    static bool parse_id3v2(const std::string& filepath, TrackMetadata& meta);
    static void parse_id3v2_frames(const char* data, size_t size, uint8_t version, TrackMetadata& meta);
    static std::string extract_text(const char* data, size_t size, uint8_t encoding);
    static bool parse_id3v1(const std::string& filepath, TrackMetadata& meta);
    static bool parse_vorbis_comment(const std::string& filepath, TrackMetadata& meta);
    static void parse_vorbis_block(std::ifstream& file, uint32_t size, TrackMetadata& meta);
    static void estimate_duration(const std::string& filepath, TrackMetadata& meta);
    static std::string get_id3v1_genre(uint8_t id);
};

#endif // METADATA_PARSER_H