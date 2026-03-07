#include "SceneSelect.hpp"
#include "SceneSelectView.hpp"
#include "BmsonLoader.hpp"
#include "BmsLoader.hpp"
#include "Config.hpp"
#include "ScoreManager.hpp"
#include "SceneDecision.hpp"
#include "SongManager.hpp" 
#include "SoundManager.hpp"
#include "VirtualFolderManager.hpp"
#include <dirent.h>
#include <algorithm>
#include <vector>
#include <fstream> 
#include <map>
#include <cmath>
#include <filesystem>
#include <random>

namespace fs = std::filesystem;

// --- Switch向けのパス正規化補助関数 ---
static std::string normalizePath(fs::path p) {
    std::string s = p.lexically_normal().string();
    if (s.length() > 1 && (s.back() == '/' || s.back() == '\\')) {
        s.pop_back();
    }
    return s;
}

static std::string g_lastSelectedPath = "";
static fs::path g_currentDirPath = "";
static SceneSelectView view;

int SceneSelect::getDifficultyOrder(const SongEntry& entry) {
    std::string s = entry.chartName;
    std::string f = entry.filename;
    std::transform(s.begin(), s.end(), s.begin(), ::toupper);
    std::transform(f.begin(), f.end(), f.begin(), ::toupper);

    if (!s.empty()) {
        if (s.find("BEGINNER") != std::string::npos) return 0;
        if (s.find("NORMAL") != std::string::npos)    return 1;
        if (s.find("HYPER") != std::string::npos)    return 2;
        if (s.find("ANOTHER") != std::string::npos)  return 3;
        if (s.find("INSANE") != std::string::npos || s.find("LEGENDARIA") != std::string::npos) return 4;
    }

    if (f.find("7B") != std::string::npos || f.find("BEG") != std::string::npos) return 0;
    if (f.find("7N") != std::string::npos || f.find("NOR") != std::string::npos) return 1;
    if (f.find("7H") != std::string::npos || f.find("HYP") != std::string::npos) return 2;
    if (f.find("7A") != std::string::npos || f.find("ANA") != std::string::npos) return 3;
    if (f.find("7X") != std::string::npos || f.find("INS") != std::string::npos) return 4;

    if (entry.level >= 12) return 4;
    if (entry.level >= 10) return 3;
    if (entry.level >= 7)  return 2;

    return 1;
}

void SceneSelect::syncDifficultyWithPreferredSlot() {
    for (auto& group : songGroups) {
        if (group.songIndices.empty() || group.isFolder) continue;

        int bestMatchIdx = 0;
        int minDistance = 999;

        for (int i = 0; i < (int)group.songIndices.size(); ++i) {
            int songIdx = group.songIndices[i];
            int thisSlot = SceneSelect::getDifficultyOrder(songCache[songIdx]);

            int distance = std::abs(thisSlot - preferredDifficultySlot);
            if (distance < minDistance) {
                minDistance = distance;
                bestMatchIdx = i;
            }
            if (distance == 0) {
                bestMatchIdx = i;
                break;
            }
        }
        group.currentDiffIdx = bestMatchIdx;
    }
}

void SceneSelect::applySort() {
    if (songGroups.empty()) return;

    switch (currentSort) {
        case SortMode::ALPHABET:    Config::SORT_NAME = "TITLE"; break;
        case SortMode::LEVEL:        Config::SORT_NAME = "LEVEL"; break;
        case SortMode::CLEAR_LAMP:  Config::SORT_NAME = "CLEAR"; break;
        case SortMode::SCORE:        Config::SORT_NAME = "SCORE"; break;
        case SortMode::BPM:          Config::SORT_NAME = "BPM"; break;
        default:                    Config::SORT_NAME = "DEFAULT"; break;
    }

    std::stable_sort(songGroups.begin(), songGroups.end(), [&](const SongGroup& a, const SongGroup& b) {
        if (a.isFolder != b.isFolder) return a.isFolder;

        if (a.isFolder && b.isFolder) {
            bool aIsVirtual = a.folderPath.rfind("VIRTUAL:", 0) == 0;
            bool bIsVirtual = b.folderPath.rfind("VIRTUAL:", 0) == 0;

            if (aIsVirtual && bIsVirtual) {
                return a.sortName < b.sortName;
            }
            if (aIsVirtual != bIsVirtual) {
                return aIsVirtual;
            }
            return a.title < b.title;
        }

        const auto& sa = songCache[a.songIndices[a.currentDiffIdx]];
        const auto& sb = songCache[b.songIndices[b.currentDiffIdx]];

        switch (currentSort) {
            case SortMode::LEVEL:
                if (sa.level != sb.level) return sa.level < sb.level;
                break;
            case SortMode::CLEAR_LAMP:
                if (sa.clearType != sb.clearType) return (int)sa.clearType > (int)sb.clearType;
                break;
            case SortMode::SCORE:
                if (sa.exScore != sb.exScore) return sa.exScore > sb.exScore;
                break;
            case SortMode::BPM:
                if (sa.bpm != sb.bpm) return sa.bpm < sb.bpm;
                break;
            default: break;
        }
        return sa.title < sb.title;
    });
}

