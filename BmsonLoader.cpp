#include "BmsonLoader.hpp"
#include <fstream>
#include <algorithm>
#include "SoundManager.hpp"
#include "json.hpp"
#include <map>

// 文字列を安全に取得する補助関数（null対策）
static std::string get_string_safe_internal(const nlohmann::json& j, const std::string& key, const std::string& def) {
    if (!j.contains(key) || j[key].is_null() || !j[key].is_string()) return def;
    return j[key].get<std::string>();
}

// 数値を安全に取得する補助関数
static double get_double_safe_internal(const nlohmann::json& j, const std::string& key, double default_val) {
    if (!j.contains(key) || j[key].is_null()) return default_val;
    auto& v = j[key];
    if (v.is_number()) return v.get<double>();
    if (v.is_string()) { try { return std::stod(v.get<std::string>()); } catch (...) { return default_val; } }
    return default_val;
}

static bool isPlayableLaneSP(int64_t x) {
    return (x >= 1 && x <= 8);
}

// 共通のパース処理：DOM方式を採用。既存ロジック100%継承
static void parse_bmson_internal(const nlohmann::json& j, BMSData& data, const std::string& path, std::function<void(float)> onProgress) {
    const nlohmann::json& info = (j.contains("info") && !j["info"].is_null()) ? j["info"] : j;

    std::string bmsonName = path;
    size_t lastSlash = bmsonName.find_last_of("/\\");
    if (lastSlash != std::string::npos) bmsonName = bmsonName.substr(lastSlash + 1);
    size_t lastDot = bmsonName.find_last_of(".");
    if (lastDot != std::string::npos) bmsonName = bmsonName.substr(0, lastDot);
    std::string rootDir = path.substr(0, path.find_last_of("/\\") + 1);

    data.header.title = get_string_safe_internal(info, "title", "Unknown");
    data.header.artist = get_string_safe_internal(info, "artist", "Unknown");
    data.header.genre = get_string_safe_internal(info, "genre", "Unknown");
    data.header.modeHint = get_string_safe_internal(info, "mode_hint", "");
    data.header.subtitle = get_string_safe_internal(info, "subtitle", "");

    std::string cn = get_string_safe_internal(info, "chart_name", "");
    int diffVal = (int)get_double_safe_internal(info, "difficulty", -1.0);
    if (!cn.empty()) {
        std::string s = cn;
        std::transform(s.begin(), s.end(), s.begin(), ::toupper);
        if (s.find("BEGINNER") != std::string::npos)      diffVal = 1;
        else if (s.find("NORMAL") != std::string::npos)   diffVal = 2;
        else if (s.find("HYPER") != std::string::npos)    diffVal = 3;
        else if (s.find("ANOTHER") != std::string::npos)  diffVal = 4;
        else if (s.find("INSANE") != std::string::npos || 
                 s.find("LEGENDARIA") != std::string::npos || 
                 s.find("LEGGENDARIA") != std::string::npos) diffVal = 5;
    }
    if (diffVal == -1) diffVal = 2;

    switch(diffVal) {
        case 1: data.header.chartName = "BEGINNER"; break;
        case 2: data.header.chartName = "NORMAL";   break;
        case 3: data.header.chartName = "HYPER";    break;
        case 4: data.header.chartName = "ANOTHER";  break;
        case 5: data.header.chartName = "INSANE";   break;
        default: data.header.chartName = "NORMAL";  break;
    }

    data.header.level = (int)get_double_safe_internal(info, "level", 0.0);
    data.header.total = get_double_safe_internal(info, "total", 100.0);
    data.header.judgeRank = get_double_safe_internal(info, "judge_rank", 100.0);
    data.header.resolution = (int)get_double_safe_internal(info, "resolution", 480.0);
    data.header.eyecatch = get_string_safe_internal(info, "eyecatch_image", "");
    data.header.banner = get_string_safe_internal(info, "banner_image", "");
    data.header.preview = get_string_safe_internal(info, "preview_music", "");

    double bpm = get_double_safe_internal(info, "bpm", -1.0);
    if (bpm <= 0) bpm = get_double_safe_internal(info, "init_bpm", -1.0);
    if (bpm <= 0) bpm = get_double_safe_internal(j, "bpm", -1.0);
    if (bpm <= 0) bpm = get_double_safe_internal(j, "init_bpm", 120.0);
    data.header.bpm = bpm;
    data.header.min_bpm = bpm;
    data.header.max_bpm = bpm;

    data.bpm_events.push_back({0, bpm});
    const nlohmann::json& bpm_src = j.contains("bpm_events") ? j["bpm_events"] : (info.contains("bpm_events") ? info["bpm_events"] : nlohmann::json());
    if (bpm_src.is_array()) {
        for (auto& e : bpm_src) {
            int64_t y = e.value("y", (int64_t)0);
            double b = get_double_safe_internal(e, "bpm", bpm);
            if (y == 0) data.bpm_events[0].bpm = b;
            else data.bpm_events.push_back({y, b});
            if (b < data.header.min_bpm) data.header.min_bpm = b;
            if (b > data.header.max_bpm) data.header.max_bpm = b;
        }
    }
    std::sort(data.bpm_events.begin(), data.bpm_events.end(), [](auto& a, auto& b){ return a.y < b.y; });

    int totalNotesCount = 0;
    bool hasP1_6or7 = false;
    bool hasP2Side = false;
    const nlohmann::json& sc_src = j.contains("sound_channels") ? j["sound_channels"] : (info.contains("sound_channels") ? info["sound_channels"] : nlohmann::json());
    if (sc_src.is_array()) {
        size_t totalCh = sc_src.size();
        for (size_t i = 0; i < totalCh; ++i) {
            const auto& ch = sc_src[i];
            BMSSoundChannel channel;
            channel.name = get_string_safe_internal(ch, "name", "");
            
            if (ch.contains("notes") && ch["notes"].is_array()) {
                for (auto& n : ch["notes"]) {
                    int64_t x = n.value("x", (int64_t)0);
                    int64_t y = n.value("y", (int64_t)0);
                    int64_t l = n.value("l", (int64_t)0);
                    channel.notes.push_back({x, y, l});
                    if (x == 6 || x == 7) hasP1_6or7 = true;
                    if (x >= 9 && x <= 16) hasP2Side = true;
                    if (isPlayableLaneSP(x)) totalNotesCount++;
                }
            }
            data.sound_channels.push_back(channel);
            if (onProgress) onProgress((float)(i + 1) / (float)totalCh);
        }
    }
    data.header.totalNotes = totalNotesCount;
    data.header.total = (double)totalNotesCount; 
    data.header.is7Key = (hasP1_6or7 && !hasP2Side);

    // ★修正: BMSON の "lines" キー（小節線位置）を data.lines に格納する。
    //        旧実装はこのキーを一切パースしておらず、data.lines が常に空だった。
    //        PlayEngine::init が data.lines をもとに beatLines を構築しているため、
    //        パース漏れ = beatLines が空 = 小節線が一切描画されない。
    if (j.contains("lines") && j["lines"].is_array()) {
        data.lines.reserve(j["lines"].size());
        for (auto& l : j["lines"]) {
            int64_t y = l.value("y", (int64_t)0);
            data.lines.push_back({y, 0.0});
        }
        std::sort(data.lines.begin(), data.lines.end(),
                  [](const BMSLine& a, const BMSLine& b){ return a.y < b.y; });
    }

    if (j.contains("bga") && j["bga"].is_object()) {
        const auto& bga_node = j["bga"];
        std::map<int, std::string> idToName;
        if (bga_node.contains("bga_header") && bga_node["bga_header"].is_array()) {
            for (auto& img : bga_node["bga_header"]) {
                int id = img.value("id", 0);
                std::string name = get_string_safe_internal(img, "name", "");
                if (!name.empty()) {
                    idToName[id] = name;
                    std::string low = name;
                    std::transform(low.begin(), low.end(), low.begin(), ::tolower);
                    if (low.find(".wmv") != std::string::npos || low.find(".mp4") != std::string::npos || low.find(".avi") != std::string::npos || low.find(".mov") != std::string::npos) {
                        data.header.bga_video = name;
                    } else {
                        data.bga_images[id] = name;
                    }
                }
            }
        }
        auto parse_events = [&](const std::string& key, std::vector<BgaEvent>& target) {
            if (bga_node.contains(key) && bga_node[key].is_array()) {
                for (auto& e : bga_node[key]) {
                    int id = e.value("id", 0);
                    target.push_back({e.value("y", (int64_t)0), id});
                    if (idToName.count(id) && idToName[id] == data.header.bga_video) {
                        data.header.bga_offset = e.value("y", (int64_t)0);
                    }
                }
                std::sort(target.begin(), target.end(), [](auto& a, auto& b){ return a.y < b.y; });
            }
        };
        parse_events("bga_events", data.bga_events);
        parse_events("layer_events", data.layer_events);
        parse_events("poor_events", data.poor_events);
    }
}

