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
// 無効文字は -1 を返す
static int base36(char hi, char lo) {
    auto val = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'A' && c <= 'Z') return c - 'A' + 10;
        if (c >= 'a' && c <= 'z') return c - 'a' + 10;
        return -1;
    };
    int h = val(hi), l = val(lo);
    if (h < 0 || l < 0) return -1;
    return h * 36 + l;
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

    // 拡張BPM テーブル (#BPMxx n)
    std::unordered_map<int, double> bpmTable;     // index(1〜1295) → bpm

    // STOP テーブル (#STOPxx n, 単位: 192分音符)
    std::unordered_map<int, double> stopTable;    // index → 192分音符数

    // LN 設定
    int     lnType = 1;   // 1=RDM, 2=MGQ
    int     lnObj  = -1;  // #LNOBJ インデックス (-1 = 未設定)

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

// ============================================================
//  テキストのパース
// ============================================================

// Shift-JIS バイト列を UTF-8 に変換する。
// SDL_iconv_string は SDL2 に内蔵されており Switch でも動作する。
// 変換に失敗した場合は元の文字列をそのまま返す（ASCII曲名等はそのまま通る）。
static std::string sjisToUtf8(const std::string& sjis) {
    if (sjis.empty()) return sjis;
    char* utf8 = SDL_iconv_string("UTF-8", "SHIFT-JIS", sjis.c_str(), sjis.size() + 1);
    if (!utf8) return sjis; // 変換失敗時はそのまま返す
    std::string result(utf8);
    SDL_free(utf8);
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
                int idx = base36(idStr[0], idStr[1]);
                if (idx > 0 && !value.empty()) {
                    try { raw.bpmTable[idx] = std::stod(value); } catch (...) {}
                }
            }
        }
        else if (cmd.substr(0, 4) == "STOP" && cmd.size() > 4) {
            std::string idStr = cmd.substr(4);
            if (idStr.size() == 2) {
                int idx = base36(idStr[0], idStr[1]);
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
        else if (cmd == "LNTYPE")    { try { raw.lnType = std::stoi(value); } catch (...) {} }
        else if (cmd == "LNOBJ") {
            if (value.size() == 2) {
                int idx = base36(value[0], value[1]);
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
        int idx = base36(idStr[0], idStr[1]);
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
                             const std::unordered_map<int, std::string>& wavTable) {
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
    h.chartName  = "NORMAL";

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
        int     wavIdx;  // base36 で取得したオブジェクトID
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
            char hi = std::toupper((unsigned char)d[i * 2]);
            char lo = std::toupper((unsigned char)d[i * 2 + 1]);
            int  idx = base36(hi, lo);
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

    for (const auto& ev : events) {
        int ch  = ev.channel;
        int idx = ev.wavIdx;

        // BPM 変化 (ch03 = 16進直接値)
        if (ch == 0x03) {
            // idx は base36 で取得しているが ch03 は hex 値として使用
            // base36 で取得した値はそのまま使える (0〜255)
            double newBpm = (double)idx;
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

        // STOP チャンネル (ch09) は現在未実装（将来対応）
        if (ch == 0x09) continue;

        // BGM チャンネル (ch01)
        if (ch == 0x01) {
            size_t ci = getOrAddChannel(idx);
            data.sound_channels[ci].notes.push_back({0, ev.y, 0, 0.0});
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

    // ヘッダー更新
    h.totalNotes = playableCount;
    h.total      = (raw.total > 0) ? raw.total : (double)playableCount;
    h.is7Key     = hasKey6or7;
    h.modeHint   = (raw.player == 3) ? "beat-14k" : (h.is7Key ? "beat-7k" : "beat-5k");

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

    BmsRaw raw;
    parseBmsText(text, raw);

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
    parseWavTable(text, wavTable);

    if (onProgress) onProgress(0.5f);

    BMSData data = convertToData(raw, wavTable);

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

    BmsRaw raw;
    parseBmsText(text, raw);

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
            char hi = std::toupper((unsigned char)d[i * 2]);
            char lo = std::toupper((unsigned char)d[i * 2 + 1]);
            int  idx = base36(hi, lo);
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
    h.chartName  = "NORMAL";
    return h;
}

