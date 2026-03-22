#include "BmsLoader.hpp"
#include <SDL2/SDL.h>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <map>
#include <unordered_map>
#include <vector>
#include <string>
#include <cctype>
#include <random>

// ============================================================
//  定数
// ============================================================

// bmson のデフォルト resolution と統一する。
// ChartProjector は「1パルスあたりの時間 = 60000 / (bpm * resolution)」で
// y座標→ms変換するため、resolution が違うと再生速度がずれる。
static constexpr int64_t BMS_RESOLUTION = 480; // 1拍=480パルス、1小節=BMS_RESOLUTION*4=1920パルス

// ============================================================
//  内部ユーティリティ
// ============================================================

// 文字列の前後空白を除去
static std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

// 文字列を大文字化
static std::string toUpper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), ::toupper);
    return s;
}

// Base-36 2文字 → int  (00=0, 01=1, ..., 0Z=35, 10=36, ...)
// base62: 0-9=0-9, A-Z=10-35, a-z=36-61
// 無効文字は -1 を返す
static int base62(char hi, char lo) {
    auto val = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'A' && c <= 'Z') return c - 'A' + 10;
        if (c >= 'a' && c <= 'z') return c - 'a' + 36;
        return -1;
    };
    int h = val(hi), l = val(lo);
    if (h < 0 || l < 0) return -1;
    return h * 62 + l;
}

// ============================================================
//  BMS ファイル内部表現
// ============================================================

// メッセージチャンネル 1行分
struct BmsMessage {
    int     measure;  // 小節番号 0〜999
    int     channel;  // チャンネル番号 (0x01〜)
    std::string data; // base-36 ペア列（2文字×N）
};

// パース結果の中間表現
struct BmsRaw {
    // ヘッダ
    std::string title, artist, genre, subTitle;
    double  bpm        = 120.0;
    double  judgeRank  = 2.0;   // 0=VERY HARD 1=HARD 2=NORMAL 3=EASY
    double  total      = 100.0;
    int     player     = 1;     // 1=SP 2=Couple 3=DP
    int     playLevel  = 0;
    std::string preview;
    std::string banner;
    std::string stagefile;
    std::string videofile; // #VIDEOFILE (bemaniaDX拡張)

    // 拡張BPM テーブル (#BPMxx n)
    std::unordered_map<int, double> bpmTable;     // index(1〜1295) → bpm

    // STOP テーブル (#STOPxx n, 単位: 192分音符)
    std::unordered_map<int, double> stopTable;    // index → 192分音符数

    // BMP テーブル (#BMPxx filename)
    std::unordered_map<int, std::string> bmpTable; // index → ファイル名

    // LN 設定
    int     lnType = 1;   // 1=RDM, 2=MGQ
    int     lnObj  = -1;  // #LNOBJ インデックス (-1 = 未設定)
    int     difficulty = 0; // #DIFFICULTY (0=未設定, 1=BEGINNER, 2=NORMAL, 3=HYPER, 4=ANOTHER, 5=INSANE)

    // 時間変化 (channel 02 = measure length multiplier)
    std::map<int, double> measureScale; // measure → スケール倍率 (デフォルト 1.0)

    // メッセージ行
    std::vector<BmsMessage> messages;
};

// ============================================================
//  チャンネル → bmson x 座標変換
// ============================================================
// 標準BMS/BME 7key仕様:
// ch11=key1(x2), ch12=key2(x3), ch13=key3(x4), ch14=key4(x5), ch15=key5(x6)
// ch16=scratch(x1), ch17=free zone(未使用), ch18=key6(x7), ch19=key7(x8)
// LNch: ch51-59 → ch11-19 相当 (0x40を引く)
//       ch61-68 → ch11-18 相当 (0x50を引く)
// BGM : ch01 → x=0

static int channelToX(int ch) {
    // LN チャンネルを通常チャンネルに正規化
    if (ch >= 0x51 && ch <= 0x59) ch = ch - 0x40; // 51→11, ..., 59→19
    if (ch >= 0x61 && ch <= 0x68) ch = ch - 0x50; // 61→11, ..., 68→18

    // 標準BMS/BME 7key仕様:
    // ch11=key1, ch12=key2, ch13=key3, ch14=key4, ch15=key5
    // ch16=scratch, ch17=free zone(未使用), ch18=key6, ch19=key7
    switch (ch) {
        case 0x11: return 2;  // 1P key1
        case 0x12: return 3;  // 1P key2
        case 0x13: return 4;  // 1P key3
        case 0x14: return 5;  // 1P key4
        case 0x15: return 6;  // 1P key5
        case 0x16: return 1;  // 1P scratch
        // 0x17: free zone (未使用)
        case 0x18: return 7;  // 1P key6 (BME拡張)
        case 0x19: return 8;  // 1P key7 (BME拡張)
        case 0x01: return 0;  // BGM
        default:   return -1; // 無視
    }
}

static bool isPlayableChannel(int ch) {
    int norm = ch;
    if (norm >= 0x51 && norm <= 0x59) norm -= 0x40;
    if (norm >= 0x61 && norm <= 0x68) norm -= 0x50;
    // ch11-16(key1-5,scratch), ch18-19(key6-7) がプレイアブル
    // ch17(free zone)は除外
    return (norm >= 0x11 && norm <= 0x16) || (norm == 0x18) || (norm == 0x19);
}

static bool isLNChannel(int ch) {
    return (ch >= 0x51 && ch <= 0x59) || (ch >= 0x61 && ch <= 0x68);
}

static bool isBGMChannel(int ch) {
    return (ch == 0x01);
}

