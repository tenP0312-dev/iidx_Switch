#include "SongManager.hpp"
#include "BmsonLoader.hpp"
#include "BmsLoader.hpp"
#include "Config.hpp"
#include "ScoreManager.hpp"
#include "SceneSelect.hpp" 
#include "VirtualFolderManager.hpp" // 追加
#include "NoteRenderer.hpp" // 追加: 描画用
#include <dirent.h>
#include <algorithm>
#include <fstream>
#include <map>
#include <iostream>
#include <cmath>
#include <cstdio>  
#include <unistd.h> 
#include <filesystem> 

namespace fs = std::filesystem;

// 静的メンバ変数の実体定義
std::map<std::string, SongGroup> SongManager::folderCustomCache;

/**
 * フォルダカスタム設定ファイルをパースする (SongManagerメンバ関数として実装)
 */
void SongManager::parseFolderCustom(const fs::path& folderPath, const std::string& folderName, SongGroup& group) {
    // --- キャッシュチェック ---
    std::string pathKey = folderPath.string();
    if (folderCustomCache.count(pathKey)) {
        const auto& cached = folderCustomCache[pathKey];
        group.customLogo = cached.customLogo;
        group.customFont = cached.customFont;
        group.customSE   = cached.customSE;
        if (!cached.title.empty() && cached.title != folderName) {
            group.title = cached.title;
        }
        return; 
    }

    if (fs::exists(folderPath / "_logo.png")) group.customLogo = (folderPath / "_logo.png").string();
    else if (fs::exists(folderPath / "_logo.jpg")) group.customLogo = (folderPath / "_logo.jpg").string();
    
    if (fs::exists(folderPath / "_custom.ttf")) group.customFont = (folderPath / "_custom.ttf").string();
    else if (fs::exists(folderPath / "_custom.ttc")) group.customFont = (folderPath / "_custom.ttc").string();

    if (fs::exists(folderPath / "_open.wav")) group.customSE = (folderPath / "_open.wav").string();

    fs::path customPath = "";
    bool isExternalConfig = false;

    if (fs::exists(folderPath.parent_path() / (folderName + "_custom.txt"))) {
        customPath = folderPath.parent_path() / (folderName + "_custom.txt");
        isExternalConfig = true; 
    } else if (fs::exists(folderPath / "custom.txt")) {
        customPath = folderPath / "custom.txt";
    } else if (fs::exists(folderPath / "config.txt")) {
        customPath = folderPath / "config.txt";
    }

    if (!customPath.empty() && fs::exists(customPath)) {
        std::ifstream ifs(customPath);
        if (ifs) {
            std::string line;
            while (std::getline(ifs, line)) {
                if (line.empty() || line[0] == '#') continue;
                size_t pos = line.find('=');
                if (pos == std::string::npos) continue;

                std::string key = line.substr(0, pos);
                std::string value = line.substr(pos + 1);

                auto trim = [](std::string& s) {
                    s.erase(0, s.find_first_not_of(" \n\r\t"));
                    s.erase(s.find_last_not_of(" \n\r\t") + 1);
                };
                trim(key); trim(value);

                if (value.empty()) continue;

                if (key == "title") {
                    group.title = value;
                    continue;
                }

                fs::path resolvedPath;
                if (isExternalConfig) {
                    if (fs::exists(folderPath / value)) resolvedPath = folderPath / value;
                    else if (fs::exists(folderPath.parent_path() / value)) resolvedPath = folderPath.parent_path() / value;
                } else {
                    resolvedPath = folderPath / value;
                }

                if (key == "font") {
                    if (!resolvedPath.empty() && fs::exists(resolvedPath)) {
                        group.customFont = resolvedPath.string();
                    } else {
                        fs::path commonFontPath = fs::path(Config::ROOT_PATH) / "fonts" / value;
                        if (fs::exists(commonFontPath)) group.customFont = commonFontPath.string();
                    }
                }
                else if (key == "logo") {
                    if (!resolvedPath.empty() && fs::exists(resolvedPath)) group.customLogo = resolvedPath.string();
                }
                else if (key == "soundeffect") {
                    if (!resolvedPath.empty() && fs::exists(resolvedPath)) group.customSE = resolvedPath.string();
                }
            }
        }
    }
    folderCustomCache[pathKey] = group;
}

