#include "metadata_parser.h"
#include <algorithm>
#include <cstring>
#include <vector>

TrackMetadata MetadataParser::parse(const std::string& filepath) {
    TrackMetadata meta;
    
    // Try ID3v2 first (at beginning of file)
    if (parse_id3v2(filepath, meta)) {
        estimate_duration(filepath, meta);
        return meta;
    }
    
    // Try ID3v1 (at end of file)
    if (parse_id3v1(filepath, meta)) {
        estimate_duration(filepath, meta);
        return meta;
    }
    
    // Try FLAC/Vorbis comments
    if (parse_vorbis_comment(filepath, meta)) {
        estimate_duration(filepath, meta);
        return meta;
    }
    
    // Fallback to filename
    size_t last_slash = filepath.find_last_of("/\\");
    size_t last_dot = filepath.find_last_of('.');
    
    if (last_slash != std::string::npos && last_dot != std::string::npos) {
        meta.title = filepath.substr(last_slash + 1, last_dot - last_slash - 1);
    } else {
        meta.title = filepath;
    }
    
    meta.artist = "Unknown Artist";
    meta.album = "Unknown Album";
    
    estimate_duration(filepath, meta);
    return meta;
}

bool MetadataParser::parse_id3v2(const std::string& filepath, TrackMetadata& meta) {
    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open()) return false;
    
    // Read ID3v2 header
    char header[10];
    file.read(header, 10);
    
    if (file.gcount() != 10 || strncmp(header, "ID3", 3) != 0) {
        return false;
    }
    
    // Parse version
    uint8_t version = header[3];
    uint8_t revision = header[4];
    uint8_t flags = header[5];
    
    // Parse size (synchsafe integer)
    uint32_t size = 0;
    size |= (header[6] & 0x7F) << 21;
    size |= (header[7] & 0x7F) << 14;
    size |= (header[8] & 0x7F) << 7;
    size |= (header[9] & 0x7F);
    
    // Read tag data
    std::vector<char> tag_data(size);
    file.read(tag_data.data(), size);
    
    // Parse frames based on version
    if (version == 3 || version == 4) {
        parse_id3v2_frames(tag_data.data(), size, version, meta);
    }
    
    return !meta.title.empty() || !meta.artist.empty();
}

void MetadataParser::parse_id3v2_frames(const char* data, size_t size, uint8_t version, TrackMetadata& meta) {
    size_t pos = 0;
    
    while (pos + 10 < size) {
        // Frame header (10 bytes for v2.3/v2.4)
        char frame_id[5] = {0};
        memcpy(frame_id, data + pos, 4);
        
        if (frame_id[0] == 0) break; // Padding
        
        uint32_t frame_size = 0;
        if (version == 4) {
            // Synchsafe integer
            frame_size |= (data[pos + 4] & 0x7F) << 21;
            frame_size |= (data[pos + 5] & 0x7F) << 14;
            frame_size |= (data[pos + 6] & 0x7F) << 7;
            frame_size |= (data[pos + 7] & 0x7F);
        } else {
            // Regular integer
            frame_size |= (uint8_t)data[pos + 4] << 24;
            frame_size |= (uint8_t)data[pos + 5] << 16;
            frame_size |= (uint8_t)data[pos + 6] << 8;
            frame_size |= (uint8_t)data[pos + 7];
        }
        
        pos += 10; // Skip frame header
        
        if (pos + frame_size > size) break;
        
        // Extract text (skip encoding byte)
        std::string text;
        if (frame_size > 1) {
            uint8_t encoding = data[pos];
            text = extract_text(data + pos + 1, frame_size - 1, encoding);
        }
        
        // Map frame ID to metadata field
        if (strcmp(frame_id, "TIT2") == 0) meta.title = text;
        else if (strcmp(frame_id, "TPE1") == 0) meta.artist = text;
        else if (strcmp(frame_id, "TALB") == 0) meta.album = text;
        else if (strcmp(frame_id, "TYER") == 0 || strcmp(frame_id, "TDRC") == 0) meta.year = text;
        else if (strcmp(frame_id, "TCON") == 0) meta.genre = text;
        
        pos += frame_size;
    }
}

std::string MetadataParser::extract_text(const char* data, size_t size, uint8_t encoding) {
    std::string result;
    
    if (encoding == 0 || encoding == 3) {
        // ISO-8859-1 or UTF-8
        result = std::string(data, size);
    } else if (encoding == 1) {
        // UTF-16 with BOM
        // Simplified: just extract ASCII-compatible characters
        for (size_t i = 2; i < size; i += 2) {
            if (data[i + 1] == 0 && data[i] != 0) {
                result += data[i];
            }
        }
    }
    
    // Trim null terminators and whitespace
    size_t end = result.find_last_not_of("\0 \t\r\n");
    if (end != std::string::npos) {
        result = result.substr(0, end + 1);
    }
    
    return result;
}