static bool isBPMChannel(int ch) {
    return (ch == 0x03); // 直接BPM (16進値)
}

static bool isExtBPMChannel(int ch) {
    return (ch == 0x08); // #BPMxx 参照
}

static bool isStopChannel(int ch) {
    return (ch == 0x09);
}

static bool isBgaChannel(int ch)   { return (ch == 0x04); }
static bool isLayerChannel(int ch) { return (ch == 0x06); }
static bool isPoorChannel(int ch)  { return (ch == 0x07); }

// 不可視オブジェチャンネル: ch31-36, ch38-39 (1P側)
// ch37 は FREE ZONE に相当するため除外
static bool isInvisibleChannel(int ch) {
    return (ch >= 0x31 && ch <= 0x36) || (ch == 0x38) || (ch == 0x39);
}

// ============================================================
//  テキストのパース
// ============================================================

// Shift-JIS バイト列を UTF-8 に変換する。
// SDL_iconv_string は SDL2 に内蔵されており Switch でも動作する。
// 変換に失敗した場合は元の文字列をそのまま返す（ASCII曲名等はそのまま通る）。
static std::string sjisToUtf8(const std::string& sjis) {
    if (sjis.empty()) return sjis;
    // SDL_iconv_string のエンコーディング名はプラットフォームによって異なる。
    // macOS の SDL2 は "SHIFT-JIS" を認識しない場合があるため複数名でフォールバックする。
    static const char* CANDIDATES[] = {
        "SHIFT-JIS", "SHIFT_JIS", "SJIS", "CP932", "MS932", nullptr
    };
    for (int i = 0; CANDIDATES[i]; ++i) {
        char* utf8 = SDL_iconv_string("UTF-8", CANDIDATES[i], sjis.c_str(), sjis.size() + 1);
        if (utf8) {
            std::string result(utf8);
            SDL_free(utf8);
            return result;
        }
    }
    return sjis; // 全て失敗したらそのまま返す
}

// ============================================================
//  chartName 決定ヘルパー
//  優先度: #DIFFICULTY タグ > ファイル名から推測 > #PLAYLEVEL から推測
// ============================================================
static std::string resolveChartName(int difficulty, int playLevel,
                                    const std::string& filePath) {
    // 1. #DIFFICULTY タグ（最優先）
    static const char* DIFF_NAMES[] = {
        "", "BEGINNER", "NORMAL", "HYPER", "ANOTHER", "INSANE"
    };
    if (difficulty >= 1 && difficulty <= 5)
        return DIFF_NAMES[difficulty];

    // 2. ファイル名末尾から推測（SPN/SPH/SPA 等）
    size_t slash = filePath.find_last_of("/\\");
    std::string stem = (slash != std::string::npos) ? filePath.substr(slash + 1) : filePath;
    size_t dot = stem.find_last_of('.');
    if (dot != std::string::npos) stem = stem.substr(0, dot);
    std::string up = stem;
    std::transform(up.begin(), up.end(), up.begin(), ::toupper);
    if (up.size() >= 3) {
        std::string t3 = up.substr(up.size() - 3);
        if (t3 == "SPB") return "BEGINNER";
        if (t3 == "SPN") return "NORMAL";
        if (t3 == "SPH") return "HYPER";
        if (t3 == "SPA") return "ANOTHER";
        if (t3 == "SPL") return "INSANE";
    }
    if (up.size() >= 2) {
        std::string t2 = up.substr(up.size() - 2);
        if (t2 == "_N") return "NORMAL";
        if (t2 == "_H") return "HYPER";
        if (t2 == "_A") return "ANOTHER";
    }

    // 3. #PLAYLEVEL から推測
    if (playLevel <= 3)  return "BEGINNER";
    if (playLevel <= 6)  return "NORMAL";
    if (playLevel <= 8)  return "HYPER";
    if (playLevel <= 11) return "ANOTHER";
    return "INSANE";
}

// ============================================================
//  #RANDOM プリプロセッサ
//
//  #RANDOM/#IF/#ENDIF 等の制御構文を処理し、
//  有効な行だけを残した文字列を返す。
//  parseBmsText / parseWavTable の前に適用することで
//  既存のパース処理を変更せず RANDOM に対応できる。
//
//  対応構文:
//    #RANDOM n / #RONDAM n  … 1〜n の乱数生成
//    #SETRANDOM n           … 固定値 n（テスト用）
//    #IF n                  … 生成値が n なら以降を有効に
//    #ELSE                  … IF が不一致のとき有効
//    #ELSEIF n              … 追加条件
//    #ENDIF / #END IF       … ブロック終端
//    #ENDRANDOM             … RANDOM ブロック終端（ネスト用）
//  入れ子を完全サポート（スタック方式）
// ============================================================