void SceneSelect::prepareSongList(bool forceScan, SDL_Renderer* ren, NoteRenderer& renderer, int currentStage) {
    fs::path rootPath = fs::path(Config::BMS_PATH).lexically_normal();
    if (g_currentDirPath.empty()) g_currentDirPath = rootPath;
    
    std::string pathStr = g_currentDirPath.string();
    songGroups.clear();

    if (pathStr.rfind("VIRTUAL:", 0) == 0) {
        auto allVirtualFolders = VirtualFolderManager::createCustomFolders(songCache);
        for (const auto& vf : allVirtualFolders) {
            if (pathStr == vf.folderPath) {
                for (int cacheIdx : vf.songIndices) {
                    SongGroup sg;
                    sg.title = songCache[cacheIdx].title;
                    sg.sortName = songCache[cacheIdx].title;
                    sg.songIndices = {cacheIdx};
                    sg.isFolder = false;
                    songGroups.push_back(sg);
                }
                break;
            }
        }
    } 
    else {
        SongManager::loadSongList(songCache, songGroups, forceScan, pathStr, ren, renderer);

        if (normalizePath(g_currentDirPath) == normalizePath(rootPath)) { 
            auto vFolders = VirtualFolderManager::createCustomFolders(songCache);
            for (auto& vf : vFolders) {
                bool exists = std::any_of(songGroups.begin(), songGroups.end(), [&](const SongGroup& sg){
                    return sg.isFolder && sg.folderPath == vf.folderPath;
                });
                if (!exists) {
                    songGroups.insert(songGroups.begin(), vf);
                }
            }
        }
    }

    std::string autoEnterPath = "";
    auto it = songGroups.begin();
    while (it != songGroups.end()) {
        if (it->isFolder) {
            bool isOM = (it->folderPath.find("ONEMORE") != std::string::npos || it->title.find("ONEMORE") != std::string::npos);
            bool isEX = (it->folderPath.find("EXTRA") != std::string::npos || it->title.find("EXTRA") != std::string::npos);

            if (isOM) {
                if (currentStage < 5) {
                    it = songGroups.erase(it);
                    continue;
                } else {
                    if (currentStage == 5 && normalizePath(g_currentDirPath) == normalizePath(rootPath)) {
                        autoEnterPath = it->folderPath;
                    }
                }
            } else if (isEX && currentStage < 4) {
                it = songGroups.erase(it);
                continue;
            }
        }
        ++it;
    }

    if (!autoEnterPath.empty()) {
        g_currentDirPath = autoEnterPath;
        this->prepareSongList(false, ren, renderer, currentStage);
        return;
    }
    
    this->applySort();
    this->syncDifficultyWithPreferredSlot();
}