int SongManager::getDifficultyOrder(const std::string& chartName) {
    std::string s = chartName;
    std::transform(s.begin(), s.end(), s.begin(), ::toupper);
    if (s.find("BEGINNER") != std::string::npos) return 0;
    if (s.find("NORMAL") != std::string::npos)   return 1;
    if (s.find("HYPER") != std::string::npos)    return 2;
    if (s.find("ANOTHER") != std::string::npos)  return 3;
    if (s.find("INSANE") != std::string::npos || s.find("LEGENDARIA") != std::string::npos) return 4;
    return 1;
}

void SongManager::scanBmsonRecursive(const std::string& path, std::vector<SongEntry>& songCache, SDL_Renderer* ren, NoteRenderer& renderer) {
    DIR* dir = opendir(path.c_str());
    if (!dir) return;
    struct dirent* ent;
    while ((ent = readdir(dir)) != NULL) {
        std::string name = ent->d_name;
        if (name[0] == '.') continue;
        std::string fullPath = path + (path.back() == '/' ? "" : "/") + name;
        if (ent->d_type == DT_DIR) {
            scanBmsonRecursive(fullPath, songCache, ren, renderer);
        } else if (name.size() > 6 && name.substr(name.size() - 6) == ".bmson") {
            BMSHeader h = BmsonLoader::loadHeader(fullPath);
            
            // --- 修正箇所: 7Key判定に基づく除外ロジック ---
            // modeHint が 14k のもの、または 7Key 条件を満たさない（5Key等）を除外
            if (h.modeHint == "beat-14k" || !h.is7Key) continue; 

            BestScore b = ScoreManager::loadScore(h.title, h.chartName, (int)h.total);
            
            SongEntry entry = {
                fullPath, h.title, h.subtitle, h.artist, h.chartName, 
                h.bpm, h.level, h.total, 
                h.totalNotes, 
                b.clearType, b.exScore, b.maxCombo, b.rank, h.modeHint
            };
            songCache.push_back(entry);

            if (ren) {
                SDL_SetRenderDrawColor(ren, 15, 15, 15, 255);
                SDL_RenderClear(ren);

                SDL_Color gray = {200, 200, 200, 255};
                SDL_Color green = {50, 205, 50, 255};
                SDL_Color yellow = {240, 230, 140, 255};

                int lineY = 40;
                auto drawLog = [&](const std::string& text, SDL_Color color) {
                    renderer.drawText(ren, text, 40, lineY, color, false, false);
                    lineY += 28;
                };

                drawLog("tenP@SWITCH-DEV:/$ find ./songs -name \"*.bmson\"", gray);
                std::string progress = "Scanning files... [" + std::to_string(songCache.size()) + " objects found]";
                drawLog(progress, gray);
                drawLog("--------------------------------------------------", gray);
                drawLog("Parsing: " + name, yellow);
                
                std::string shortPath = fullPath;
                if (shortPath.length() > 90) shortPath = "..." + shortPath.substr(shortPath.length() - 87);
                drawLog("  Path: " + shortPath, gray);
                drawLog("  Title: " + h.title, gray);
                drawLog("  Artist: " + h.artist, gray);

                char stats[128];
                sprintf(stats, "  BPM: %.2f | Level: %d | Nodes: %d", h.bpm, h.level, (int)h.totalNotes);
                drawLog(stats, gray);
                drawLog("  Status: OK", green);

                renderer.drawText(ren, "$ _", 40, lineY + 20, gray, false, false);
                SDL_RenderPresent(ren);
            }
        } else if (BmsLoader::isBmsFile(name)) {
            BMSHeader h = BmsLoader::loadHeader(fullPath);

            // DP (.bms player=3) と 5Key は除外
            if (h.modeHint == "beat-14k" || !h.is7Key) continue;

            BestScore b = ScoreManager::loadScore(h.title, h.chartName, (int)h.total);

            SongEntry entry = {
                fullPath, h.title, h.subtitle, h.artist, h.chartName,
                h.bpm, h.level, h.total,
                h.totalNotes,
                b.clearType, b.exScore, b.maxCombo, b.rank, h.modeHint
            };
            songCache.push_back(entry);

            if (ren) {
                SDL_SetRenderDrawColor(ren, 15, 15, 15, 255);
                SDL_RenderClear(ren);

                SDL_Color gray  = {200, 200, 200, 255};
                SDL_Color green = {50, 205, 50, 255};
                SDL_Color yellow = {240, 230, 140, 255};

                int lineY = 40;
                auto drawLog = [&](const std::string& text, SDL_Color color) {
                    renderer.drawText(ren, text, 40, lineY, color, false, false);
                    lineY += 28;
                };

                drawLog("tenP@SWITCH-DEV:/$ find ./songs -name \"*.bm[sel]\"", gray);
                std::string progress = "Scanning files... [" + std::to_string(songCache.size()) + " objects found]";
                drawLog(progress, gray);
                drawLog("--------------------------------------------------", gray);
                drawLog("Parsing: " + name, yellow);

                std::string shortPath = fullPath;
                if (shortPath.length() > 90) shortPath = "..." + shortPath.substr(shortPath.length() - 87);
                drawLog("  Path: " + shortPath, gray);
                drawLog("  Title: " + h.title, gray);
                drawLog("  Artist: " + h.artist, gray);

                char stats[128];
                sprintf(stats, "  BPM: %.2f | Level: %d | Notes: %d", h.bpm, h.level, (int)h.totalNotes);
                drawLog(stats, gray);
                drawLog("  Status: OK", green);

                renderer.drawText(ren, "$ _", 40, lineY + 20, gray, false, false);
                SDL_RenderPresent(ren);
            }
        }
    }
    closedir(dir);
}