bool MetadataParser::parse_id3v1(const std::string& filepath, TrackMetadata& meta) {
    std::ifstream file(filepath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return false;
    
    std::streampos file_size = file.tellg();
    if (file_size < 128) return false;
    
    // Read last 128 bytes
    file.seekg(-128, std::ios::end);
    char tag[128];
    file.read(tag, 128);
    
    if (file.gcount() != 128 || strncmp(tag, "TAG", 3) != 0) {
        return false;
    }
    
    // Extract fields
    meta.title = std::string(tag + 3, 30);
    meta.artist = std::string(tag + 33, 30);
    meta.album = std::string(tag + 63, 30);
    meta.year = std::string(tag + 93, 4);
    
    // Trim spaces
    auto trim = [](std::string& s) {
        s.erase(s.find_last_not_of(" \0\t\r\n") + 1);
    };
    
    trim(meta.title);
    trim(meta.artist);
    trim(meta.album);
    trim(meta.year);
    
    // Genre
    uint8_t genre_id = tag[127];
    meta.genre = get_id3v1_genre(genre_id);
    
    return true;
}

bool MetadataParser::parse_vorbis_comment(const std::string& filepath, TrackMetadata& meta) {
    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open()) return false;
    
    // Check for FLAC
    char flac_header[4];
    file.read(flac_header, 4);
    
    if (strncmp(flac_header, "fLaC", 4) != 0) {
        return false; // Not FLAC
    }
    
    // Read metadata blocks
    while (file.good()) {
        uint8_t header_byte;
        file.read((char*)&header_byte, 1);
        
        bool is_last = (header_byte & 0x80) != 0;
        uint8_t block_type = header_byte & 0x7F;
        
        uint32_t block_size = 0;
        file.read((char*)&block_size, 3);
        block_size = (block_size >> 16) | ((block_size & 0xFF00)) | ((block_size & 0xFF) << 16);
        
        if (block_type == 4) { // VORBIS_COMMENT
            parse_vorbis_block(file, block_size, meta);
            return true;
        } else {
            file.seekg(block_size, std::ios::cur);
        }
        
        if (is_last) break;
    }
    
    return false;
}

void MetadataParser::parse_vorbis_block(std::ifstream& file, uint32_t size, TrackMetadata& meta) {
    // Skip vendor string
    uint32_t vendor_len;
    file.read((char*)&vendor_len, 4);
    file.seekg(vendor_len, std::ios::cur);
    
    // Read comment count
    uint32_t comment_count;
    file.read((char*)&comment_count, 4);
    
    for (uint32_t i = 0; i < comment_count && file.good(); ++i) {
        uint32_t comment_len;
        file.read((char*)&comment_len, 4);
        
        if (comment_len > 1024) continue; // Sanity check
        
        std::string comment(comment_len, '\0');
        file.read(&comment[0], comment_len);
        
        size_t eq_pos = comment.find('=');
        if (eq_pos != std::string::npos) {
            std::string key = comment.substr(0, eq_pos);
            std::string value = comment.substr(eq_pos + 1);
            
            // Convert to uppercase for comparison
            std::transform(key.begin(), key.end(), key.begin(), ::toupper);
            
            if (key == "TITLE") meta.title = value;
            else if (key == "ARTIST") meta.artist = value;
            else if (key == "ALBUM") meta.album = value;
            else if (key == "DATE") meta.year = value;
            else if (key == "GENRE") meta.genre = value;
        }
    }
}

void MetadataParser::estimate_duration(const std::string& filepath, TrackMetadata& meta) {
    std::ifstream file(filepath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return;
    
    size_t file_size = file.tellg();
    
    // Rough estimation based on file size and typical bitrates
    // This is approximate - real duration requires full parsing
    int estimated_bitrate = 192000; // 192 kbps default
    
    std::string ext = filepath.substr(filepath.find_last_of('.') + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    
    if (ext == "flac") {
        estimated_bitrate = 800000; // FLAC typical
    } else if (ext == "wav") {
        estimated_bitrate = 1411000; // CD quality WAV
    }
    
    meta.duration_seconds = (file_size * 8) / estimated_bitrate;
    meta.bitrate = estimated_bitrate / 1000;
}

std::string MetadataParser::get_id3v1_genre(uint8_t id) {
    static const char* genres[] = {
        "Blues", "Classic Rock", "Country", "Dance", "Disco", "Funk", "Grunge",
        "Hip-Hop", "Jazz", "Metal", "New Age", "Oldies", "Other", "Pop", "R&B",
        "Rap", "Reggae", "Rock", "Techno", "Industrial", "Alternative", "Ska",
        "Death Metal", "Pranks", "Soundtrack", "Euro-Techno", "Ambient"
    };
    
    if (id < 27) return genres[id];
    return "Unknown";
}