void SceneSelect::init(bool forceScan, SDL_Renderer* ren, NoteRenderer& renderer, int currentStage) { 
    fs::path rootPath = fs::path(Config::BMS_PATH).lexically_normal();
    
    if (currentStage == 5) {
        g_currentDirPath = rootPath;
    } else if (g_currentDirPath.empty()) {
        g_currentDirPath = rootPath;
    }
    
    if (songCache.empty() || forceScan) {
        prepareSongList(forceScan, ren, renderer, currentStage);
    } else {
        prepareSongList(false, ren, renderer, currentStage);
        for (auto& entry : songCache) {
            BestScore b = ScoreManager::loadScore(entry.title, entry.chartName, (int)entry.total);
            entry.clearType = b.clearType;
            entry.exScore = b.exScore;
            entry.maxCombo = b.maxCombo;
            entry.rank = b.rank;
        }
    }

    if (!g_lastSelectedPath.empty()) {
        for (size_t i = 0; i < songGroups.size(); ++i) {
            if (songGroups[i].isFolder && songGroups[i].folderPath == g_lastSelectedPath) {
                selectedIndex = (int)i;
                break;
            }
            for (size_t j = 0; j < songGroups[i].songIndices.size(); ++j) {
                if (songCache[songGroups[i].songIndices[j]].filename == g_lastSelectedPath) {
                    selectedIndex = (int)i;
                    songGroups[i].currentDiffIdx = (int)j;
                    break;
                }
            }
        }
    }

    lastSelectedIndex = -1;
    currentState = SelectState::SELECT_SONG;
    isShowingExitDialog = false;
    exitDialogSelection = 0;
    startButtonPressed = false;
    effectButtonPressed = false;
    scrUpPressed = false;
    scrDownPressed = false;
    lastScrollTime = 0;
    optionCategoryIndex = 0; 
    detailOptionIndex = 0; 
    backToModeSelectRequested = false;

    // --- プレビュー用タイマー初期化 ---
    previewPendingStartTime = 0;
    previewPending = false;

    if (songGroups.empty()) {
        selectedIndex = 0;
    } else {
        if (selectedIndex >= (int)songGroups.size()) selectedIndex = (int)songGroups.size() - 1;
        if (selectedIndex < 0) selectedIndex = 0;
    }
}

