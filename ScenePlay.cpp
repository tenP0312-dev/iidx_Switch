#include "ScenePlay.hpp"
#include "PlayEngine.hpp"
#include "Config.hpp"
#include "SceneResult.hpp"
#include "BgaManager.hpp"
#include <cmath>
#include <algorithm>
#include <SDL2/SDL_image.h> 

#ifdef __SWITCH__
#include <switch.h>
#endif

// --- 既存の補助関数群 (100%継承) ---
int ScenePlay::getLaneFromJoystickButton(int btn) {
    if (btn == Config::BTN_LANE1) return 1;
    if (btn == Config::BTN_LANE2) return 2;
    if (btn == Config::BTN_LANE3) return 3;
    if (btn == Config::BTN_LANE4) return 4;
    if (btn == Config::BTN_LANE5) return 5;
    if (btn == Config::BTN_LANE6) return 6;
    if (btn == Config::BTN_LANE7) return 7;
    if (btn == Config::BTN_LANE8_A || btn == Config::BTN_LANE8_B) return 8;
    return -1;
}

bool ScenePlay::isAutoLane(int lane) {
    if (Config::ASSIST_OPTION == 7) return true;
    bool autoScr = (Config::ASSIST_OPTION == 1 || Config::ASSIST_OPTION == 4 || 
                    Config::ASSIST_OPTION == 5 || Config::ASSIST_OPTION == 6);
    bool auto5k = (Config::ASSIST_OPTION == 3 || Config::ASSIST_OPTION == 5 || 
                    Config::ASSIST_OPTION == 6);
    if (lane == 8 && autoScr) return true;
    if ((lane == 6 || lane == 7) && auto5k) return true;
    return false;
}

void ScenePlay::updateAssist(double cur_ms, PlayEngine& engine, SoundManager& snd) {
    uint32_t now = SDL_GetTicks();
    const auto& notes = engine.getNotes();
    // 描画開始位置から探索を開始することで計算量を削減
    for (size_t i = drawStartIndex; i < notes.size(); ++i) {
        const auto& n = notes[i];
        if (n.played || n.isBGM) continue;
        if (n.target_ms > cur_ms + 100) break; // 早期終了
        
        if (isAutoLane(n.lane)) {
            if (!n.isBeingPressed && cur_ms >= n.target_ms) {
                // ★修正: 完全オート(ASSIST_OPTION==7)はスコア・コンボ加算あり。
                //        部分オート（スクラッチ/5kオート）はそのレーンを加算しない。
                bool isPartialAuto = (Config::ASSIST_OPTION != 7);
                int resultJudge = engine.processHit(n.lane, n.target_ms, now, snd, isPartialAuto);

                bool found = false;
                for (size_t ei = 0; ei < effectCount; ++ei) {
                    if (effectsBuf[ei].lane == n.lane) {
                        effectsBuf[ei].startTime = now;
                        found = true;
                        break;
                    }
                }
                if (!found && effectCount < MAX_EFFECTS)
                    effectsBuf[effectCount++] = {n.lane, now};
                if (bombCount < MAX_BOMBS)
                    bombAnimsBuf[bombCount++] = {n.lane, now, 2};

                // ★修正: オートLNの持続ボム用に判定結果を保存。
                //        旧実装は lnHitJudge を processInput() 内でしかセットしなかったため
                //        オートレーンでは常に 0 のまま → LN持続ボムが出なかった。
                if (n.isLN) {
                    lnHitJudge[n.lane]     = resultJudge;
                    lastLNBombTime[n.lane] = now;
                }
            }
            if (n.isLN && n.isBeingPressed && cur_ms >= n.target_ms + n.duration_ms) {
                engine.processRelease(n.lane, n.target_ms + n.duration_ms, now);
                // ★修正: LNリリース時にリセット（手動入力と同じ処理）
                lnHitJudge[n.lane]     = 0;
                lastLNBombTime[n.lane] = 0;
            }
        }
    }
}