static std::string preprocessRandom(const std::string& text) {
    static std::mt19937 rng(std::random_device{}());

    std::istringstream iss(text);
    std::string result;
    result.reserve(text.size());

    struct RandomFrame {
        int  generatedValue;
        int  currentIf;   // -1=IFブロック外, 0=不一致, >0=マッチ, -2=ELSE有効
        bool matched;
        bool active;      // 親フレームが無効なら false
    };
    std::vector<RandomFrame> stack;

    std::string line;
    while (std::getline(iss, line)) {
        // BOM 除去
        if (line.size() >= 3 &&
            (unsigned char)line[0] == 0xEF &&
            (unsigned char)line[1] == 0xBB &&
            (unsigned char)line[2] == 0xBF) {
            line = line.substr(3);
        }
        std::string trimmed = trim(line);

        if (trimmed.empty() || trimmed[0] != '#') {
            // 制御行以外: 現在のスタック状態で出力判定
            bool out = true;
            for (const auto& f : stack) {
                if (!f.active || f.currentIf == 0) { out = false; break; }
            }
            if (out) { result += line; result += '\n'; }
            continue;
        }

        // コマンド名取得（大文字化）
        size_t sp = trimmed.find_first_of(" \t", 1);
        std::string cmd = toUpper(trimmed.substr(1, sp == std::string::npos ? std::string::npos : sp - 1));
        std::string val = (sp == std::string::npos) ? "" : trim(trimmed.substr(sp + 1));

        // #RANDOM n / #RONDAM n（誤字対応）
        if (cmd == "RANDOM" || cmd == "RONDAM") {
            int n = 1;
            try { n = std::stoi(val); } catch (...) {}
            if (n < 1) n = 1;
            int generated = std::uniform_int_distribution<int>(1, n)(rng);
            bool parentActive = true;
            for (const auto& f : stack) {
                if (!f.active || f.currentIf == 0) { parentActive = false; break; }
            }
            stack.push_back({generated, -1, false, parentActive});
            continue;
        }

        // #SETRANDOM n
        if (cmd == "SETRANDOM") {
            int n = 1;
            try { n = std::stoi(val); } catch (...) {}
            bool parentActive = true;
            for (const auto& f : stack) {
                if (!f.active || f.currentIf == 0) { parentActive = false; break; }
            }
            stack.push_back({n, -1, false, parentActive});
            continue;
        }

        // #IF n
        if (cmd == "IF") {
            int n = 0;
            try { n = std::stoi(val); } catch (...) {}
            if (!stack.empty()) {
                auto& f = stack.back();
                if (f.generatedValue == n && !f.matched) {
                    f.currentIf = n;
                    f.matched   = true;
                } else {
                    f.currentIf = 0;
                }
            }
            continue;
        }

        // #ELSEIF n
        if (cmd == "ELSEIF") {
            int n = 0;
            try { n = std::stoi(val); } catch (...) {}
            if (!stack.empty()) {
                auto& f = stack.back();
                if (!f.matched && f.generatedValue == n) {
                    f.currentIf = n;
                    f.matched   = true;
                } else {
                    f.currentIf = 0;
                }
            }
            continue;
        }

        // #ELSE
        if (cmd == "ELSE") {
            if (!stack.empty()) {
                auto& f = stack.back();
                if (!f.matched) {
                    f.currentIf = -2;
                    f.matched   = true;
                } else {
                    f.currentIf = 0;
                }
            }
            continue;
        }

        // #ENDIF / #END IF（誤字対応）
        if (cmd == "ENDIF" || (cmd == "END" && val == "IF")) {
            if (!stack.empty()) {
                stack.back().currentIf = -1;
            }
            continue;
        }

        // #ENDRANDOM
        if (cmd == "ENDRANDOM") {
            if (!stack.empty()) stack.pop_back();
            continue;
        }

        // 通常のコマンド行: スタック状態で出力判定
        bool output = true;
        for (const auto& f : stack) {
            if (!f.active || f.currentIf == 0) { output = false; break; }
        }
        if (output) {
            result += line;
            result += '\n';
        }
    }

    return result;
}