std::string SceneSelect::update(SDL_Renderer* ren, NoteRenderer& renderer, int currentStage, bool& quit) {
    Uint32 currentTime = SDL_GetTicks();
    const uint32_t REPEAT_INTERVAL = 100;
    static uint32_t lastDetailRepeatTime = 0;

    // ★修正: std::random_device は Switch 上でエントロピーソースがなく
    //        毎回同じシードになる。SDL_GetPerformanceCounter() で代替する。
    //        static にしないことでフォルダ移動ごとにシードが変わり再現性がなくなる。
    std::mt19937 gen(static_cast<uint32_t>(SDL_GetPerformanceCounter()));

    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT) { quit = true; return ""; }

        // ★Mac/PC: キーボード → SYS_BTNに変換
        if (e.type == SDL_KEYDOWN && !e.key.repeat) {
            int btn = -1;
            switch (e.key.keysym.sym) {
                case SDLK_UP:     btn = Config::SYS_BTN_UP;     break;
                case SDLK_DOWN:   btn = Config::SYS_BTN_DOWN;   break;
                case SDLK_LEFT:   btn = Config::SYS_BTN_LEFT;   break;
                case SDLK_RIGHT:  btn = Config::SYS_BTN_RIGHT;  break;
                case SDLK_RETURN: btn = Config::SYS_BTN_DECIDE; break;
                case SDLK_ESCAPE: btn = Config::SYS_BTN_BACK;   break;
                case SDLK_TAB:    btn = Config::SYS_BTN_DIFF;   break;
                case SDLK_F1:     btn = Config::SYS_BTN_OPTION; break;
                case SDLK_r:      btn = Config::SYS_BTN_RANDOM; break;
                case SDLK_q:      btn = Config::SYS_BTN_SORT;   break;
                default: break;
            }
            if (btn >= 0) {
                e.type = SDL_JOYBUTTONDOWN;
                e.jbutton.button = (Uint8)btn;
            }
        }
        
        if (e.type == SDL_JOYBUTTONDOWN || e.type == SDL_JOYBUTTONUP) {
            bool isDown = (e.type == SDL_JOYBUTTONDOWN);
            int btn = e.jbutton.button;
            if (btn == Config::SYS_BTN_OPTION) startButtonPressed = isDown;
            if (btn == Config::SYS_BTN_DIFF) effectButtonPressed = isDown;
            if (btn == Config::SYS_BTN_UP) scrUpPressed = isDown;
            if (btn == Config::SYS_BTN_DOWN) scrDownPressed = isDown;

            if (startButtonPressed && effectButtonPressed) {
                isShowingExitDialog = true;
                exitDialogSelection = 0;
            }
            if (isDown && currentState == SelectState::EDIT_DETAIL) {
                lastDetailRepeatTime = currentTime; 
            }
        }

        if (e.type == SDL_JOYBUTTONDOWN) {
            int btn = e.jbutton.button;

            if (isShowingExitDialog) {
                if (btn == Config::SYS_BTN_UP || btn == Config::SYS_BTN_DOWN) {
                    if (exitDialogSelection == 2) exitDialogSelection = 0;
                    else exitDialogSelection = (exitDialogSelection == 0) ? 1 : 0;
                }
                if (btn == Config::SYS_BTN_DIFF) {
                    if (exitDialogSelection == 2) exitDialogSelection = 0;
                    else exitDialogSelection = 2;
                }
                if (btn == Config::SYS_BTN_DECIDE) {
                    if (exitDialogSelection == 1) { quit = true; return ""; }
                    else if (exitDialogSelection == 2) {
                        isShowingExitDialog = false;
                        backToModeSelectRequested = true;
                        return "";
                    } else { isShowingExitDialog = false; }
                }
                continue; 
            }

            if (btn == Config::SYS_BTN_OPTION) {
                if (currentState == SelectState::SELECT_SONG) currentState = SelectState::EDIT_OPTION;
                else { currentState = SelectState::SELECT_SONG; Config::save(); renderer.notifyLayoutChanged(); }
                continue;
            }

            if (currentState == SelectState::SELECT_SONG) {
                if (btn == Config::SYS_BTN_RANDOM) {
                    std::vector<int> songIndicesOnly;
                    for (int i = 0; i < (int)songGroups.size(); ++i) {
                        if (!songGroups[i].isFolder) songIndicesOnly.push_back(i);
                    }
                    if (!songIndicesOnly.empty()) {
                        std::uniform_int_distribution<> dis(0, (int)songIndicesOnly.size() - 1);
                        selectedIndex = songIndicesOnly[dis(gen)];
                        syncDifficultyWithPreferredSlot();
                    }
                    continue;
                }

                if (btn == Config::SYS_BTN_SORT) {
                    currentSort = static_cast<SortMode>((static_cast<int>(currentSort) + 1) % static_cast<int>(SortMode::MAX_COUNT));
                    std::string currentPath = "";
                    if (!songGroups.empty()) {
                        if (songGroups[selectedIndex].isFolder) currentPath = songGroups[selectedIndex].folderPath;
                        else currentPath = songCache[songGroups[selectedIndex].songIndices[songGroups[selectedIndex].currentDiffIdx]].filename;
                    }
                    this->applySort();
                    for (int i = 0; i < (int)songGroups.size(); ++i) {
                        if (songGroups[i].isFolder) {
                            if (songGroups[i].folderPath == currentPath) { selectedIndex = i; break; }
                        } else {
                            if (songCache[songGroups[i].songIndices[songGroups[i].currentDiffIdx]].filename == currentPath) { selectedIndex = i; break; }
                        }
                    }
                    continue;
                }

                if (btn == Config::SYS_BTN_UP) {
                    selectedIndex = (selectedIndex - 1 + (int)songGroups.size()) % (int)songGroups.size();
                    syncDifficultyWithPreferredSlot(); 
                    lastScrollTime = currentTime + 250; 
                }
                else if (btn == Config::SYS_BTN_DOWN) {
                    selectedIndex = (selectedIndex + 1) % (int)songGroups.size();
                    syncDifficultyWithPreferredSlot(); 
                    lastScrollTime = currentTime + 250;
                }
                else if (btn == Config::SYS_BTN_BACK) {
                    std::string curPathStr = g_currentDirPath.string();
                    if (curPathStr.rfind("VIRTUAL:", 0) == 0) {
                        g_lastSelectedPath = curPathStr;
                        g_currentDirPath = fs::path(Config::BMS_PATH).lexically_normal();
                        this->init(false, ren, renderer, currentStage);
                        continue;
                    }
                    fs::path rootPath = fs::path(Config::BMS_PATH).lexically_normal();
                    if (normalizePath(g_currentDirPath) != normalizePath(rootPath)) {
                        g_lastSelectedPath = g_currentDirPath.string();
                        g_currentDirPath = g_currentDirPath.parent_path();
                        this->init(false, ren, renderer, currentStage); 
                        continue;
                    }
                }
                else if (btn == Config::SYS_BTN_DIFF && !songGroups.empty()) {
                    SongGroup& g = songGroups[selectedIndex];
                    if (!g.isFolder) {
                        int numCharts = (int)g.songIndices.size();
                        if (numCharts > 1) {
                            g.currentDiffIdx = (g.currentDiffIdx + 1) % numCharts;
                            int cacheIdx = g.songIndices[g.currentDiffIdx];
                            preferredDifficultySlot = SceneSelect::getDifficultyOrder(songCache[cacheIdx]);
                            this->syncDifficultyWithPreferredSlot();
                            // ★難易度変更時もプレビューを予約
                            lastSelectedIndex = -1; 
                        }
                    }
                }
                else if (btn == Config::SYS_BTN_DECIDE && !songGroups.empty()) {
                    SongGroup& g = songGroups[selectedIndex];
                    if (g.isFolder) {
                        if (!g.customSE.empty() && fs::exists(g.customSE)) {
                            fs::path sePath(g.customSE);
                            SoundManager::getInstance().loadSingleSound(sePath.filename().string(), sePath.parent_path().string());
                            SoundManager::getInstance().playByName(sePath.filename().string());
                        }
                        g_currentDirPath = g.folderPath;
                        this->init(false, ren, renderer, currentStage); 
                        selectedIndex = 0;
                        continue;
                    } else {
                        // 決定時はプレビューを止める
                        SoundManager::getInstance().stopPreview();
                        int cacheIdx = g.songIndices[g.currentDiffIdx];
                        std::string fullPath = songCache[cacheIdx].filename;
                        g_lastSelectedPath = fullPath;

                        std::string currentDir = g_currentDirPath.string();
                        lastSelectedWasExtra = (currentDir.find("EXTRA") != std::string::npos || currentDir.find("Extra") != std::string::npos);
                        lastSelectedWasOneMore = (currentDir.find("ONEMORE") != std::string::npos || currentDir.find("OneMore") != std::string::npos);

                        BMSHeader header = BmsLoader::isBmsFile(fullPath)
                            ? BmsLoader::loadHeader(fullPath)
                            : BmsonLoader::loadHeader(fullPath);
                        SceneDecision decision;
                        if (decision.run(ren, renderer, header)) {
                            return fullPath; 
                        }
                    }
                }
            } 
            else if (currentState == SelectState::EDIT_OPTION) {
                if (btn == Config::SYS_BTN_DIFF) {
                    currentState = SelectState::EDIT_DETAIL;
                    detailOptionIndex = 0;
                    continue;
                }
                if (btn == Config::SYS_BTN_LEFT) optionCategoryIndex = (optionCategoryIndex + 3) % 4;
                if (btn == Config::SYS_BTN_RIGHT) optionCategoryIndex = (optionCategoryIndex + 1) % 4;
                if (btn == Config::SYS_BTN_UP) {
                    if (optionCategoryIndex == 0) Config::PLAY_OPTION = (Config::PLAY_OPTION + 4) % 5;
                    else if (optionCategoryIndex == 1) Config::GAUGE_OPTION = (Config::GAUGE_OPTION + 6) % 7;
                    else if (optionCategoryIndex == 2) Config::ASSIST_OPTION = (Config::ASSIST_OPTION + 7) % 8;
                    else if (optionCategoryIndex == 3) Config::EX_OPTION = (Config::EX_OPTION + 4) % 5;
                }
                if (btn == Config::SYS_BTN_DOWN) {
                    if (optionCategoryIndex == 0) Config::PLAY_OPTION = (Config::PLAY_OPTION + 1) % 5;
                    else if (optionCategoryIndex == 1) Config::GAUGE_OPTION = (Config::GAUGE_OPTION + 1) % 7;
                    else if (optionCategoryIndex == 2) Config::ASSIST_OPTION = (Config::ASSIST_OPTION + 1) % 8;
                    else if (optionCategoryIndex == 3) Config::EX_OPTION = (Config::EX_OPTION + 1) % 5;
                }
                double currentBPM = 150.0;
                if (!songGroups.empty() && !songGroups[selectedIndex].isFolder) {
                    int cacheIdx = songGroups[selectedIndex].songIndices[songGroups[selectedIndex].currentDiffIdx];
                    currentBPM = songCache[cacheIdx].bpm;
                }
                Config::HIGH_SPEED = (double)Config::HS_BASE / (Config::GREEN_NUMBER * currentBPM) * ((double)Config::VISIBLE_PX / 723.0);
            }
            else if (currentState == SelectState::EDIT_DETAIL) {
                if (btn == Config::SYS_BTN_DIFF || btn == Config::SYS_BTN_BACK) {
                    currentState = SelectState::EDIT_OPTION;
                    continue;
                }
                if (btn == Config::SYS_BTN_LEFT) detailOptionIndex = (detailOptionIndex + 4) % 5;
                if (btn == Config::SYS_BTN_RIGHT) detailOptionIndex = (detailOptionIndex + 1) % 5;

                bool folderRefreshed = false;
                if (detailOptionIndex == 0) {
                    if (btn == Config::SYS_BTN_UP)    Config::JUDGE_OFFSET += 1;
                    if (btn == Config::SYS_BTN_DOWN)  Config::JUDGE_OFFSET -= 1;
                    if (btn == Config::SYS_BTN_DECIDE) Config::JUDGE_OFFSET = 0;
                }
                else if (detailOptionIndex == 1) {
                    if (btn == Config::SYS_BTN_UP || btn == Config::SYS_BTN_DOWN || btn == Config::SYS_BTN_DECIDE) {
                        Config::SHOW_FAST_SLOW = (Config::SHOW_FAST_SLOW + 1) % 3; // 0=OFF→1=ON→2=DETAIL→0
                    }
                }
                else if (detailOptionIndex == 2) {
                    if (btn == Config::SYS_BTN_UP) {
                        Config::FOLDER_NOTES_MIN += 10;
                        if (Config::FOLDER_NOTES_MIN > Config::FOLDER_NOTES_MAX) Config::FOLDER_NOTES_MAX = Config::FOLDER_NOTES_MIN;
                        folderRefreshed = true;
                    }
                    if (btn == Config::SYS_BTN_DOWN) {
                        Config::FOLDER_NOTES_MIN = std::max(0, Config::FOLDER_NOTES_MIN - 10);
                        folderRefreshed = true;
                    }
                }
                else if (detailOptionIndex == 3) {
                    if (btn == Config::SYS_BTN_UP) {
                        Config::FOLDER_NOTES_MAX += 10;
                        folderRefreshed = true;
                    }
                    if (btn == Config::SYS_BTN_DOWN) {
                        Config::FOLDER_NOTES_MAX = std::max(0, Config::FOLDER_NOTES_MAX - 10);
                        if (Config::FOLDER_NOTES_MAX < Config::FOLDER_NOTES_MIN) Config::FOLDER_NOTES_MIN = Config::FOLDER_NOTES_MAX;
                        folderRefreshed = true;
                    }
                }
                else if (detailOptionIndex == 4) {
                    if (btn == Config::SYS_BTN_UP)    Config::DAN_GAUGE_START_PERCENT = std::min(100, Config::DAN_GAUGE_START_PERCENT + 2);
                    if (btn == Config::SYS_BTN_DOWN)  Config::DAN_GAUGE_START_PERCENT = std::max(0, Config::DAN_GAUGE_START_PERCENT - 2);
                    if (btn == Config::SYS_BTN_DECIDE) Config::DAN_GAUGE_START_PERCENT = 100;
                }
                if (folderRefreshed) {
                    this->prepareSongList(false, ren, renderer, currentStage);
                }
            }
        }
    }

    if (currentState == SelectState::EDIT_DETAIL && (currentTime - lastDetailRepeatTime >= REPEAT_INTERVAL)) {
        if (scrUpPressed || scrDownPressed) {
            bool folderRefreshed = false;
            if (detailOptionIndex == 0) {
                if (scrUpPressed)    Config::JUDGE_OFFSET += 1;
                if (scrDownPressed) Config::JUDGE_OFFSET -= 1;
            }
            else if (detailOptionIndex == 2) {
                if (scrUpPressed) {
                    Config::FOLDER_NOTES_MIN += 10;
                    if (Config::FOLDER_NOTES_MIN > Config::FOLDER_NOTES_MAX) Config::FOLDER_NOTES_MAX = Config::FOLDER_NOTES_MIN;
                    folderRefreshed = true;
                }
                if (scrDownPressed) {
                    Config::FOLDER_NOTES_MIN = std::max(0, Config::FOLDER_NOTES_MIN - 10);
                    folderRefreshed = true;
                }
            }
            else if (detailOptionIndex == 3) {
                if (scrUpPressed) {
                    Config::FOLDER_NOTES_MAX += 10;
                    folderRefreshed = true;
                }
                if (scrDownPressed) {
                    Config::FOLDER_NOTES_MAX = std::max(0, Config::FOLDER_NOTES_MAX - 10);
                    if (Config::FOLDER_NOTES_MAX < Config::FOLDER_NOTES_MIN) Config::FOLDER_NOTES_MIN = Config::FOLDER_NOTES_MAX;
                    folderRefreshed = true;
                }
            }
            else if (detailOptionIndex == 4) {
                if (scrUpPressed)    Config::DAN_GAUGE_START_PERCENT = std::min(100, Config::DAN_GAUGE_START_PERCENT + 2);
                if (scrDownPressed) Config::DAN_GAUGE_START_PERCENT = std::max(0, Config::DAN_GAUGE_START_PERCENT - 2);
            }
            if (folderRefreshed) { this->prepareSongList(false, ren, renderer, currentStage); }
            lastDetailRepeatTime = currentTime;
        }
    }

    if (currentTime > lastScrollTime) {
        if (currentState == SelectState::SELECT_SONG && !isShowingExitDialog) {
            if (scrUpPressed) {
                selectedIndex = (selectedIndex - 1 + (int)songGroups.size()) % (int)songGroups.size();
                syncDifficultyWithPreferredSlot();
                lastScrollTime = currentTime + 45; 
            } else if (scrDownPressed) {
                selectedIndex = (selectedIndex + 1) % (int)songGroups.size();
                syncDifficultyWithPreferredSlot();
                lastScrollTime = currentTime + 45; 
            }
        }
    }

    // ★修正：previewTimer (フレームカウンタ) を廃止し、SDL_GetTicks() ベースに変更。
    // フレームカウンタはフレームレートに依存するため 30fps/60fps で待機時間が変わる問題があった。
    if (selectedIndex != lastSelectedIndex) {
        lastSelectedIndex = selectedIndex;
        previewPendingStartTime = currentTime;
        previewPending = true;
        SoundManager::getInstance().stopPreview();
    }

    if (previewPending && (currentTime - previewPendingStartTime >= PREVIEW_DELAY_MS)) {
        previewPending = false;
        if (currentState == SelectState::SELECT_SONG) {
            if (!songGroups.empty() && !songGroups[selectedIndex].isFolder) {
                const auto& g = songGroups[selectedIndex];
                const auto& entry = songCache[g.songIndices[g.currentDiffIdx]];
                if (!entry.previewPath.empty()) {
                    SoundManager::getInstance().playPreview(entry.previewPath);
                } else {
                    BMSHeader header = BmsLoader::isBmsFile(entry.filename)
                        ? BmsLoader::loadHeader(entry.filename)
                        : BmsonLoader::loadHeader(entry.filename);
                    if (!header.preview.empty()) {
                        fs::path root = fs::path(entry.filename).parent_path();
                        SoundManager::getInstance().playPreview((root / header.preview).string());
                    }
                }
            }
        }
    }

    view.render(ren, renderer, songGroups, songCache, selectedIndex, (int)currentState, isShowingExitDialog, exitDialogSelection, optionCategoryIndex, detailOptionIndex, currentStage);
    
    SDL_RenderPresent(ren);
    return "";
}

bool SceneSelect::isExtraFolderSelected() const {
    return lastSelectedWasExtra;
}

bool SceneSelect::isOneMoreFolderSelected() const {
    return lastSelectedWasOneMore;
}

void SceneSelect::renderOptionOverlay(SDL_Renderer* ren, NoteRenderer& renderer) {}
void SceneSelect::renderExitDialog(SDL_Renderer* ren, NoteRenderer& renderer) {}














