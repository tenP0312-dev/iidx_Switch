#pragma once
#include <string>
#include <vector>

// ============================================================
//  段位認定モード データ定義
// ============================================================

enum class DanGaugeType { EASY, NORMAL, HARD, EX_HARD, DAN };

// DanGaugeType → Config::GAUGE_OPTION 値へのマッピング
inline int danGaugeToOption(DanGaugeType t) {
    switch (t) {
        case DanGaugeType::EASY:    return 1;
        case DanGaugeType::NORMAL:  return 0;
        case DanGaugeType::HARD:    return 3;
        case DanGaugeType::EX_HARD: return 4;
        case DanGaugeType::DAN:     return 5;
    }
    return 0;
}

struct DanSong {
    std::string path; // ROOT_PATH からの相対パス
};

struct DanCourse {
    std::string  id;
    std::string  name;
    bool         visible    = true;          // セレクト画面に表示するか
    DanGaugeType gaugeType  = DanGaugeType::NORMAL;
    double       startGauge = 100.0;         // 開始ゲージ (通常100)
    std::vector<DanSong> songs;              // 配列長 = 課程の曲数
};

struct DanData {
    std::vector<DanCourse> courses;
};