static void parseBmsText(const std::string& text, BmsRaw& raw) {
    std::istringstream iss(text);
    std::string line;
    while (std::getline(iss, line)) {
        // BOM 除去
        if (line.size() >= 3 &&
            (unsigned char)line[0] == 0xEF &&
            (unsigned char)line[1] == 0xBB &&
            (unsigned char)line[2] == 0xBF) {
            line = line.substr(3);
        }
        line = trim(line);
        if (line.empty() || line[0] != '#') continue;

        // メッセージ行: #XXXYY:ZZ...
        // 3桁 + 2桁 + ':'
        bool isMessage = false;
        if (line.size() >= 7) {
            bool threeDigits = std::isdigit((unsigned char)line[1]) &&
                               std::isdigit((unsigned char)line[2]) &&
                               std::isdigit((unsigned char)line[3]);
            bool colonAt6 = (line.size() > 6 && line[6] == ':');
            // ch は 16進2文字
            bool chValid  = std::isalnum((unsigned char)line[4]) &&
                            std::isalnum((unsigned char)line[5]);
            isMessage = threeDigits && chValid && colonAt6;
        }
        if (isMessage) {
            int measure = std::stoi(line.substr(1, 3));
            // チャンネル文字を大文字化してから解析
            char c4 = std::toupper((unsigned char)line[4]);
            char c5 = std::toupper((unsigned char)line[5]);
            auto hval = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                return -1;
            };
            int hi = hval(c4), lo = hval(c5);
            if (hi < 0 || lo < 0) continue;
            int channel = hi * 16 + lo;
            std::string data = line.substr(7);
            raw.messages.push_back({measure, channel, trim(data)});
            continue;
        }

        // ヘッダーコマンド
        // コマンド名と値を分離（空白またはタブで区切り）
        size_t sp = line.find_first_of(" \t", 1);
        std::string cmd   = toUpper(line.substr(1, sp == std::string::npos ? std::string::npos : sp - 1));
        std::string value = (sp == std::string::npos) ? "" : trim(line.substr(sp + 1));

        if      (cmd == "TITLE")    raw.title    = value;
        else if (cmd == "SUBTITLE") raw.subTitle = value;
        else if (cmd == "ARTIST")   raw.artist   = value;
        else if (cmd == "GENRE")    raw.genre    = value;
        else if (cmd == "BPM" && !value.empty()) {
            // #BPM n (通常BPM)
            // #BPMxx n (拡張BPM) … cmd は "BPMxx" になる
            if (cmd == "BPM") {
                try { raw.bpm = std::stod(value); } catch (...) {}
            }
        }
        else if (cmd.substr(0, 3) == "BPM" && cmd.size() > 3) {
            // #BPMxx n
            std::string idStr = cmd.substr(3);
            if (idStr.size() == 2) {
                int idx = base62(idStr[0], idStr[1]);
                if (idx > 0 && !value.empty()) {
                    try { raw.bpmTable[idx] = std::stod(value); } catch (...) {}
                }
            }
        }
        else if (cmd.substr(0, 4) == "STOP" && cmd.size() > 4) {
            std::string idStr = cmd.substr(4);
            if (idStr.size() == 2) {
                int idx = base62(idStr[0], idStr[1]);
                if (idx > 0 && !value.empty()) {
                    try { raw.stopTable[idx] = std::stod(value); } catch (...) {}
                }
            }
        }
        else if (cmd == "PLAYER")    { try { raw.player = std::stoi(value); } catch (...) {} }
        else if (cmd == "PLAYLEVEL") { try { raw.playLevel = std::stoi(value); } catch (...) {} }
        else if (cmd == "RANK")      { try { raw.judgeRank = std::stod(value); } catch (...) {} }
        else if (cmd == "TOTAL")     { try { raw.total = std::stod(value); } catch (...) {} }
        else if (cmd == "PREVIEW")   { raw.preview   = value; }
        else if (cmd == "BANNER")    { raw.banner    = value; }
        else if (cmd == "STAGEFILE") { raw.stagefile = value; }
        else if (cmd == "VIDEOFILE") { raw.videofile = value; } // bemaniaDX拡張
        else if (cmd.substr(0, 3) == "BMP" && cmd.size() == 5) {
            // #BMPxx filename
            std::string idStr = cmd.substr(3, 2);
            int idx = base62(idStr[0], idStr[1]);
            if (idx >= 0 && !value.empty()) {
                raw.bmpTable[idx] = value;
            }
        }
        else if (cmd == "LNTYPE")    { try { raw.lnType = std::stoi(value); } catch (...) {} }
        else if (cmd == "DIFFICULTY"){ try { raw.difficulty = std::stoi(value); } catch (...) {} }
        else if (cmd == "LNOBJ") {
            if (value.size() == 2) {
                int idx = base62(value[0], value[1]);
                if (idx > 0) raw.lnObj = idx;
            }
        }
        // #WAV, #BMP 等はここでは無視（SoundManagerが直接ファイルパスを参照する）
        // ただし sound_channels の "name" フィールドを埋めるため記録する
    }
}

// ============================================================
//  WAV テーブルのパース（sound_channels name 用）
// ============================================================
static void parseWavTable(const std::string& text,
                          std::unordered_map<int, std::string>& wavTable) {
    std::istringstream iss(text);
    std::string line;
    while (std::getline(iss, line)) {
        line = trim(line);
        if (line.size() < 6 || line[0] != '#') continue;
        std::string cmd = toUpper(line.substr(1, 5));
        if (cmd.substr(0, 3) != "WAV") continue;
        if (cmd.size() < 5) continue;
        std::string idStr = cmd.substr(3, 2);
        if (idStr.size() != 2) continue;
        int idx = base62(idStr[0], idStr[1]);
        if (idx <= 0) continue;
        size_t sp = line.find_first_of(" \t", 1);
        if (sp == std::string::npos) continue;
        std::string filename = trim(line.substr(sp + 1));
        if (!filename.empty()) wavTable[idx] = filename;
    }
}

// ============================================================
//  BmsRaw → BMSData 変換
// ============================================================