// --- メインロジック ---
bool ScenePlay::run(SDL_Renderer* ren, SoundManager& snd, NoteRenderer& renderer, const std::string& bmsonPath) {
    // ─────────────────────────────────────────────
    // フェーズ 1: 前曲クリーンアップ
    // ─────────────────────────────────────────────
    {
        BgaManager tempBga;
        tempBga.cleanup();
    }
    snd.clear();
    SDL_RenderClear(ren);
    SDL_RenderPresent(ren);
    SDL_Delay(200);

    // ─────────────────────────────────────────────
    // フェーズ 2: BMSON パースのみ
    // ここに含まれるのは JSON 解析だけ。
    // 動画・BGA・音声は一切触らない。
    // ─────────────────────────────────────────────
    {
        SDL_SetRenderDrawColor(ren, 10, 10, 15, 255);
        SDL_RenderClear(ren);
        renderer.renderBackground(ren);
        renderer.renderLanes(ren, 0.0, 0);
        renderer.drawText(ren, "BMSON Loading...", renderer.getLaneCenterX(),
                          450, {255, 255, 0, 255}, false, true);
        SDL_RenderPresent(ren);
    }
    BMSData data = BmsonLoader::load(bmsonPath, [&](float /*progress*/) {
        SDL_Event e; while (SDL_PollEvent(&e));
    });

    if (data.sound_channels.empty()) return true;

    // JSON DOM をここで破棄させる（スコープ外に出ているため自動解放済み）
    SDL_Delay(100);

    // ─────────────────────────────────────────────
    // フェーズ 3: エンジン・BGA 初期化 → プレイ画面を表示
    // BMSON パース完了 → ヘッダー情報が揃ったのでタイトル等を描画できる。
    // 動画ロード(bga.loadBgaFile)もここで行い、完了したら即座に画面を更新する。
    // ─────────────────────────────────────────────
    currentHeader = data.header;
    Config::HIGH_SPEED = (double)Config::HS_BASE / (std::max(1, Config::GREEN_NUMBER) * data.header.bpm);

    PlayEngine engine;
    engine.init(data);
    drawStartIndex = 0;

    BgaManager bga;
    bga.init(data.bga_images.size());
    bga.setEvents(data.bga_events);
    bga.setLayerEvents(data.layer_events);
    bga.setPoorEvents(data.poor_events);

    effectCount = 0;
    bombCount   = 0;
    for (int i = 0; i < 9; ++i) {
        lanePressed[i]    = false;
        lnHitJudge[i]     = 0;
        lastLNBombTime[i] = 0;
    }

    isAssistUsed       = (Config::ASSIST_OPTION > 0);
    startButtonPressed = false;
    effectButtonPressed= false;
    scratchUpActive    = false;
    scratchDownActive  = false;
    lastStartPressTime = 0;
    if (Config::SUDDEN_PLUS > 0) backupSudden = Config::SUDDEN_PLUS;

    std::string bmsonDir = "";
    size_t lastSlash = bmsonPath.find_last_of("/\\");
    if (lastSlash != std::string::npos)
        bmsonDir = bmsonPath.substr(0, lastSlash + 1);
    else
        bmsonDir = Config::ROOT_PATH;

    bga.setBgaDirectory(bmsonDir);

    if (!data.header.bga_video.empty()) {
        bga.loadBgaFile(bmsonDir + data.header.bga_video, ren);
    }
    for (auto const& [id, filename] : data.bga_images) {
        bga.registerPath(id, filename);
    }

    // エンジン・BGA 準備完了 → タイトル・アーティスト・レベルを含む画面を表示
    {
        SDL_SetRenderDrawColor(ren, 10, 10, 15, 255);
        SDL_RenderClear(ren);
        renderer.renderBackground(ren);
        renderer.renderLanes(ren, 0.0, 0);
        renderer.renderDecisionInfo(ren, currentHeader);
        SDL_RenderPresent(ren);
    }

    // ─────────────────────────────────────────────
    // フェーズ 4: BoxWav インデックス構築 → 音声ロード
    // ローディング中はプレイ背景 + タイトル情報 + テキストを表示。
    // renderScene() は呼ばない（bga.render() による動画デコードを避けるため）。
    // ─────────────────────────────────────────────
    std::string bmsonBaseName = "";
    {
        size_t lastDot = bmsonPath.find_last_of(".");
        if (lastDot != std::string::npos && lastDot > lastSlash)
            bmsonBaseName = bmsonPath.substr(lastSlash + 1, lastDot - lastSlash - 1);
        else
            bmsonBaseName = data.header.title;
    }

    auto showLoadingScreen = [&](const char* text) {
        SDL_SetRenderDrawColor(ren, 10, 10, 15, 255);
        SDL_RenderClear(ren);
        renderer.renderBackground(ren);
        renderer.renderLanes(ren, 0.0, 0);
        renderer.renderDecisionInfo(ren, currentHeader);
        renderer.drawTextCached(ren, text, renderer.getLaneCenterX(),
                                450, {255, 255, 0, 255}, false, true);
        SDL_RenderPresent(ren);
    };

    showLoadingScreen("BOXWAV Loading...");
    snd.preloadBoxIndex(bmsonDir, bmsonBaseName);

    // BGM を先頭に並べて MAX_WAV_MEMORY 到達時でも BGM が確実にロードされるようにする
    std::vector<std::string> soundListBGM;
    std::vector<std::string> soundListKeys;
    soundListBGM.reserve(data.sound_channels.size());
    soundListKeys.reserve(data.sound_channels.size());

    for (const auto& ch : data.sound_channels) {
        if (ch.name.empty()) continue;
        bool isBgmOnly = !ch.notes.empty() &&
                         std::all_of(ch.notes.begin(), ch.notes.end(),
                                     [](const BMSNote& n) { return n.x < 1 || n.x > 8; });
        if (isBgmOnly) soundListBGM.push_back(ch.name);
        else           soundListKeys.push_back(ch.name);
    }

    std::vector<std::string> soundList;
    soundList.reserve(soundListBGM.size() + soundListKeys.size());
    soundList.insert(soundList.end(), soundListBGM.begin(), soundListBGM.end());
    soundList.insert(soundList.end(), soundListKeys.begin(), soundListKeys.end());

    showLoadingScreen("AUDIO Loading...");
    snd.loadSoundsInBulk(soundList, bmsonDir, bmsonBaseName, [&](int processedCount, const std::string&) {
        // 200件ごとに表示更新 + イベント処理（renderScene は呼ばない）
        if (processedCount % 200 == 0) {
            showLoadingScreen("AUDIO Loading...");
            SDL_Event e; while(SDL_PollEvent(&e));
        }
    });

    double videoOffsetMs = 0.0;
    if (data.header.bga_offset != 0) {
        double currentBpm = data.header.bpm;
        int64_t currentY = 0;
        double currentMs = 0.0;
        std::vector<BPMEvent> sortedBpm = data.bpm_events;
        std::sort(sortedBpm.begin(), sortedBpm.end(), [](const BPMEvent& a, const BPMEvent& b){ return a.y < b.y; });
        for (const auto& bpmEv : sortedBpm) {
            if (bpmEv.y >= data.header.bga_offset) break;
            int64_t distY = bpmEv.y - currentY;
            currentMs += (double)distY * 60000.0 / (currentBpm * data.header.resolution);
            currentY = bpmEv.y;
            currentBpm = bpmEv.bpm;
        }
        if (currentY < data.header.bga_offset) {
            int64_t distY = data.header.bga_offset - currentY;
            currentMs += (double)distY * 60000.0 / (currentBpm * data.header.resolution);
        }
        videoOffsetMs = currentMs;
    }

    double max_target_ms = 0;
    for (const auto& n : engine.getNotes()) {
        if (!n.isBGM) max_target_ms = std::max(max_target_ms, n.target_ms);
    }

    bool waiting = true;
    while (waiting) {
        uint32_t now = SDL_GetTicks();
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) return false;
            if (ev.type == SDL_JOYBUTTONDOWN) {
                if (ev.jbutton.button == Config::SYS_BTN_DECIDE) {
                    waiting = false;
                    break;
                }
            }
        }
        if (!processInput(-2000.0, now, snd, engine)) return false;
        bga.preLoad(0, ren);
        renderScene(ren, renderer, engine, bga, -2000.0, 0, 0, currentHeader, now, 0.0);
        renderer.drawTextCached(ren, "PRESS DECIDE BUTTON TO START",
                                renderer.getLaneCenterX(), 450, {255, 255, 255, 255}, false, true);
        SDL_RenderPresent(ren);