void SongManager::loadSongList(std::vector<SongEntry>& songCache, std::vector<SongGroup>& songGroups, bool forceScan, const std::string& currentPath, SDL_Renderer* ren, NoteRenderer& renderer) {
    if (forceScan) SongManager::clearCache();

    if (forceScan || songCache.empty()) {
        songCache.clear();
        std::string cachePath = Config::ROOT_PATH + "songlist.dat";
        bool cacheLoaded = false;
        if (!forceScan) {
            std::ifstream ifs(cachePath, std::ios::binary);
            if (ifs) {
                size_t count;
                if (ifs.read((char*)&count, sizeof(count))) {
                    for (size_t i = 0; i < count; ++i) {
                        SongEntry e;
                        auto readStr = [&](std::string& s) {
                            size_t len; ifs.read((char*)&len, sizeof(len));
                            s.resize(len); ifs.read(&s[0], len);
                        };
                        readStr(e.filename); readStr(e.title); readStr(e.subtitle);
                        readStr(e.artist); readStr(e.chartName);
                        ifs.read((char*)&e.bpm, sizeof(e.bpm));
                        ifs.read((char*)&e.level, sizeof(e.level));
                        ifs.read((char*)&e.total, sizeof(e.total));
                        ifs.read((char*)&e.totalNotes, sizeof(e.totalNotes)); 
                        ifs.read((char*)&e.clearType, sizeof(e.clearType));
                        ifs.read((char*)&e.exScore, sizeof(e.exScore));
                        ifs.read((char*)&e.maxCombo, sizeof(e.maxCombo));
                        readStr(e.rank); readStr(e.modeHint);
                        
                        // --- キャッシュ読み込み時も 14k 等を除外する (is7Keyはキャッシュ保存されていないためmodeHint等で判定) ---
                        if (e.modeHint != "beat-14k") songCache.push_back(e);
                    }
                    cacheLoaded = !songCache.empty();
                }
                ifs.close();
            }
        }
        if (!cacheLoaded) {
            scanBmsonRecursive(Config::BMS_PATH, songCache, ren, renderer);
            std::remove(cachePath.c_str());
            FILE* fp = std::fopen(cachePath.c_str(), "wb");
            if (fp) {
                size_t count = songCache.size();
                std::fwrite(&count, sizeof(count), 1, fp);
                for (const auto& e : songCache) {
                    auto writeStr = [&](const std::string& s) {
                        size_t len = s.size();
                        std::fwrite(&len, sizeof(len), 1, fp);
                        std::fwrite(s.data(), 1, len, fp);
                    };
                    writeStr(e.filename); writeStr(e.title); writeStr(e.subtitle);
                    writeStr(e.artist); writeStr(e.chartName);
                    std::fwrite(&e.bpm, sizeof(e.bpm), 1, fp);
                    std::fwrite(&e.level, sizeof(e.level), 1, fp);
                    std::fwrite(&e.total, sizeof(e.total), 1, fp);
                    std::fwrite(&e.totalNotes, sizeof(e.totalNotes), 1, fp); 
                    std::fwrite(&e.clearType, sizeof(e.clearType), 1, fp);
                    std::fwrite(&e.exScore, sizeof(e.exScore), 1, fp);
                    std::fwrite(&e.maxCombo, sizeof(e.maxCombo), 1, fp);
                    writeStr(e.rank); writeStr(e.modeHint);
                }
                std::fflush(fp);
                int fd = fileno(fp);
                fsync(fd); 
                std::fclose(fp);
            }
        }
    }

    songGroups.clear();
    std::string baseDir = currentPath;
    if (!baseDir.empty() && baseDir.back() != '/') baseDir += "/";
    fs::path baseDirPath(baseDir);

    std::map<std::string, std::vector<int>> songGrouping;
    std::map<std::string, fs::path> folderGrouping;

    for (int i = 0; i < (int)songCache.size(); ++i) {
        const std::string& songFile = songCache[i].filename;
        if (songFile.compare(0, baseDir.length(), baseDir) != 0) continue; 
        std::string relStr = songFile.substr(baseDir.length());
        if (relStr.empty()) continue;
        size_t slashPos = relStr.find_first_of("/\\");
        if (slashPos == std::string::npos) {
            songGrouping[songCache[i].title].push_back(i);
        } else {
            std::string subDirName = relStr.substr(0, slashPos);
            std::string remainingPath = relStr.substr(slashPos + 1);
            if (remainingPath.find_first_of("/\\") == std::string::npos) songGrouping[subDirName].push_back(i);
            else folderGrouping[subDirName] = baseDirPath / subDirName;
        }
    }

    for (auto const& [folderName, fullPath] : folderGrouping) {
        if (songGrouping.count(folderName)) continue;
        SongGroup g;
        g.title = folderName; g.sortName = folderName; g.isFolder = true;
        g.folderPath = fullPath.string();
        parseFolderCustom(fullPath, folderName, g); 
        songGroups.push_back(g);
    }

    for (auto& pair : songGrouping) {
        SongGroup group;
        group.title = pair.first; group.sortName = pair.first; 
        group.songIndices = pair.second; group.isFolder = false;
        std::sort(group.songIndices.begin(), group.songIndices.end(), [&](int a, int b) {
            int orderA = getDifficultyOrder(songCache[a].chartName);
            int orderB = getDifficultyOrder(songCache[b].chartName);
            if (orderA != orderB) return orderA < orderB;
            return songCache[a].level < songCache[b].level;
        });
        group.currentDiffIdx = 0;
        if (!group.songIndices.empty()) {
            fs::path songPath(songCache[group.songIndices[0]].filename);
            fs::path parentDir = songPath.parent_path();
            parseFolderCustom(parentDir, parentDir.filename().string(), group);
            if (group.customLogo.empty()) {
                fs::path grandParentDir = parentDir.parent_path();
                if (parentDir.string().length() > baseDir.length()) {
                    SongGroup parentGroup;
                    parseFolderCustom(grandParentDir, grandParentDir.filename().string(), parentGroup);
                    if (!parentGroup.customLogo.empty()) group.customLogo = parentGroup.customLogo;
                    if (group.customFont.empty()) group.customFont = parentGroup.customFont;
                }
            }
        }
        songGroups.push_back(group);
    }

    if (currentPath == Config::BMS_PATH || currentPath.empty()) {
        auto vFolders = VirtualFolderManager::createCustomFolders(songCache);
        songGroups.insert(songGroups.end(), vFolders.begin(), vFolders.end());
    }

    std::sort(songGroups.begin(), songGroups.end(), [](const SongGroup& a, const SongGroup& b) {
        if (a.isFolder != b.isFolder) return a.isFolder; 
        return a.sortName < b.sortName;
    });
}

void SongManager::syncDifficulty(std::vector<SongGroup>& songGroups, const std::vector<SongEntry>& songCache, int preferredSlot) {
    for (auto& group : songGroups) {
        if (group.isFolder || group.songIndices.empty()) continue; 
        int bestMatchIdx = 0;
        int minDistance = 999;
        for (int i = 0; i < (int)group.songIndices.size(); ++i) {
            int slot = getDifficultyOrder(songCache[group.songIndices[i]].chartName);
            int distance = std::abs(slot - preferredSlot);
            if (distance < minDistance) {
                minDistance = distance;
                bestMatchIdx = i;
            }
            if (distance == 0) break;
        }
        group.currentDiffIdx = bestMatchIdx;
    }
}