static BMSData convertToData(const BmsRaw& raw,
                             const std::unordered_map<int, std::string>& wavTable,
                             const std::string& filePath = "") {
    BMSData data;
    BMSHeader& h = data.header;

    h.title      = raw.title.empty() ? "Unknown" : sjisToUtf8(raw.title);
    h.subtitle   = sjisToUtf8(raw.subTitle);
    h.artist     = raw.artist.empty() ? "Unknown" : sjisToUtf8(raw.artist);
    h.genre      = sjisToUtf8(raw.genre);
    h.bpm        = raw.bpm;
    h.min_bpm    = raw.bpm;
    h.max_bpm    = raw.bpm;
    h.resolution = (int)BMS_RESOLUTION;
    h.total      = raw.total;
    h.judgeRank  = raw.judgeRank;
    h.level      = raw.playLevel;
    h.preview    = raw.preview;
    h.banner     = raw.banner;
    h.modeHint   = (raw.player == 3) ? "beat-14k" : "beat-7k";
    h.is7Key     = true; // 後で再判定
    h.chartName  = resolveChartName(raw.difficulty, raw.playLevel, filePath);

    // ------------------------------------
    // 各小節の開始 y 座標を計算する
    // ------------------------------------
    // まず最大小節番号を調べる
    int maxMeasure = 0;
    for (const auto& msg : raw.messages) {
        if (msg.measure > maxMeasure) maxMeasure = msg.measure;
    }
    maxMeasure += 2; // 余裕を持たせる

    // measureStartY[m] = 小節 m の開始 pulse
    std::vector<int64_t> measureStartY(maxMeasure + 1, 0);
    {
        int64_t y = 0;
        for (int m = 0; m < maxMeasure; ++m) {
            measureStartY[m] = y;
            double scale = 1.0;
            auto it = raw.measureScale.find(m);
            if (it != raw.measureScale.end()) scale = it->second;
            y += (int64_t)std::round(BMS_RESOLUTION * 4 * scale);
        }
        measureStartY[maxMeasure] = y;
    }

    // ------------------------------------
    // 小節線 (lines)
    // ------------------------------------
    for (int m = 0; m <= maxMeasure; ++m) {
        data.lines.push_back({measureStartY[m], 0.0});
    }

    // ------------------------------------
    // BPM イベントを先に収集して hit_ms を後で計算できるよう用意する
    // ------------------------------------
    data.bpm_events.push_back({0, raw.bpm});

    // ------------------------------------
    // sound_channels の初期化
    // max wav index を知るため wavTable を走査
    // ------------------------------------
    // wavIndex → sound_channels のインデックス
    std::unordered_map<int, size_t> wavToChannel;
    for (auto& [idx, name] : wavTable) {
        size_t ci = data.sound_channels.size();
        BMSSoundChannel ch;
        ch.name = name;
        data.sound_channels.push_back(ch);
        wavToChannel[idx] = ci;
    }
    // wavTable にない index が現れた場合に追加するラムダ
    auto getOrAddChannel = [&](int wavIdx) -> size_t {
        auto it = wavToChannel.find(wavIdx);
        if (it != wavToChannel.end()) return it->second;
        size_t ci = data.sound_channels.size();
        BMSSoundChannel ch;
        ch.name = ""; // ファイル名不明
        data.sound_channels.push_back(ch);
        wavToChannel[wavIdx] = ci;
        return ci;
    };

    // ------------------------------------
    // LN 処理のための状態
    // LNTYPE 1 (RDM): ch51-58 の非0 → start, 次の非0 → end
    // LNTYPE 2 (MGQ): ch51-58 の非0連続 → 1LN
    // LNOBJ: ch11-18 内で wavIdx==lnObj → LN end
    // ------------------------------------

    // LN開始ノートを一時保持: bmson x → {y, channelIndex, noteIndex}
    struct LnStart {
        int64_t startY;
        size_t  chIdx;
        size_t  noteIdx;
    };
    std::unordered_map<int, LnStart> lnOpenMap; // bmson_x → LnStart

    // LNOBJ 用: ch11-18 で wavIdx==lnObj が来たら直前ノートをLN化
    // 各レーン (bmson x) の最後のプレイアブルノートを記録
    struct LastNote {
        size_t chIdx;
        size_t noteIdx;
        int64_t y;
    };
    std::unordered_map<int, LastNote> lastNoteMap; // bmson_x → LastNote

    // ------------------------------------
    // メッセージを小節・位置順にソートして処理
    // ------------------------------------
    // まず measure 02 (小節スケール) を先行処理
    for (const auto& msg : raw.messages) {
        if (msg.channel == 0x02) {
            if (!msg.data.empty()) {
                try {
                    double scale = std::stod(msg.data);
                    if (scale > 0) {
                        const_cast<BmsRaw&>(raw).measureScale[msg.measure] = scale;
                    }
                } catch (...) {}
            }
        }
    }
    // measureStartY を再計算（02チャンネル反映後）
    {
        int64_t y = 0;
        for (int m = 0; m < maxMeasure; ++m) {
            measureStartY[m] = y;
            double scale = 1.0;
            auto it = raw.measureScale.find(m);
            if (it != raw.measureScale.end()) scale = it->second;
            y += (int64_t)std::round(BMS_RESOLUTION * 4 * scale);
        }
        measureStartY[maxMeasure] = y;
    }
    // data.lines を再構築
    data.lines.clear();
    for (int m = 0; m <= maxMeasure; ++m) {
        data.lines.push_back({measureStartY[m], 0.0});
    }

    // メッセージを（measure, position_in_measure）順に並べて処理
    // ただし BPM変化チャンネル(03,08) → STOP(09) → 通常 の順で処理
    // 簡単のため: 全メッセージを y 座標付きリストに展開 → y でソート
    struct NoteEvent {
        int64_t y;
        int     channel;
        int     wavIdx;  // base62 で取得したオブジェクトID
    };
    std::vector<NoteEvent> events;
    events.reserve(raw.messages.size() * 8);

    for (const auto& msg : raw.messages) {
        if (msg.channel == 0x02) continue; // 小節スケールは処理済み
        const std::string& d = msg.data;
        if (d.size() < 2 || d.size() % 2 != 0) continue;
        int slots = (int)(d.size() / 2);
        if (slots == 0) continue;

        double scale = 1.0;
        auto sit = raw.measureScale.find(msg.measure);
        if (sit != raw.measureScale.end()) scale = sit->second;
        int64_t measureLen = (int64_t)std::round(BMS_RESOLUTION * 4 * scale);
        int64_t startY = (msg.measure < (int)measureStartY.size())
                         ? measureStartY[msg.measure] : 0;

        for (int i = 0; i < slots; ++i) {
            char hi = d[i * 2];
            char lo = d[i * 2 + 1];
            int  idx = base62(hi, lo);
            if (idx == 0) continue; // 00 = rest

            int64_t y = startY + (int64_t)std::round((double)i / slots * measureLen);
            events.push_back({y, msg.channel, idx});
        }
    }

    // y でソート（同じ y は BPM → STOP → 通常の優先順）
    std::stable_sort(events.begin(), events.end(), [](const NoteEvent& a, const NoteEvent& b) {
        if (a.y != b.y) return a.y < b.y;
        // BPM 変化を先に
        bool aBpm = (a.channel == 0x03 || a.channel == 0x08);
        bool bBpm = (b.channel == 0x03 || b.channel == 0x08);
        if (aBpm != bBpm) return aBpm > bBpm;
        return false;
    });

    // ------------------------------------
    // イベント処理
    // ------------------------------------
    bool hasKey6or7 = false; // x7 or x8 が使われているか
    int  playableCount = 0;

    // BGA イベント収集用
    std::vector<BgaEvent> bgaEvents, layerEvents, poorEvents;

    // STOP イベント仮置き: {y, stopValue(192分音符数)}
    struct StopRaw { int64_t y; double stopValue; };
    std::vector<StopRaw> stopEventsRaw;

    for (const auto& ev : events) {
        int ch  = ev.channel;
        int idx = ev.wavIdx;

        // BPM 変化 (ch03 = 16進直接値)
        // ch03 の2文字は hex 表記（00〜FF）。
        // ノーツ展開時に base62 でidxを計算しているため、元の2文字を逆算してhex値に戻す。
        // base62(hi, lo)=idx → hi=idx/62, lo=idx%62 → hex値を得る
        if (ch == 0x03) {
            int b62hi = idx / 62;
            int b62lo = idx % 62;
            // 0-9 → そのまま, 10-15 → A-F (hexとして有効)
            // 16以上 → hex文字として無効（G-Z, a-z はch03では使用しない）
            auto hexDigit = [](int v) -> int {
                if (v >= 0 && v <= 15) return v;
                return -1;
            };
            int hexHi = hexDigit(b62hi);
            int hexLo = hexDigit(b62lo);
            double newBpm = (hexHi >= 0 && hexLo >= 0)
                            ? (double)(hexHi * 16 + hexLo)
                            : (double)idx; // フォールバック
            if (newBpm > 0) {
                data.bpm_events.push_back({ev.y, newBpm});
                if (newBpm < h.min_bpm) h.min_bpm = newBpm;
                if (newBpm > h.max_bpm) h.max_bpm = newBpm;
            }
            continue;
        }

        // 拡張 BPM 変化 (ch08 = #BPMxx 参照)
        if (ch == 0x08) {
            auto it = raw.bpmTable.find(idx);
            if (it != raw.bpmTable.end()) {
                double newBpm = it->second;
                if (newBpm > 0) {
                    data.bpm_events.push_back({ev.y, newBpm});
                    if (newBpm < h.min_bpm) h.min_bpm = newBpm;
                    if (newBpm > h.max_bpm) h.max_bpm = newBpm;
                }
            }
            continue;
        }

        // STOP チャンネル (ch09): stopTable を参照して収集
        if (ch == 0x09) {
            auto it = raw.stopTable.find(idx);
            if (it != raw.stopTable.end() && it->second > 0) {
                // duration_ms はこの時点のBPMが必要なので仮置きとして y と stopValue を記録
                // 後でbpm_eventsを使って変換する
                stopEventsRaw.push_back({ev.y, it->second});
            }
            continue;
        }

        // BGM チャンネル (ch01)
        if (ch == 0x01) {
            size_t ci = getOrAddChannel(idx);
            data.sound_channels[ci].notes.push_back({0, ev.y, 0, 0.0});
            continue;
        }

        // 不可視オブジェ (ch31-36, ch38-39)
        // 表示・判定・スコアなし。キー入力時に発音する。
        // ch3x → ch1x に正規化してレーンを取得し、x を負値として格納する。
        // PlayEngine::init で負値 x を検出して laneHiddenNotes に振り分ける。
        if (isInvisibleChannel(ch)) {
            int normCh = ch - 0x20; // 0x31→0x11, ..., 0x36→0x16, 0x38→0x18, 0x39→0x19
            int x = channelToX(normCh);
            if (x >= 1 && x <= 8) {
                size_t ci = getOrAddChannel(idx);
                data.sound_channels[ci].notes.push_back({-(int64_t)x, ev.y, 0, 0.0});
            }
            continue;
        }

        // BGA チャンネル (ch04 = base, ch06 = poor, ch07 = layer)
        if (isBgaChannel(ch)) {
            bgaEvents.push_back({ev.y, idx});
            continue;
        }
        if (isLayerChannel(ch)) {
            layerEvents.push_back({ev.y, idx});
            continue;
        }
        if (isPoorChannel(ch)) {
            poorEvents.push_back({ev.y, idx});
            continue;
        }

        // プレイアブル / LN チャンネル
        bool isLNCh = isLNChannel(ch);
        int  x      = channelToX(ch);
        if (x < 0) continue; // 未対応チャンネル

        // 7key 判定
        if (x == 7 || x == 8) hasKey6or7 = true;

        size_t ci = getOrAddChannel(idx);

        if (!isLNCh) {
            // ========== 通常チャンネル (ch11-18) ==========

            // LNOBJ: このノートが LN 終端か？
            if (raw.lnObj > 0 && idx == raw.lnObj) {
                // 直前ノートを LN の start として l を設定
                auto lit = lastNoteMap.find(x);
                if (lit != lastNoteMap.end()) {
                    auto& last = lit->second;
                    int64_t lnLen = ev.y - last.y;
                    if (lnLen > 0) {
                        data.sound_channels[last.chIdx].notes[last.noteIdx].l = lnLen;
                    }
                    lastNoteMap.erase(lit);
                }
                // LNOBJ ノート自体は BGM として発音 (x=0 として追加)
                data.sound_channels[ci].notes.push_back({0, ev.y, 0, 0.0});
                continue;
            }

            // 通常ノートを追加
            size_t noteIdx = data.sound_channels[ci].notes.size();
            data.sound_channels[ci].notes.push_back({x, ev.y, 0, 0.0});
            lastNoteMap[x] = {ci, noteIdx, ev.y};
            playableCount++;

        } else {
            // ========== LN チャンネル (ch51-58) ==========

            if (raw.lnType == 1) {
                // RDM: 奇数番目→start, 偶数番目→end (各レーン独立)
                auto oit = lnOpenMap.find(x);
                if (oit == lnOpenMap.end()) {
                    // LN start
                    size_t noteIdx = data.sound_channels[ci].notes.size();
                    data.sound_channels[ci].notes.push_back({x, ev.y, 0, 0.0});
                    lnOpenMap[x] = {ev.y, ci, noteIdx};
                    playableCount++;
                } else {
                    // LN end: start ノートの l を設定
                    auto& start = oit->second;
                    int64_t lnLen = ev.y - start.startY;
                    if (lnLen > 0) {
                        data.sound_channels[start.chIdx].notes[start.noteIdx].l = lnLen;
                    }
                    lnOpenMap.erase(oit);
                }
            } else {
                // MGQ: 連続非0 → 1LN
                // 同じ x が連続している間は LN 継続、途切れたら終了
                // (簡易実装: RDM と同様に start/end ペアとして扱う)
                auto oit = lnOpenMap.find(x);
                if (oit == lnOpenMap.end()) {
                    size_t noteIdx = data.sound_channels[ci].notes.size();
                    data.sound_channels[ci].notes.push_back({x, ev.y, 0, 0.0});
                    lnOpenMap[x] = {ev.y, ci, noteIdx};
                    playableCount++;
                } else {
                    auto& start = oit->second;
                    int64_t lnLen = ev.y - start.startY;
                    if (lnLen > 0) {
                        data.sound_channels[start.chIdx].notes[start.noteIdx].l = lnLen;
                    }
                    // MGQ では end が来ても再度 start になる可能性がある
                    // ここでは RDM と同じく close として扱う
                    lnOpenMap.erase(oit);
                }
            }
        }
    }

    // 未閉じ LN は捨てる（壊れた譜面対策）

    // BPM イベントをソート
    std::sort(data.bpm_events.begin(), data.bpm_events.end(),
              [](const BPMEvent& a, const BPMEvent& b){ return a.y < b.y; });

    // STOP イベントを duration_ms に変換して格納
    // 仕様: stopValue は「1小節を192分割した単位数」。
    // 1小節 = 4拍 = 4*(60000/bpm) ms なので
    // duration_ms = stopValue * 60000 / (bpm * 48)
    // y位置での BPM を bpm_events から求める。
    if (!stopEventsRaw.empty()) {
        std::sort(stopEventsRaw.begin(), stopEventsRaw.end(),
                  [](const StopRaw& a, const StopRaw& b){ return a.y < b.y; });
        for (const auto& sr : stopEventsRaw) {
            // sr.y 直前の BPM を取得
            double bpm = h.bpm;
            for (const auto& bev : data.bpm_events) {
                if (bev.y > sr.y) break;
                bpm = bev.bpm;
            }
            if (bpm <= 0) bpm = 120.0;
            double duration_ms = sr.stopValue * 60000.0 / (bpm * 48.0);
            data.stop_events.push_back({sr.y, duration_ms});
        }
    }

    // ヘッダー更新
    h.totalNotes = playableCount;
    h.total      = (raw.total > 0) ? raw.total : (double)playableCount;
    h.is7Key     = hasKey6or7;
    h.modeHint   = (raw.player == 3) ? "beat-14k" : (h.is7Key ? "beat-7k" : "beat-5k");

    // BGA: #VIDEOFILE が最優先。なければ bmpTable から動画を探す。
    // #VIDEOFILE
    if (!raw.videofile.empty()) {
        h.bga_video = raw.videofile;
    }

    // bmpTable → bga_images / bga_video に振り分け
    auto isVideoFile = [](const std::string& name) -> bool {
        if (name.size() < 4) return false;
        std::string low = name;
        std::transform(low.begin(), low.end(), low.begin(), ::tolower);
        return low.find(".mp4")  != std::string::npos ||
               low.find(".wmv")  != std::string::npos ||
               low.find(".avi")  != std::string::npos ||
               low.find(".mov")  != std::string::npos ||
               low.find(".m4v")  != std::string::npos ||
               low.find(".mpg")  != std::string::npos ||
               low.find(".mpeg") != std::string::npos;
    };
    for (auto& [id, filename] : raw.bmpTable) {
        if (isVideoFile(filename)) {
            // #VIDEOFILE が未設定のときだけ採用（最初の動画のみ）
            if (h.bga_video.empty()) {
                h.bga_video = filename;
            }
        } else {
            data.bga_images[id] = filename;
        }
    }

    // BGAイベントをソートして格納
    std::sort(bgaEvents.begin(),   bgaEvents.end(),   [](auto& a, auto& b){ return a.y < b.y; });
    std::sort(layerEvents.begin(), layerEvents.end(), [](auto& a, auto& b){ return a.y < b.y; });
    std::sort(poorEvents.begin(),  poorEvents.end(),  [](auto& a, auto& b){ return a.y < b.y; });

    // BGA video offset: BmsonLoader と同様に、動画の最初の表示イベントの y 座標を
    // bga_offset に設定する。ScenePlay がこれを元に videoOffsetMs を計算し、
    // 動画が BGA イベント発火タイミングから再生されるようにする。
    // #VIDEOFILE で指定された動画は beat0 から再生されるため対象外。
    if (!h.bga_video.empty() && raw.videofile.empty()) {
        int videoBmpId = -1;
        for (auto& [id, filename] : raw.bmpTable) {
            if (filename == h.bga_video) { videoBmpId = id; break; }
        }
        if (videoBmpId >= 0) {
            for (const auto& ev : bgaEvents) {
                if (ev.id == videoBmpId) {
                    h.bga_offset = ev.y;
                    break;
                }
            }
        }
    }

    data.bga_events   = std::move(bgaEvents);
    data.layer_events = std::move(layerEvents);
    data.poor_events  = std::move(poorEvents);

    return data;
}

