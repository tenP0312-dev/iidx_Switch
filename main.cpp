#include <SDL2/SDL.h>
#include "Config.hpp"
#include "SoundManager.hpp"
#include "NoteRenderer.hpp"
#include "SceneTitle.hpp"
#include "SceneSideSelect.hpp"
#include "SceneModeSelect.hpp"
#include "SceneSelect.hpp"
#include "ScenePlay.hpp"
#include "SceneResult.hpp"
#include "SceneOption.hpp" 
#include "SceneGameOver.hpp" // 追加
#include "SongManager.hpp" // 追加：スキャン実行用
#include "Logger.hpp"
#include <fstream>
#include <unistd.h>
#include <cstdio> 

#ifdef __SWITCH__
#include <switch.h>

extern "C" {
    u32 __nx_applet_heap_size = 2867ULL * 1024ULL * 1024ULL;
}
#endif

enum class AppState {
    TITLE,      
    SIDESELECT, 
    MODESELECT, 
    SELECT,      
    PLAYING,    
    OPTION,
    GAMEOVER,
    ONEMORE_ENTRY // 追加：演出用ステート
};

int main(int argc, char* argv[]) {
#ifdef __SWITCH__
    socketInitializeDefault();
    nxlinkStdio();

    for (int i = 0; i < 1024; i++) {
        int fd = dup(STDOUT_FILENO);
        if (fd >= 0) close(fd);
    }
#endif

    Config::load();
    Logger::init(Config::ROOT_PATH + "log.txt");
    LOG_INFO("main", "=== App Start ===");
    LOG_INFO("main", "ROOT_PATH: %s", Config::ROOT_PATH.c_str());
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_JOYSTICK) < 0) {
        return -1;
    }
    
    SDL_Joystick* joy = nullptr;
    if (SDL_NumJoysticks() > 0) {
        joy = SDL_JoystickOpen(0);
    }

    SDL_Window* win = SDL_CreateWindow("IIDX_TEST", 0, 0, 1280, 720, 0);
    SDL_Renderer* ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);

    NoteRenderer renderer;
    SoundManager::getInstance().init(); 
    renderer.init(ren); 

    SceneTitle sceneTitle;
    SceneSideSelect sceneSideSelect; 
    SceneModeSelect sceneModeSelect; 
    SceneSelect sceneSelect;
    ScenePlay scenePlay;
    SceneResult sceneResult;
    SceneOption sceneOption; 
    SceneGameOver sceneGameOver; // 追加

    // ★ステージ管理用変数
    int globalCurrentStage = 1;

    bool forceScan = false;
    bool scanSelectionFinished = false;
    int scanSelectedOption = 0; 

    std::string cachePath = Config::ROOT_PATH + "songlist.dat";
    FILE* checkFp = std::fopen(cachePath.c_str(), "rb");
    if (!checkFp) {
        forceScan = true;
        scanSelectionFinished = true;
    } else {
        std::fclose(checkFp);
    }

    while (!scanSelectionFinished) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) {
                SDL_Quit();
                return 0;
            }
            if (e.type == SDL_JOYBUTTONDOWN) {
                int btn = e.jbutton.button;

                if (btn == Config::SYS_BTN_UP || btn == Config::SYS_BTN_DOWN || 
                    btn == Config::SYS_BTN_LEFT || btn == Config::SYS_BTN_RIGHT) {
                    scanSelectedOption = 1 - scanSelectedOption;
                }
                
                if (btn == Config::BTN_EXIT) {
                    forceScan = (scanSelectedOption == 1);
                    scanSelectionFinished = true;
                }
            }
        }

        SDL_SetRenderDrawColor(ren, 10, 10, 20, 255);
        SDL_RenderClear(ren);

        SDL_Color white = {255, 255, 255, 255};
        SDL_Color cyan = {0, 255, 255, 255};
        SDL_Color gray = {150, 150, 150, 255};

        renderer.drawText(ren, "RE-SCAN SONG LIST?", 640, 250, white, true, true);
        renderer.drawText(ren, "NO (Use Cache)", 480, 400, (scanSelectedOption == 0 ? cyan : gray), true, true);
        renderer.drawText(ren, "YES (Re-scan)", 800, 400, (scanSelectedOption == 1 ? cyan : gray), true, true);
        
        renderer.drawText(ren, "UP/DOWN TO MOVE / START TO DECIDE", 640, 600, {180, 180, 180, 255}, false, true);

        SDL_RenderPresent(ren);
    }

    if (forceScan) {
        std::vector<SongEntry> dummyCache;
        std::vector<SongGroup> dummyGroups;
        SongManager::loadSongList(dummyCache, dummyGroups, true, Config::BMS_PATH, ren, renderer);
        forceScan = false; 
    }

    AppState currentState;
    if (Config::START_UP_OPTION == 1) {
        sceneSelect.init(false, ren, renderer, globalCurrentStage);
        currentState = AppState::SELECT;
    } else {
        sceneTitle.init();
        currentState = AppState::TITLE;
    }
    
    bool quitApp = false;
    // ★修正: フレームカウンタ(fps依存) → SDL_GetTicks() ミリ秒ベースに変更。
    //        旧: onemoreTimer++ / if(onemoreTimer >= 180) → 60fps=3秒、30fps=6秒で不一致。
    //        新: 入場時刻を記録し、経過ms で判定 → fps に依存しない。
    uint32_t onemoreStartTime = 0;

    while (!quitApp) {
#ifdef __SWITCH__
        if (!appletMainLoop()) break;
#endif

        switch (currentState) {
            case AppState::TITLE: {
                if (sceneTitle.update(ren, renderer) == TitleStep::FINISHED) {
                    sceneSideSelect.init();
                    currentState = AppState::SIDESELECT;
                }
                break;
            }

            case AppState::SIDESELECT: {
                if (sceneSideSelect.update(ren, renderer) == SideSelectStep::FINISHED) {
                    sceneModeSelect.init();
                    currentState = AppState::MODESELECT;
                }
                break;
            }

            case AppState::MODESELECT: {
                ModeSelectStep mStep = sceneModeSelect.update(ren, renderer);
                if (mStep == ModeSelectStep::GO_SELECT) {
                    globalCurrentStage = 1; 
                    sceneSelect.init(forceScan, ren, renderer, globalCurrentStage);
                    currentState = AppState::SELECT;
                    forceScan = false; 
                }
                else if (mStep == ModeSelectStep::GO_OPTION) {
                    sceneOption.init();
                    currentState = AppState::OPTION;
                }
                break;
            }

            case AppState::OPTION: { 
                if (sceneOption.update(ren, renderer) == OptionState::FINISHED) {
                    sceneModeSelect.init(); 
                    currentState = AppState::MODESELECT;
                }
                break;
            }

            case AppState::SELECT: {
                // ★修正：フリープレイ時は内部ロジック用(effectiveStage)は 6 (全解禁&自動進入無効)
                // 画面表示用には 0 を渡し、SceneSelect内での「STAGE 6」という不自然な表記を避ける
                bool isFreePlay = (sceneModeSelect.getSelect() == 0);
                int effectiveStage = isFreePlay ? 6 : globalCurrentStage;

                std::string selectedPath = sceneSelect.update(ren, renderer, effectiveStage, quitApp);
                
                if (!quitApp && sceneSelect.shouldBackToModeSelect()) {
                    sceneModeSelect.init();
                    currentState = AppState::MODESELECT;
                    globalCurrentStage = 1; 
                    break;
                }

                if (quitApp) break;
                
                if (!selectedPath.empty()) {
                    SDL_RenderClear(ren);
                    SDL_RenderPresent(ren); 
                    SDL_Delay(500); 

                    // プレイ実行
                    bool playFinishedNormal = scenePlay.run(ren, renderer, selectedPath);
                    PlayStatus status = scenePlay.getStatus();

                    if (playFinishedNormal) {
                        // リザルト表示
                        sceneResult.run(ren, renderer, status, scenePlay.getHeader());

                        if (isFreePlay) {
                            // フリープレイ時は解禁状態(6)を維持して即選曲へ戻る
                            sceneSelect.init(false, ren, renderer, 6);
                        } else {
                            // --- スタンダードモード進行ロジック ---
                            if (status.isFailed) {
                                sceneGameOver.init();
                                currentState = AppState::GAMEOVER;
                            } else {
                                if (globalCurrentStage < 3) {
                                    globalCurrentStage++;
                                } 
                                else if (globalCurrentStage == 3) {
                                    globalCurrentStage = 4;
                                }
                                else if (globalCurrentStage == 4) {
                                    if (sceneSelect.isExtraFolderSelected()) {
                                        globalCurrentStage = 5; 
                                        currentState = AppState::ONEMORE_ENTRY;
                                        // onemoreStartTime は ONEMORE_ENTRY ステート内で初回フレームに記録する
                                    } else {
                                        sceneGameOver.init();
                                        currentState = AppState::GAMEOVER;
                                    }
                                }
                                else if (globalCurrentStage == 5) {
                                    sceneGameOver.init();
                                    currentState = AppState::GAMEOVER;
                                }
                                
                                if (currentState == AppState::SELECT) {
                                    sceneSelect.init(false, ren, renderer, globalCurrentStage);
                                }
                            }
                        }
                    } else {
                        // 中断時
                        if (isFreePlay) {
                            sceneSelect.init(false, ren, renderer, 6);
                        } else {
                            sceneGameOver.init();
                            currentState = AppState::GAMEOVER;
                        }
                    }

                    SDL_RenderClear(ren);
                    SDL_RenderPresent(ren);
                }
                break;
            }

            case AppState::ONEMORE_ENTRY: {
                SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
                SDL_RenderClear(ren);

                uint32_t now = SDL_GetTicks();
                // ★修正: ステート初回エントリ時刻を記録する
                if (onemoreStartTime == 0) onemoreStartTime = now;
                uint32_t elapsed = now - onemoreStartTime;

                // 点滅は elapsed ms ベースで決定論的に制御 (10ms周期交互)
                SDL_Color gold = {255, 215, 0, 255};
                SDL_Color red  = {255, 50, 50, 255};
                SDL_Color currentMsgColor = ((elapsed / 167) % 2 == 0) ? gold : red; // ~167ms = 60fpsの10フレーム相当

                renderer.drawText(ren, "ONE MORE EXTRA STAGE", 640, 360, currentMsgColor, true, true);

                // 3000ms = 3秒固定 (fps 非依存)
                if (elapsed >= 3000) {
                    sceneSelect.init(false, ren, renderer, globalCurrentStage);
                    currentState = AppState::SELECT;
                    onemoreStartTime = 0; // 次回エントリに備えてリセット
                }

                SDL_RenderPresent(ren);
                break;
            }

            case AppState::GAMEOVER: {
                if (sceneGameOver.update(ren, renderer)) {
                    quitApp = true;
                }
                break;
            }
            
            default:
                break;
        }

        if (quitApp) break;
    }

    if (joy) SDL_JoystickClose(joy);
    renderer.cleanup();
    SoundManager::getInstance().cleanup();
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    
#ifdef __SWITCH__
    socketExit();
#endif
    
    return 0;
}