#ifdef __SWITCH__
        if (!appletMainLoop()) return false;
#endif
    }

    uint32_t start_ticks = SDL_GetTicks() + 2000;
    startTicks = start_ticks; // ★修正(CRITICAL-1): メンバ変数にコピー → processInput で参照
    uint32_t lastFpsTime = SDL_GetTicks();
    int frameCount = 0, fps = 0;
    bool playing = true;
    bool isAborted = false;
    bool fcEffectTriggered = false; 

    SDL_Texture* gradTex = nullptr;
    const int TEX_H = 512;
    {
        Uint32 rmask, gmask, bmask, amask;
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
        rmask = 0xff000000; gmask = 0x00ff0000; bmask = 0x0000ff00; amask = 0x000000ff;
#else
        rmask = 0x000000ff; gmask = 0x0000ff00; bmask = 0x00ff0000; amask = 0xff000000;
#endif
        SDL_Surface* surf = SDL_CreateRGBSurface(0, 1, TEX_H, 32, rmask, gmask, bmask, amask);
        if (surf) {
            Uint32* pixels = (Uint32*)surf->pixels;
            for (int y = 0; y < TEX_H; y++) {
                float dist = std::abs((float)y - (TEX_H / 2.0f)) / (TEX_H / 2.0f);
                Uint8 alpha = (Uint8)((1.0f - dist) * 255);
                pixels[y] = SDL_MapRGBA(surf->format, 255, 255, 255, alpha);
            }
            gradTex = SDL_CreateTextureFromSurface(ren, surf);
            SDL_FreeSurface(surf);
        }
    }

    while (playing) {
        uint32_t now = SDL_GetTicks();
        double cur_ms = (double)((int64_t)now - (int64_t)start_ticks);

        bga.syncTime(cur_ms - videoOffsetMs);

        if (!processInput(cur_ms, now, snd, engine)) {
            if (engine.getStatus().isFailed) playing = false;
            else { isAborted = true; playing = false; break; }
        }
        updateAssist(cur_ms, engine, snd);

        // LN押下中ボム演出: GREAT/PGREAT 判定のLNを押下中、0.1秒ごとにボムを発生
        {
            const auto& allNotesLN = engine.getNotes();
            for (size_t i = drawStartIndex; i < allNotesLN.size(); ++i) {
                const auto& n = allNotesLN[i];
                if (n.isBGM || !n.isLN || !n.isBeingPressed) continue;
                if (n.target_ms > cur_ms + 500.0) break;
                // GREAT(2) または PGREAT(3) のみ
                // ★修正: 0.1秒(100ms)間隔 → ボム1周期(300ms)の半分=150ms間隔に変更。
                //        前のボムが半分まで再生されたら新しいボムを重ねることで
                //        「連続して輝き続ける」演出になる。
                if (lnHitJudge[n.lane] >= 2 && now - lastLNBombTime[n.lane] >= 150) {
                    lastLNBombTime[n.lane] = now;
                    int bombType = (lnHitJudge[n.lane] == 3) ? 1 : 2;
                    if (bombCount < MAX_BOMBS)
                        bombAnimsBuf[bombCount++] = {n.lane, now, bombType};
                }
            }
        }

        engine.update(cur_ms + 10.0, now, snd);
        // ★修正①: const ref で受け取ることで gaugeHistory (最大 2000 要素) の
        //          毎フレームコピーを完全に排除。432KB/秒のヒープコピー帯域を節約。
        const PlayStatus& s = engine.getStatus();
        if (s.isFailed) playing = false;
        double progress = 0.0;
        if (max_target_ms > 0) progress = std::clamp(cur_ms / max_target_ms, 0.0, 1.0);
        int64_t cur_y = engine.getYFromMs(cur_ms);
        auto& judge = engine.getCurrentJudge();
        if (judge.active && (judge.kind == JudgeKind::POOR || judge.kind == JudgeKind::BAD)) bga.setMissTrigger(true);
        else bga.setMissTrigger(false);

        renderScene(ren, renderer, engine, bga, cur_ms, cur_y, fps, currentHeader, now, progress);

        if (!fcEffectTriggered && s.remainingNotes <= 0) {
            bool isFC = (s.poorCount == 0 && s.badCount == 0 && s.totalNotes > 0);
            if (isFC) {
                // ★修正①: FC 確定時に一度だけコピーし、clearType を上書き
                status = s;
                status.clearType = ClearType::FULL_COMBO;
                fcEffectTriggered = true; 
                uint32_t fcStart = SDL_GetTicks();
                // ★修正⑥: rebuildLaneLayout() でキャッシュ済みの値を使用（再計算を廃止）
                int baseX      = renderer.getLaneBaseX();
                int totalWidth = renderer.getLaneTotalWidth();
                int laneCenterX = renderer.getLaneCenterX();
                while (SDL_GetTicks() - fcStart < 2500) {
                    uint32_t nowFC = SDL_GetTicks();
                    float p = std::min(1.0f, (float)(nowFC - fcStart) / 1000.0f);

                    // ★修正: FCループ中も入力処理を行う。
                    //        旧実装は SDL_PollEvent を呼ばず SDL_Delay(1) で待機するだけだったため
                    //        Switch の入力イベントキューが詰まりフリーズしていた。
                    if (!processInput(cur_ms, nowFC, snd, engine)) break;

                    renderScene(ren, renderer, engine, bga, cur_ms, cur_y, fps, currentHeader, nowFC, 1.0);
                    if (gradTex) {
                        int lineY = Config::JUDGMENT_LINE_Y;
                        int uvOffset = (int)(nowFC * 1) % TEX_H;
                        SDL_Rect srcRect = { 0, uvOffset, 1, TEX_H / 2 };
                        SDL_Rect dstRect = { baseX, 0, totalWidth, lineY };
                        SDL_SetTextureBlendMode(gradTex, SDL_BLENDMODE_BLEND);
                        SDL_SetTextureColorMod(gradTex, 0, 255, 255);
                        SDL_SetTextureAlphaMod(gradTex, (Uint8)((1.0f - p) * 200));
                        SDL_RenderCopy(ren, gradTex, &srcRect, &dstRect);
                        SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
                        SDL_SetRenderDrawColor(ren, 0, 255, 255, (Uint8)((1.0f - p) * 255));
                        SDL_Rect brightLine = { baseX, lineY - 3, totalWidth, 6 };
                        SDL_RenderFillRect(ren, &brightLine);
                        SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);
                    }
                    SDL_Color fcColor;
                    uint32_t t = nowFC / 80;
                    if (t % 3 == 0)      fcColor = {0, 255, 255, 255};
                    else if (t % 3 == 1) fcColor = {0, 200, 255, 255};
                    else                  fcColor = {255, 255, 255, 255};
                    renderer.drawText(ren, "FULL COMBO", laneCenterX, 200, fcColor, true, true);
                    SDL_RenderPresent(ren);
#ifdef __SWITCH__
                    if (!appletMainLoop()) break;
#endif
                }
                playing = false; break;           
            }
        }
        if (cur_ms > s.maxTargetMs + 1500.0) playing = false;
        frameCount++;
        if (now - lastFpsTime >= 1000) { fps = frameCount; frameCount = 0; lastFpsTime = now; }