// ============================================================
//  ファイル読み込み（Shift-JIS / UTF-8 両対応）
// ============================================================
// Shift-JIS は直接 std::string として保持する。
// タイトル等の描画は SDL_ttf が行うため文字コード変換は不要
// （SDL_ttf 側で Shift-JIS フォントを使うか、
//  実際には描画エンジンが文字列をそのまま渡す形式に依存する）。
// ここでは生バイト列としてそのまま読み込む。

static std::string readFileText(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) return "";
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// ============================================================
//  公開 API
// ============================================================

bool BmsLoader::isBmsFile(const std::string& path) {
    if (path.size() < 4) return false;
    std::string ext = toUpper(path.substr(path.size() - 4));
    // .bms .bme .bml は 4文字
    if (ext == ".BMS" || ext == ".BME" || ext == ".BML") return true;
    return false;
}

BMSData BmsLoader::load(const std::string& path,
                        std::function<void(float)> onProgress) {
    std::string text = readFileText(path);
    if (text.empty()) return BMSData{};

    // #RANDOM/#IF/#ENDIF 等の制御構文を処理し、有効な行だけを抽出
    std::string processed = preprocessRandom(text);

    BmsRaw raw;
    parseBmsText(processed, raw);

    // 02チャンネル (measure scale) を再スキャンして反映
    for (const auto& msg : raw.messages) {
        if (msg.channel == 0x02 && !msg.data.empty()) {
            try {
                double scale = std::stod(msg.data);
                if (scale > 0) raw.measureScale[msg.measure] = scale;
            } catch (...) {}
        }
    }

    std::unordered_map<int, std::string> wavTable;
    parseWavTable(processed, wavTable);

    if (onProgress) onProgress(0.5f);

    BMSData data = convertToData(raw, wavTable, path);

    // ファイルパスから rootDir を設定するためのメタ情報
    // (SoundManager が相対パスを解決できるよう、
    //  BGA と同様に sound_channels の name はファイル名のみ)
    // ※ SoundManager::preloadBoxIndex は bmsonName(=ステムファイル名) と
    //   rootPath でファイルを検索するため、ここでは name のみで十分。

    if (onProgress) onProgress(1.0f);
    return data;
}