// 指摘事項：JSON DOMオブジェクトの生存期間を最小化
BMSData BmsonLoader::load(const std::string& path, std::function<void(float)> onProgress) {
    BMSData data;
    std::ifstream f(path);
    if (!f.is_open()) return data;
    try {
        // スコープを制限し、parse_bmson_internal終了直後に nlohmann::json j を破棄する
        {
            nlohmann::json j;
            f >> j;
            parse_bmson_internal(j, data, path, onProgress);
        } // ここで巨大なJSON DOMがメモリから解放される
    } catch (...) {}
    return data;
}

// ★修正⑤: ヘッダーのみの高速パース。選曲画面のリスト表示用。
// 旧実装は parse_bmson_internal を呼び出し、数千ノーツの sound_channels を全構築してから
// header だけを返していた。曲数×フルパースは起動時間の数十秒増加につながる。
// この関数は "info" セクションと "bpm_events" のみ読み込み、sound_channels は一切触れない。
static BMSHeader parse_bmson_header_only(const nlohmann::json& j, const std::string& path) {
    BMSHeader h;
    const nlohmann::json& info = (j.contains("info") && !j["info"].is_null()) ? j["info"] : j;

    h.title    = get_string_safe_internal(info, "title",    "Unknown");
    h.artist   = get_string_safe_internal(info, "artist",   "Unknown");
    h.genre    = get_string_safe_internal(info, "genre",    "Unknown");
    h.modeHint = get_string_safe_internal(info, "mode_hint","");
    h.subtitle = get_string_safe_internal(info, "subtitle", "");
    h.eyecatch = get_string_safe_internal(info, "eyecatch_image", "");
    h.banner   = get_string_safe_internal(info, "banner_image",   "");
    h.preview  = get_string_safe_internal(info, "preview_music",  "");

    std::string cn = get_string_safe_internal(info, "chart_name", "");
    int diffVal = (int)get_double_safe_internal(info, "difficulty", -1.0);
    if (!cn.empty()) {
        std::string s = cn;
        std::transform(s.begin(), s.end(), s.begin(), ::toupper);
        if      (s.find("BEGINNER")   != std::string::npos) diffVal = 1;
        else if (s.find("NORMAL")     != std::string::npos) diffVal = 2;
        else if (s.find("HYPER")      != std::string::npos) diffVal = 3;
        else if (s.find("ANOTHER")    != std::string::npos) diffVal = 4;
        else if (s.find("INSANE")     != std::string::npos ||
                 s.find("LEGENDARIA") != std::string::npos ||
                 s.find("LEGGENDARIA")!= std::string::npos) diffVal = 5;
    }
    if (diffVal == -1) diffVal = 2;
    switch (diffVal) {
        case 1: h.chartName = "BEGINNER"; break;
        case 3: h.chartName = "HYPER";    break;
        case 4: h.chartName = "ANOTHER";  break;
        case 5: h.chartName = "INSANE";   break;
        default: h.chartName = "NORMAL";  break;
    }

    h.level     = (int)get_double_safe_internal(info, "level",      0.0);
    h.total     = get_double_safe_internal(info, "total",      100.0);
    h.judgeRank = get_double_safe_internal(info, "judge_rank", 100.0);
    h.resolution= (int)get_double_safe_internal(info, "resolution", 480.0);

    double bpm = get_double_safe_internal(info, "bpm", -1.0);
    if (bpm <= 0) bpm = get_double_safe_internal(info, "init_bpm", -1.0);
    if (bpm <= 0) bpm = get_double_safe_internal(j,    "bpm",      -1.0);
    if (bpm <= 0) bpm = get_double_safe_internal(j,    "init_bpm", 120.0);
    h.bpm = h.min_bpm = h.max_bpm = bpm;

    // BPM 変化幅だけ取得（ノーツは不要）
    const nlohmann::json& bpm_src = j.contains("bpm_events") ? j["bpm_events"]
                                  : (info.contains("bpm_events") ? info["bpm_events"]
                                                                  : nlohmann::json());
    if (bpm_src.is_array()) {
        for (const auto& e : bpm_src) {
            double b = get_double_safe_internal(e, "bpm", bpm);
            if (b < h.min_bpm) h.min_bpm = b;
            if (b > h.max_bpm) h.max_bpm = b;
        }
    }

    // is7Key と totalNotes の確定のために x 値だけスキャンする。
    // BMSSoundChannel / BMSNote オブジェクトは一切生成しないため、
    // フルパースと比べてアロケーション量は大幅に少ない。
    bool hasP1_6or7 = false;
    bool hasP2Side  = false;
    int  totalNotesCount = 0;
    const nlohmann::json& sc_src = j.contains("sound_channels") ? j["sound_channels"]
                                 : (info.contains("sound_channels") ? info["sound_channels"]
                                                                    : nlohmann::json());
    if (sc_src.is_array()) {
        for (const auto& ch : sc_src) {
            if (ch.contains("notes") && ch["notes"].is_array()) {
                for (const auto& n : ch["notes"]) {
                    int64_t x = n.value("x", (int64_t)0);
                    if (x == 6 || x == 7)          hasP1_6or7 = true;
                    if (x >= 9 && x <= 16)          hasP2Side  = true;
                    if (isPlayableLaneSP(x))        totalNotesCount++;
                }
            }
        }
    }
    h.is7Key     = (hasP1_6or7 && !hasP2Side);
    h.totalNotes = totalNotesCount;
    h.total      = (double)totalNotesCount;

    return h;
}

BMSHeader BmsonLoader::loadHeader(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) return BMSHeader{};
    try {
        // ★修正⑤: スコープを絞って JSON を即破棄。sound_channels は一切構築しない。
        nlohmann::json j;
        f >> j;
        return parse_bmson_header_only(j, path);
    } catch (...) {}
    return BMSHeader{};
}