#ifdef __SWITCH__
        if (!appletMainLoop()) { isAborted = true; playing = false; break; }
#endif
    }

    // ★修正①: ループ終了後に一度だけコピー（FC の場合はループ内でコピー済みなのでスキップ）
    if (!fcEffectTriggered) status = engine.getStatus();

    Config::save();

    if (gradTex) SDL_DestroyTexture(gradTex);
    snd.clear();
    bga.cleanup(); 
    if (isAborted) return false; 
    return true;
}

// --- 入力処理 ---
bool ScenePlay::processInput(double cur_ms, uint32_t now, SoundManager& snd, PlayEngine& engine) {
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        if (ev.type == SDL_QUIT) return false;

        if (ev.type == SDL_JOYBUTTONDOWN || ev.type == SDL_JOYBUTTONUP) {
            bool isDown = (ev.type == SDL_JOYBUTTONDOWN);
            int btn = ev.jbutton.button;
            int lane = getLaneFromJoystickButton(btn);

            // ★修正(CRITICAL-1): ループ先頭の cur_ms ではなく
            //   ev.jbutton.timestamp（物理打鍵時刻）から cur_ms を再計算する。
            //   これにより最大16.67ms(60Hzの1フレーム)あったジッターを解消する。
            //   start_ticks はメンバ変数として保持する。
            double hit_ms = (startTicks > 0)
                ? (double)((int64_t)ev.jbutton.timestamp - (int64_t)startTicks)
                : cur_ms;

            if (lane != -1) lanePressed[lane] = isDown;

            if (btn == Config::BTN_EXIT) {
                if (isDown) {
                    if (now - lastStartPressTime < 500) {
                        if (Config::SUDDEN_PLUS > 0) {
                            backupSudden = Config::SUDDEN_PLUS;
                            Config::SUDDEN_PLUS = 0;
                        } else {
                            Config::SUDDEN_PLUS = backupSudden;
                        }
                        lastStartPressTime = 0; 
                    } else {
                        lastStartPressTime = now;
                    }
                }
                startButtonPressed = isDown;
            }
            if (btn == Config::BTN_EFFECT) effectButtonPressed = isDown;
            if (btn == Config::BTN_LANE8_A) scratchUpActive = isDown;
            if (btn == Config::BTN_LANE8_B) scratchDownActive = isDown;

            if (startButtonPressed && effectButtonPressed) { engine.forceFail(); return false; }

            if (isDown && startButtonPressed && lane != -1 && lane <= 7) {
                double currentBPM = engine.getBpmFromMs(cur_ms);
                int effectiveGN = (int)(Config::HS_BASE / (std::max(0.01, Config::HIGH_SPEED) * currentBPM));
                if (lane == 1)      effectiveGN += 10;
                else if (lane == 2) effectiveGN -= 10;
                else if (lane == 3) effectiveGN += 25;
                else if (lane == 4) effectiveGN -= 25;
                else if (lane == 5) effectiveGN += 50;
                else if (lane == 6) effectiveGN -= 50;
                else if (lane == 7) effectiveGN = 1200;
                Config::GREEN_NUMBER = std::clamp(effectiveGN, 1, 9999);
                Config::HIGH_SPEED = (double)Config::HS_BASE / (Config::GREEN_NUMBER * currentBPM);
                continue; 
            }

            if (lane != -1 && !isAutoLane(lane)) {
                if (isDown) {
                    if (!engine.getStatus().isFailed && cur_ms >= -500.0) {
                        int resultJudge = engine.processHit(lane, hit_ms, now, snd);

                        // LN押下時の判定を保存 (押下中ボム演出で参照)
                        lnHitJudge[lane] = resultJudge;

                        bool found = false;
                        for (size_t ei = 0; ei < effectCount; ++ei) {
                            if (effectsBuf[ei].lane == lane) {
                                effectsBuf[ei].startTime = now;
                                found = true;
                                break;
                            }
                        }
                        if (!found && effectCount < MAX_EFFECTS)
                            effectsBuf[effectCount++] = {lane, now};

                        if (resultJudge >= 2) {
                            int bombType = (resultJudge == 3) ? 1 : 2;
                            if (bombCount < MAX_BOMBS)
                                bombAnimsBuf[bombCount++] = {lane, now, bombType};
                        }
                    }
                } else {
                    if (!engine.getStatus().isFailed && cur_ms >= -500.0) {
                        engine.processRelease(lane, hit_ms, now);
                        lnHitJudge[lane] = 0;  // 離したらクリア
                    }
                }
            }
        }
    }
    if (startButtonPressed && (scratchUpActive || scratchDownActive)) {
        int delta = scratchUpActive ? -2 : 2; 
        Config::SUDDEN_PLUS = std::clamp(Config::SUDDEN_PLUS + delta, 0, 1000);
        if (Config::SUDDEN_PLUS > 0) backupSudden = Config::SUDDEN_PLUS;
    }
    return true;
}