BMSHeader BmsLoader::loadHeader(const std::string& path) {
    std::string text = readFileText(path);
    if (text.empty()) return BMSHeader{};

    // #RANDOM/#IF/#ENDIF 等の制御構文を処理
    std::string processed = preprocessRandom(text);

    BmsRaw raw;
    parseBmsText(processed, raw);

    // 02チャンネルを先行処理
    for (const auto& msg : raw.messages) {
        if (msg.channel == 0x02 && !msg.data.empty()) {
            try {
                double scale = std::stod(msg.data);
                if (scale > 0) raw.measureScale[msg.measure] = scale;
            } catch (...) {}
        }
    }

    // ヘッダー用に note 数と 7key 判定だけ行う
    bool hasKey6or7 = false;
    int  totalNotes = 0;
    double minBpm = raw.bpm, maxBpm = raw.bpm;

    for (const auto& msg : raw.messages) {
        int ch = msg.channel;
        // BPM 変化
        if (ch == 0x08) {
            // #BPMxx 参照はテーブルがないと値を取れないので、とりあえずスキップ
        } else if (ch == 0x03) {
            // do nothing for header
        }

        if (!isPlayableChannel(ch)) continue;
        const std::string& d = msg.data;
        if (d.size() < 2 || d.size() % 2 != 0) continue;
        int slots = (int)(d.size() / 2);
        for (int i = 0; i < slots; ++i) {
            char hi = d[i * 2];
            char lo = d[i * 2 + 1];
            int  idx = base62(hi, lo);
            if (idx == 0) continue;
            int x = channelToX(ch);
            if (x == 7 || x == 8) hasKey6or7 = true;
            totalNotes++;
        }
    }

    // 拡張 BPM テーブルを走査
    for (auto& [idx, b] : raw.bpmTable) {
        if (b < minBpm) minBpm = b;
        if (b > maxBpm) maxBpm = b;
    }

    BMSHeader h;
    h.title      = raw.title.empty() ? "Unknown" : sjisToUtf8(raw.title);
    h.subtitle   = sjisToUtf8(raw.subTitle);
    h.artist     = raw.artist.empty() ? "Unknown" : sjisToUtf8(raw.artist);
    h.genre      = sjisToUtf8(raw.genre);
    h.bpm        = raw.bpm;
    h.min_bpm    = minBpm;
    h.max_bpm    = maxBpm;
    h.resolution = (int)BMS_RESOLUTION;
    h.total      = (raw.total > 0) ? raw.total : (double)totalNotes;
    h.judgeRank  = raw.judgeRank;
    h.level      = raw.playLevel;
    h.preview    = raw.preview;
    h.banner     = raw.banner;
    h.is7Key     = hasKey6or7;
    h.totalNotes = totalNotes;
    h.modeHint   = (raw.player == 3) ? "beat-14k"
                 : (hasKey6or7 ? "beat-7k" : "beat-5k");
    h.chartName  = resolveChartName(raw.difficulty, raw.playLevel, path);
    return h;
}











