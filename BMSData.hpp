#ifndef BMSDATA_HPP
#define BMSDATA_HPP

#include <vector>
#include <string>
#include <cstdint>
#include <unordered_map>
#include "BgaManager.hpp"

struct BMSNote {
    int64_t x, y, l;
    double hit_ms = 0.0; // 【追加】描画・判定用の絶対時間
};

struct BMSLine {
    int64_t y;
    double hit_ms = 0.0; // 【追加】小節線の描画用の絶対時間
};

// --- 以下、変更なし ---
struct BMSSoundChannel {
    std::string name;
    std::vector<BMSNote> notes;
};

struct BPMEvent {
    int64_t y;
    double bpm;
};

struct StopEvent {
    int64_t y;           // 停止が発生するパルス位置
    double  duration_ms; // 停止時間 (ms)
};

struct BMSHeader {
    std::string title, artist, genre;
    std::string modeHint;
    double bpm;
    int resolution = 480;
    double min_bpm;
    double max_bpm;
    int totalNotes;
    std::string subtitle;
    std::string chartName;
    int level;
    double total;
    double judgeRank;
    std::string eyecatch;
    std::string banner;
    std::string preview;
    bool is7Key;
    std::string bga_video;
    int64_t bga_offset = 0;
};

class BMSData {
public:
    BMSHeader header;
    std::vector<BMSSoundChannel> sound_channels;
    std::vector<BMSLine> lines;
    std::vector<BPMEvent> bpm_events;
    std::vector<StopEvent> stop_events; // #STOP / ch09 による停止イベント
    std::unordered_map<int, std::string> bga_images;
    std::vector<BgaEvent> bga_events;
    std::vector<BgaEvent> layer_events;
    std::vector<BgaEvent> poor_events;
};

#endif