void ScenePlay::renderScene(SDL_Renderer* ren, NoteRenderer& renderer, PlayEngine& engine, BgaManager& bga, double cur_ms, int64_t cur_y, int fps, const BMSHeader& header, uint32_t now, double progress) {
    SDL_SetRenderDrawColor(ren, 10, 10, 15, 255);
    SDL_RenderClear(ren);
    renderer.renderBackground(ren);
    int bgaX = (Config::PLAY_SIDE == 1) ? 600 : 40;
    int bgaY = 40;
    double currentBpm = engine.getBpmFromMs(cur_ms);
    renderer.renderUI(ren, header, fps, currentBpm, engine.getStatus().exScore);
    bga.render(cur_y, ren, bgaX, bgaY, cur_ms);
    renderer.renderLanes(ren, progress,
        scratchUpActive ? 1 : (scratchDownActive ? 2 : 0));

    // pixels_per_y: 1Yユニットあたりのピクセル数（BPM非依存の定数）
    // ハイスピード・画面サイズで決まる唯一の定数。これを軸に全描画が決まる。
    // 475 は基準値（旧コードからの継承）、60000/res は 1Yユニット = 何ms か
    double pixels_per_y = (Config::HIGH_SPEED * 60000.0) / (475.0 * header.resolution);

    // 可視範囲をYユニットで計算
    double max_visible_y = (double)Config::VISIBLE_PX / std::max(1e-9, pixels_per_y) + 1000.0;

    // 小節線描画
    for (const auto& bl : engine.getBeatLines()) {
        double diff_y = (double)(bl.y - cur_y);
        if (diff_y > -2000.0 && diff_y < max_visible_y) {
            renderer.renderBeatLine(ren, diff_y, pixels_per_y);
        }
    }

    // --- ノーツ描画（スライディング・ウィンドウ）---
    // ★修正: notes を先に描画し、effects/bombs を後から重ねる。
    //        旧実装では bombs → notes の順だったため、LNの上にボムが隠れていた。
    const auto& allNotes = engine.getNotes();

    while (drawStartIndex < allNotes.size()
           && allNotes[drawStartIndex].target_ms < cur_ms - 1000.0
           && !allNotes[drawStartIndex].isBeingPressed) {
        drawStartIndex++;
    }

    for (size_t i = drawStartIndex; i < allNotes.size(); ++i) {
        const auto& n = allNotes[i];

        double y_diff = (double)(n.y - cur_y);
        if (!n.isBeingPressed && y_diff > max_visible_y) break;

        if ((!n.played || n.isBeingPressed) && !n.isBGM) {
            double end_y_diff = n.isLN ? (double)(n.y + n.l - cur_y) : y_diff;
            if (end_y_diff > -5000.0) {
                renderer.renderNote(ren, n, cur_y, pixels_per_y, isAutoLane(n.lane));
            }
        }
    }

    // エフェクト(キービーム) → ボムの順で描画 (ノーツより前面)
    // ★修正: 描画ループを NoteRenderer に移動し、ScenePlay は呼び出すだけにする。
    renderer.renderEffects(ren, effectsBuf, effectCount, lanePressed, now);
    renderer.renderBombs(ren, bombAnimsBuf, bombCount, now);

    int laneCenterX = renderer.getLaneCenterX();

    auto& judge = engine.getCurrentJudge();
    if (judge.active) {
        float p_raw = (float)(now - judge.startTime) / 500.0f;
        if (p_raw >= 1.0f) judge.active = false;
        else {
            if (judge.kind == JudgeKind::PGREAT || (now / 32) % 2 != 0) {
                renderer.renderJudgment(ren, judge.kind, 0.0f, engine.getStatus().combo);
            }
            // ★新規: FAST/SLOW 表示 (NoteRenderer に実装)
            if (judge.isFast || judge.isSlow) {
                renderer.renderFastSlow(ren, judge.isFast, judge.isSlow, p_raw);
            }
        }
    }

    renderer.renderCombo(ren, engine.getStatus().combo);
    renderer.renderGauge(ren, engine.getStatus().gauge, Config::GAUGE_OPTION, engine.getStatus().isFailed);

    if (startButtonPressed) {
        double hs = std::max(0.01, Config::HIGH_SPEED);
        int effectiveHeight = Config::JUDGMENT_LINE_Y - Config::SUDDEN_PLUS;
        auto calcSyncGN = [&](double bpm) {
            return (int)((Config::HS_BASE / (hs * bpm)) * (double)effectiveHeight / Config::JUDGMENT_LINE_Y);
        };
        char gearText[256];
        snprintf(gearText, sizeof(gearText), "GN: %d | SUD+:%d LIFT:%d", calcSyncGN(currentBpm), Config::SUDDEN_PLUS, Config::LIFT);
        // ★修正: drawText → drawTextCached に変更。
        //        drawText は毎回 TTF_RenderUTF8_Blended → SDL_CreateTextureFromSurface → SDL_DestroyTexture を行う。
        //        START ボタン押下中は毎フレーム走り、TTF レンダリングが 0.5-2ms のスパイクになっていた。
        //        drawTextCached はキャッシュにヒットする限りテクスチャを再利用する。
        //        GN値は連続的に変化するが、変化しているフレームのみ TTF が走り、停止中はゼロコスト。
        renderer.drawTextCached(ren, gearText, laneCenterX, 20, {0, 255, 0, 255}, false, true);
    }
    SDL_RenderPresent(ren);
}
