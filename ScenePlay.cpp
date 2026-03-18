#include "ScenePlay.hpp"
#include "PlayEngine.hpp"
#include "SoundManager.hpp"
#include "Config.hpp"
#include "SceneResult.hpp"
#include "BgaManager.hpp"
#include "Logger.hpp"
#include "BmsLoader.hpp"
#include <cmath>
#include <algorithm>
#include <set>
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

// 2P 用ボタン→レーン変換（BTN_2P_* を使用）
static int getLane2P(int btn) {
    if (btn == Config::BTN_2P_LANE1) return 1;
    if (btn == Config::BTN_2P_LANE2) return 2;
    if (btn == Config::BTN_2P_LANE3) return 3;
    if (btn == Config::BTN_2P_LANE4) return 4;
    if (btn == Config::BTN_2P_LANE5) return 5;
    if (btn == Config::BTN_2P_LANE6) return 6;
    if (btn == Config::BTN_2P_LANE7) return 7;
    if (btn == Config::BTN_2P_LANE8_A || btn == Config::BTN_2P_LANE8_B) return 8;
    return -1;
}

bool ScenePlay::isAutoLane(int lane) {
    if (Config::ASSIST_OPTION == 7) return true; // AUTO PLAY: 全レーンオート
    bool autoScr = (Config::ASSIST_OPTION == 1 || Config::ASSIST_OPTION == 4 || 
                    Config::ASSIST_OPTION == 5 || Config::ASSIST_OPTION == 6);
    bool auto5k = (Config::ASSIST_OPTION == 3 || Config::ASSIST_OPTION == 5 || 
                    Config::ASSIST_OPTION == 6);
    // ASSIST: SCR ONLY (8) = 7鍵盤をすべてオートにしてスクラッチのみプレイ
    if (Config::ASSIST_OPTION == 8 && lane != 8) return true;
    if (lane == 8 && autoScr) return true;
    if ((lane == 6 || lane == 7) && auto5k) return true;
    return false;
}

void ScenePlay::updateAssist(double cur_ms, PlayEngine& engine) {
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
                int resultJudge = engine.processHit(n.lane, n.target_ms, now, isPartialAuto);

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
                if (n.isLN) {
                    lnHitJudge[n.lane]     = resultJudge;
                    lastLNBombTime[n.lane] = now;
                }
            }
            // ★LNモード: update()でP-GREAT確定済みなのでここには来ないはずだが、
            //   CN/HCNモードは従来通り終端msでprocessReleaseを呼ぶ。
            if (n.isLN && n.isBeingPressed && Config::LN_OPTION != 2
                && cur_ms >= n.target_ms + n.duration_ms) {
                engine.processRelease(n.lane, n.target_ms + n.duration_ms, now);
                lnHitJudge[n.lane]     = 0;
                lastLNBombTime[n.lane] = 0;
            }
        }
    }
}

bool ScenePlay::run(SDL_Renderer* ren, NoteRenderer& renderer, const std::string& bmsonPath) {
    LOG_INFO("ScenePlay", "=== run() start: '%s' ===", bmsonPath.c_str());
    numPlayers = 1; // ★修正: runVS()後も正しく1Pモードに戻るようにリセット
    SoundManager& snd = SoundManager::getInstance();
    // ─────────────────────────────────────────────
    // フェーズ 1: 前曲クリーンアップ
    // ─────────────────────────────────────────────
    LOG_INFO("ScenePlay", "Phase1: snd.clear() start");
    snd.clear();
    LOG_INFO("ScenePlay", "Phase1: snd.clear() done");
    SDL_RenderClear(ren);
    SDL_RenderPresent(ren);
    SDL_Delay(200);

    // ─────────────────────────────────────────────
    // フェーズ 2: BMSON パース（暗転のまま）
    // ─────────────────────────────────────────────
    {
        SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
        SDL_RenderClear(ren);
        SDL_RenderPresent(ren);
    }
    BMSData data;
    if (BmsLoader::isBmsFile(bmsonPath)) {
        data = BmsLoader::load(bmsonPath, [&](float /*progress*/) {
            SDL_Event e; while (SDL_PollEvent(&e));
        });
    } else {
        data = BmsonLoader::load(bmsonPath, [&](float /*progress*/) {
            SDL_Event e; while (SDL_PollEvent(&e));
        });
    }

    if (data.sound_channels.empty()) {
        LOG_WARN("ScenePlay", "sound_channels empty, aborting");
        return true;
    }
    LOG_INFO("ScenePlay", "Phase2: chart loaded: title='%s' channels=%zu bpm=%.1f resolution=%d",
             data.header.title.c_str(), data.sound_channels.size(),
             data.header.bpm, data.header.resolution);

    // パース完了後にデータ構造のスコープを整理（bmsonの場合はJSON DOMが既に解放済み）
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
    LOG_INFO("ScenePlay", "Phase3: engine.init done: totalNotes=%d", engine.getStatus().totalNotes);
    drawStartIndex = 0;

    BgaManager bga;
    bga.init(data.bga_images.size());
    bga.setLayout(renderer.getBgaCenterX()); // NoteRendererが計算した値をBgaManagerに渡す
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
    startButtonPressed  = false;
    decideButtonPressed  = false;
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

    // エンジン・BGA 準備完了 → まだ暗転のまま（音声ロード後にフェードイン）

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

    auto showLoadingScreen = [&](const char* /*text*/) {
        // 読み込み中は暗転のまま（音声ロード後にフェードイン）
        SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
        SDL_RenderClear(ren);
        SDL_RenderPresent(ren);
        SDL_Event e; while (SDL_PollEvent(&e));
    };

    LOG_INFO("ScenePlay", "Phase4: preloadBoxIndex start: dir='%s' base='%s'",
             bmsonDir.c_str(), bmsonBaseName.c_str());
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

    LOG_INFO("ScenePlay", "Phase4: soundList BGM=%zu keys=%zu total=%zu",
             soundListBGM.size(), soundListKeys.size(), soundList.size());
    // 先頭BGMファイル名をログ（原因調査用）
    if (!soundListBGM.empty())
        LOG_INFO("ScenePlay", "Phase4: first BGM channel='%s'", soundListBGM[0].c_str());

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

    // 音声ロード完了 → フェードインしてプレイ画面を表示
    {
        int64_t cur_y_wait = engine.getYFromMs(-2000.0);
        fadeIn(ren, renderer, engine, bga, -2000.0, cur_y_wait, currentHeader, SDL_GetTicks(), 500);
    }

    // 待機ループ: DECIDEボタンを押すまで HS/サドプラ/リフト調整可能
    // STARTボタンはHS変更等に使うため、曲開始はDECIDEボタン(SYS_BTN_DECIDE)に分離。
    // processInput 内で decideButtonPressed をセットし、ここで検知して待機を終了する。
    // startTicks を仮設定しておくことで processInput 内の HS 変更計算を安定させる。
    startTicks = SDL_GetTicks() + 99999999; // 待機中はゲームが始まらない大きな値
    bool waiting = true;
    while (waiting) {
        uint32_t now = SDL_GetTicks();
        // processInput を先に呼んでイベントを全消費、その中で HS/サドプラ変更も処理
        if (!processInput(-2000.0, now, engine)) return false;
        // processInput 後に残ったフラグでDECIDEボタン確認
        // STARTボタンは待機中もHS/サドプラ変更に使うため競合しないよう分離
        // STARTボタン長押し中はDECIDEを無効化: HS変更ボタンにDECIDEが割り当たっていても誤爆しない
        if (decideButtonPressed && !startButtonPressed) {
            waiting = false;
        }
        bga.preLoad(0, ren);
        renderScene(ren, renderer, engine, bga, -2000.0, 0, 0, currentHeader, now, 0.0);
        renderer.drawTextCached(ren, "PRESS DECIDE BUTTON TO START",
                                renderer.getLaneCenterX(), 450, {255, 255, 255, 255}, false, true);
        SDL_RenderPresent(ren);
#ifdef __SWITCH__
        if (!appletMainLoop()) return false;
#endif
    }
    startButtonPressed  = false; // 待機終了後はリセット
    decideButtonPressed  = false;

    uint32_t start_ticks = SDL_GetTicks() + 2000;
    startTicks = start_ticks;
    LOG_INFO("ScenePlay", "=== Play loop start: start_ticks=%u maxTargetMs=%.0f ===",
             start_ticks, max_target_ms);
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
        // gradTex は FC エフェクト用。失敗してもゲームは続行できるが
        // 失敗が続く場合は VRAM 不足や SDL_Renderer 破損のサインになる。
        if (!gradTex) LOG_WARN("ScenePlay", "gradTex creation failed (FC effect disabled): %s", SDL_GetError());
    }

    bool firstFrame = true;
    while (playing) {
        uint32_t now = SDL_GetTicks();

        // Play loop の最初のフレームに入れたことを記録する。
        // ここに到達せずクラッシュした場合はその直前のログ（gradTex 行など）が手がかりになる。
        if (firstFrame) {
            LOG_INFO("ScenePlay", "Play loop first frame: now=%u", now);
            firstFrame = false;
        }

        double cur_ms = (double)((int64_t)now - (int64_t)start_ticks);

        bga.syncTime(std::max(0.0, cur_ms - videoOffsetMs));

        if (!processInput(cur_ms, now, engine)) {
            if (engine.getStatus().isFailed) playing = false;
            else { isAborted = true; playing = false; break; }
        }
        updateAssist(cur_ms, engine);

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

        engine.update(cur_ms + 10.0, now);
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
                    if (!processInput(cur_ms, nowFC, engine)) break;

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
                    // 【指摘(3-4)修正】drawText → drawTextCached
                    // "FULL COMBO" は固定文字列。色が3パターン=最大3エントリのキャッシュで
                    // 100%ヒットする。毎フレームの TTF_RenderUTF8_Blended(0.5~2ms)を排除。
                    renderer.drawTextCached(ren, "FULL COMBO", laneCenterX, 200, fcColor, true, true);
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

    LOG_INFO("ScenePlay", "=== Play loop end: isAborted=%d score(PG=%d GR=%d PR=%d BD=%d) combo=%d ===",
             isAborted ? 1 : 0,
             status.pGreatCount, status.greatCount, status.poorCount, status.badCount,
             status.combo);

    if (gradTex) SDL_DestroyTexture(gradTex);

    // フェードアウト: 通常終了・強制終了ともにフェード中はBGA・ノーツを動かし続け、
    // フェード完了後に bga/snd を停止する（先に止めると画面が固まって見栄えが悪い）
    {
        uint32_t fadeNow = SDL_GetTicks();
        double cur_ms_end = (double)((int64_t)fadeNow - (int64_t)start_ticks);
        int64_t cur_y_end = engine.getYFromMs(cur_ms_end);
        fadeOut(ren, renderer, engine, bga, cur_ms_end, cur_y_end, currentHeader, fadeNow, 500);
    }

    snd.clear();
    bga.cleanup();

    // 【CRITICAL-3修正】Config::saveAsync() はバックグラウンドスレッドで書き込むため
    // メインスレッドをブロックしない。旧 save() の 100ms スパイクが解消される。
    Config::markDirty();
    Config::saveAsync();
    if (isAborted) return false; 
    return true;
}

// --- キーボード → 仮想ジョイスティックボタン変換 (Mac/PC用) ---
static int keyToJoyButton(SDL_Keycode key) {
    // プレイキー: z=lane1, s=lane2, x=lane3, d=lane4, c=lane5, f=lane6, v=lane7
    // g=scratch_up, b=scratch_down, a=start, e=effect
    // Enter=decide（待機ループ解除）
    switch (key) {
        case SDLK_z: return Config::BTN_LANE1;
        case SDLK_s: return Config::BTN_LANE2;
        case SDLK_x: return Config::BTN_LANE3;
        case SDLK_d: return Config::BTN_LANE4;
        case SDLK_c: return Config::BTN_LANE5;
        case SDLK_f: return Config::BTN_LANE6;
        case SDLK_v: return Config::BTN_LANE7;
        case SDLK_g: return Config::BTN_LANE8_A;   // scratch up
        case SDLK_b: return Config::BTN_LANE8_B;   // scratch down
        case SDLK_a: return Config::BTN_EXIT;       // start
        case SDLK_e: return Config::BTN_EFFECT;     // effect
        case SDLK_RETURN:
        case SDLK_KP_ENTER: return Config::SYS_BTN_DECIDE; // 曲開始
        default:     return -1;
    }
}

// --- 入力処理 ---
bool ScenePlay::processInput(double cur_ms, uint32_t now, PlayEngine& engine) {
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        if (ev.type == SDL_QUIT) return false;

        // ★Mac/PC: キーボードをジョイスティックイベントに変換して以降の処理に流す
        if (ev.type == SDL_KEYDOWN || ev.type == SDL_KEYUP) {
            if (ev.key.repeat) continue; // キーリピートは無視
            int btn = keyToJoyButton(ev.key.keysym.sym);
            if (btn < 0) continue;
            ev.type        = (ev.type == SDL_KEYDOWN) ? SDL_JOYBUTTONDOWN : SDL_JOYBUTTONUP;
            ev.jbutton.button    = (Uint8)btn;
            ev.jbutton.timestamp = ev.key.timestamp;
            // fall through → 下のJOYBUTTON処理へ
        }

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
            if (btn == Config::SYS_BTN_DECIDE) decideButtonPressed = isDown;
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
                        int resultJudge = engine.processHit(lane, hit_ms, now);

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
                        // ★HCNモード: 離しても lnHitJudge を維持（ティック中ボム演出継続）
                        // CN/LNは従来通りクリア
                        if (Config::LN_OPTION != 1) lnHitJudge[lane] = 0;
                    }
                }
            }
        }
    }
    if (startButtonPressed && (scratchUpActive || scratchDownActive)) {
        int delta = scratchUpActive ? -2 : 2;
        if (Config::SUDDEN_PLUS == 0) {
            // サドプラなし状態ではリフトを操作
            Config::LIFT = std::clamp(Config::LIFT + delta, 0, 1000);
        } else {
            Config::SUDDEN_PLUS = std::clamp(Config::SUDDEN_PLUS + delta, 0, 1000);
            if (Config::SUDDEN_PLUS > 0) backupSudden = Config::SUDDEN_PLUS;
        }
    }
    return true;
}

void ScenePlay::fadeIn(SDL_Renderer* ren, NoteRenderer& renderer, PlayEngine& engine,
                       BgaManager& bga, double cur_ms, int64_t cur_y,
                       const BMSHeader& header, uint32_t baseNow, int durationMs) {
    SDL_Rect screen = { 0, 0, Config::SCREEN_WIDTH, Config::SCREEN_HEIGHT };

#ifdef __SWITCH__
    // Switch: スナップショットテクスチャを使ったフェードイン（従来通り）
    SDL_Texture* snap = SDL_CreateTexture(ren,
        SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET,
        Config::SCREEN_WIDTH, Config::SCREEN_HEIGHT);
    if (snap) {
        SDL_SetRenderTarget(ren, snap);
        renderScene(ren, renderer, engine, bga, cur_ms, cur_y, 0, header, baseNow, 0.0);
        SDL_SetRenderTarget(ren, nullptr);
    } else {
        LOG_WARN("ScenePlay", "fadeIn: SDL_CreateTexture(TARGET) failed: %s", SDL_GetError());
    }
    uint32_t start = SDL_GetTicks();
    while (true) {
        uint32_t frameStart = SDL_GetTicks();
        float t = std::min(1.0f, (float)(frameStart - start) / (float)durationMs);
        if (snap) {
            SDL_RenderCopy(ren, snap, nullptr, nullptr);
        } else {
            renderScene(ren, renderer, engine, bga, cur_ms, cur_y, 0, header, baseNow, 0.0);
        }
        Uint8 alpha = (Uint8)((1.0f - t) * 255);
        if (alpha > 0) {
            SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(ren, 0, 0, 0, alpha);
            SDL_RenderFillRect(ren, &screen);
            SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);
        }
        SDL_RenderPresent(ren);
        SDL_Event e; while (SDL_PollEvent(&e)) {}
        if (!appletMainLoop()) break;
        if (t >= 1.0f) break;
        uint32_t elapsed = SDL_GetTicks() - frameStart;
        if (elapsed < 16) SDL_Delay(16 - elapsed);
    }
    if (snap) SDL_DestroyTexture(snap);
#else
    // macOS/PC: SDL_TEXTUREACCESS_TARGET はデコードスレッド実行中に
    // Metal コンテキスト競合を起こすため使用しない。
    // 毎フレーム renderScene を呼ぶシンプルなフェードイン。
    uint32_t start = SDL_GetTicks();
    while (true) {
        uint32_t frameStart = SDL_GetTicks();
        float t = std::min(1.0f, (float)(frameStart - start) / (float)durationMs);
        renderScene(ren, renderer, engine, bga, cur_ms, cur_y, 0, header, baseNow, 0.0);
        Uint8 alpha = (Uint8)((1.0f - t) * 255);
        if (alpha > 0) {
            SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(ren, 0, 0, 0, alpha);
            SDL_RenderFillRect(ren, &screen);
            SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);
        }
        SDL_RenderPresent(ren);
        SDL_Event e; while (SDL_PollEvent(&e)) {}
        if (t >= 1.0f) break;
        uint32_t elapsed = SDL_GetTicks() - frameStart;
        if (elapsed < 16) SDL_Delay(16 - elapsed);
    }
#endif
}


void ScenePlay::fadeOut(SDL_Renderer* ren, NoteRenderer& renderer, PlayEngine& engine,
                        BgaManager& bga, double cur_ms, int64_t cur_y,
                        const BMSHeader& header, uint32_t baseNow, int durationMs) {
    SDL_Rect screen = { 0, 0, Config::SCREEN_WIDTH, Config::SCREEN_HEIGHT };
    uint32_t start = SDL_GetTicks();
    while (true) {
        uint32_t frameStart = SDL_GetTicks();
        float t = std::min(1.0f, (float)(frameStart - start) / (float)durationMs);

        // フェード中も時間を進めてライブ描画（BGA・ノーツが動き続ける）
        // cur_ms / cur_y は呼び出し時点の値を起点に、経過時間で更新する
        double live_ms = cur_ms + (double)(frameStart - baseNow);
        int64_t live_y  = engine.getYFromMs(live_ms);
        bga.syncTime(live_ms);
        renderScene(ren, renderer, engine, bga, live_ms, live_y, 0, header, frameStart, 1.0);

        // 黒矩形を t の不透明度で重ねる（透明→黒へ遷移 = フェードアウト）
        Uint8 alpha = (Uint8)(t * 255);
        if (alpha > 0) {
            SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(ren, 0, 0, 0, alpha);
            SDL_RenderFillRect(ren, &screen);
            SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);
        }
        SDL_RenderPresent(ren);
        SDL_Event e; while (SDL_PollEvent(&e)) {}
#ifdef __SWITCH__
        if (!appletMainLoop()) break;
#endif
        if (t >= 1.0f) break;
        // VSync が効いていない場合でも約16ms/frame を下限として保証（CPU 100% 防止）
        uint32_t elapsed = SDL_GetTicks() - frameStart;
        if (elapsed < 16) SDL_Delay(16 - elapsed);
    }
}

void ScenePlay::renderScene(SDL_Renderer* ren, NoteRenderer& renderer, PlayEngine& engine, BgaManager& bga, double cur_ms, int64_t cur_y, int fps, const BMSHeader& header, uint32_t now, double progress) {
    // 【CRITICAL-2対策】フレーム先頭で Config 値をスナップショットコピーする。
    // ARM Cortex-A57 の弱メモリモデルでは、double の torn read が起きうる。
    // BGA スレッドがフレーム途中に HIGH_SPEED を読むと中途半端な値になる。
    // フレーム先頭で 1 回コピーすれば、このフレーム内の一貫性が保たれる。
    const int   fc_playSide   = Config::PLAY_SIDE;
    const int   fc_visiblePx  = Config::VISIBLE_PX;
    const int   fc_suddenPlus = Config::SUDDEN_PLUS;
    const int   fc_lift       = Config::LIFT;

    SDL_SetRenderDrawColor(ren, 10, 10, 15, 255);
    SDL_RenderClear(ren);
    renderer.renderBackground(ren);
    int bgaX = (fc_playSide == 1) ? 600 : 40;
    int bgaY = 40;
    double currentBpm = engine.getBpmFromMs(cur_ms);
    renderer.renderUI(ren, header, fps, currentBpm, engine.getStatus().exScore);
    bga.render(cur_y, ren, bgaX, bgaY, cur_ms);
    renderer.renderLanes(ren, progress,
        scratchUpActive ? 1 : (scratchDownActive ? 2 : 0));

    // pixels_per_y: 1Yユニットあたりのピクセル数（BPM非依存の定数）
    // ハイスピード・画面サイズで決まる唯一の定数。これを軸に全描画が決まる。
    // 475 は基準値（旧コードからの継承）、60000/res は 1Yユニット = 何ms か
    //
    // 【指摘(3-5)修正】HIGH_SPEED が変わったときだけ再計算するキャッシュを導入。
    // double の除算は ARM Cortex-A57 で ~15 サイクル。毎フレーム計算する必要はない。
    // HIGH_SPEED は START+レーンキーで変更されたときだけ変わるため、
    // 変化を検出して cachedPixelsPerY / cachedMaxVisibleY を更新する。
    if (Config::HIGH_SPEED != cachedHS_ || header.resolution != cachedResolution_) {
        cachedHS_          = Config::HIGH_SPEED;
        cachedResolution_  = header.resolution;
        cachedPixelsPerY_  = (cachedHS_ * 60000.0) / (475.0 * header.resolution);
        cachedMaxVisibleY_ = (double)Config::VISIBLE_PX / std::max(1e-9, cachedPixelsPerY_) + 1000.0;
    }
    const double pixels_per_y  = cachedPixelsPerY_;

    // 可視範囲をYユニットで計算
    const double max_visible_y = cachedMaxVisibleY_;

    // 小節線描画
    for (const auto& bl : engine.getBeatLines()) {
        double diff_y = (double)(bl.y - cur_y);
        if (diff_y > -2000.0 && diff_y < max_visible_y) {
            renderer.renderBeatLine(ren, diff_y, pixels_per_y);
        }
    }

    // --- ノーツ描画: 2パス（単発→LN の順）---
    const auto& allNotes = engine.getNotes();

    while (drawStartIndex < allNotes.size()
           && allNotes[drawStartIndex].target_ms < cur_ms - 1000.0
           && !allNotes[drawStartIndex].isBeingPressed) {
        drawStartIndex++;
    }

    // パス1: 単発ノーツ
    for (size_t i = drawStartIndex; i < allNotes.size(); ++i) {
        const auto& n = allNotes[i];
        double y_diff = (double)(n.y - cur_y);
        if (!n.isBeingPressed && y_diff > max_visible_y) break;
        if ((!n.played || n.isBeingPressed) && !n.isBGM && !n.isLN) {
            double end_y_diff = (double)(n.y - cur_y);
            if (end_y_diff > -5000.0) {
                renderer.renderNote(ren, n, cur_y, pixels_per_y, isAutoLane(n.lane));
            }
        }
    }
    // パス2: LNノーツ
    for (size_t i = drawStartIndex; i < allNotes.size(); ++i) {
        const auto& n = allNotes[i];
        double y_diff = (double)(n.y - cur_y);
        if (!n.isBeingPressed && y_diff > max_visible_y) break;
        if ((!n.played || n.isBeingPressed) && !n.isBGM && n.isLN) {
            double end_y_diff = (double)(n.y + n.l - cur_y);
            if (end_y_diff > -5000.0) {
                renderer.renderNote(ren, n, cur_y, pixels_per_y, isAutoLane(n.lane));
            }
        }
    }

    // SUDDEN_PLUS/LIFTオーバーレイをノーツの上に描画
    renderer.renderSuddenLift(ren);

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
                renderer.renderFastSlow(ren, judge.isFast, judge.isSlow, p_raw, judge.diffMs);
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

// ============================================================
//  ★2P VS モード
// ============================================================

// キーボード → 2P仮想ボタン変換
static int keyToJoyButton2P(SDL_Keycode key) {
    switch (key) {
        case SDLK_j:         return Config::BTN_LANE1;
        case SDLK_k:         return Config::BTN_LANE2;
        case SDLK_l:         return Config::BTN_LANE3;
        case SDLK_SEMICOLON: return Config::BTN_LANE4;
        case SDLK_COMMA:     return Config::BTN_LANE5;
        case SDLK_PERIOD:    return Config::BTN_LANE6;
        case SDLK_SLASH:     return Config::BTN_LANE7;
        case SDLK_h:         return Config::BTN_LANE8_A;
        case SDLK_n:         return Config::BTN_LANE8_B;
        default:             return -1;
    }
}

void ScenePlay::handlePlayerButton(int playerIdx, int lane, bool isDown,
                                    double hit_ms, uint32_t now,
                                    PlayEngine& engine, PlayerState& ps) {
    if (ps.isPlayerFailed) return;
    if (lane < 1 || lane > 8) return;

    ps.lanePressed[lane] = isDown;

    if (!isAutoLane(lane)) {
        if (isDown) {
            if (!engine.getStatus().isFailed && hit_ms >= -500.0) {
                int resultJudge = engine.processHit(lane, hit_ms, now);
                ps.lnHitJudge[lane] = resultJudge;

                bool found = false;
                for (size_t ei = 0; ei < ps.effectCount; ++ei) {
                    if (ps.effectsBuf[ei].lane == lane) {
                        ps.effectsBuf[ei].startTime = now;
                        found = true;
                        break;
                    }
                }
                if (!found && ps.effectCount < PlayerState::MAX_EFFECTS)
                    ps.effectsBuf[ps.effectCount++] = {lane, now};

                if (resultJudge >= 2) {
                    int bombType = (resultJudge == 3) ? 1 : 2;
                    if (ps.bombCount < PlayerState::MAX_BOMBS)
                        ps.bombAnimsBuf[ps.bombCount++] = {lane, now, bombType};
                }
            }
        } else {
            if (!engine.getStatus().isFailed && hit_ms >= -500.0) {
                engine.processRelease(lane, hit_ms, now);
                // ★HCNモード: 離しても lnHitJudge を維持（ティック中ボム演出継続）
                if (Config::LN_OPTION != 1) ps.lnHitJudge[lane] = 0;
            }
        }
    }
}

bool ScenePlay::processInputVS(double cur_ms, uint32_t now,
                                PlayEngine& engine1P, PlayEngine& engine2P,
                                SDL_JoystickID joy1ID, SDL_JoystickID joy2ID) {
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        if (ev.type == SDL_QUIT) return false;

        int playerIdx = -1;

        // キーボード入力: 1Pキーか2Pキーかで振り分け
        if (ev.type == SDL_KEYDOWN || ev.type == SDL_KEYUP) {
            if (ev.key.repeat) continue;
            int btn1P = keyToJoyButton(ev.key.keysym.sym);
            int btn2P = keyToJoyButton2P(ev.key.keysym.sym);

            if (btn1P >= 0) {
                playerIdx = 0;
                ev.type = (ev.type == SDL_KEYDOWN) ? SDL_JOYBUTTONDOWN : SDL_JOYBUTTONUP;
                ev.jbutton.button = (Uint8)btn1P;
                ev.jbutton.timestamp = ev.key.timestamp;
            } else if (btn2P >= 0) {
                playerIdx = 1;
                ev.type = (ev.type == SDL_KEYDOWN) ? SDL_JOYBUTTONDOWN : SDL_JOYBUTTONUP;
                ev.jbutton.button = (Uint8)btn2P;
                ev.jbutton.timestamp = ev.key.timestamp;
            } else {
                continue;
            }
        }

        if (ev.type == SDL_JOYBUTTONDOWN || ev.type == SDL_JOYBUTTONUP) {
            if (playerIdx < 0) {
                SDL_JoystickID id = ev.jbutton.which;
                if (id == joy2ID && joy2ID >= 0)
                    playerIdx = 1;
                else
                    playerIdx = 0;
            }

            bool isDown = (ev.type == SDL_JOYBUTTONDOWN);
            int btn = ev.jbutton.button;
            // 2P は BTN_2P_* で引き当て、1P は BTN_* で引き当て
            int lane = (playerIdx == 1) ? getLane2P(btn) : getLaneFromJoystickButton(btn);

            double hit_ms = (startTicks > 0)
                ? (double)((int64_t)ev.jbutton.timestamp - (int64_t)startTicks)
                : cur_ms;

            // スクラッチ状態更新（各プレイヤーのボタン定義で判定）
            PlayerState& curPs = players[playerIdx];
            if (playerIdx == 0) {
                if (btn == Config::BTN_LANE8_A) curPs.scratchUpActive = isDown;
                if (btn == Config::BTN_LANE8_B) curPs.scratchDownActive = isDown;
            } else {
                if (btn == Config::BTN_2P_LANE8_A) curPs.scratchUpActive = isDown;
                if (btn == Config::BTN_2P_LANE8_B) curPs.scratchDownActive = isDown;
            }

            // START ボタン検出（各プレイヤー独立）
            bool isStartBtn = (playerIdx == 0) ? (btn == Config::BTN_EXIT)
                                               : (btn == Config::BTN_2P_EXIT);
            if (isStartBtn) {
                if (isDown) {
                    uint32_t& lastPress = (playerIdx == 0) ? lastStartPressTime : last2PStartPressTime;
                    if (now - lastPress < 500) {
                        PlayerState& ps = players[playerIdx];
                        if (ps.suddenPlus > 0) {
                            ps.backupSudden = ps.suddenPlus;
                            ps.suddenPlus = 0;
                        } else {
                            ps.suddenPlus = ps.backupSudden;
                        }
                        lastPress = 0;
                    } else {
                        (playerIdx == 0 ? lastStartPressTime : last2PStartPressTime) = now;
                    }
                }
                if (playerIdx == 0) startButtonPressed = isDown;
                else                start2PButtonPressed = isDown;
            }

            // 1P のみ: DECIDE/EFFECT/強制終了
            if (playerIdx == 0) {
                if (btn == Config::SYS_BTN_DECIDE) decideButtonPressed = isDown;
                if (btn == Config::BTN_EFFECT) effectButtonPressed = isDown;
                if (startButtonPressed && effectButtonPressed) {
                    engine1P.forceFail();
                    engine2P.forceFail();
                    return false;
                }
            }

            // HS変更（STARTボタン + レーンキー、各プレイヤー独立）
            bool thisPlayerStart = (playerIdx == 0) ? startButtonPressed : start2PButtonPressed;
            if (isDown && thisPlayerStart && lane >= 1 && lane <= 7) {
                PlayerState& ps = players[playerIdx];
                PlayEngine& eng = (playerIdx == 0) ? engine1P : engine2P;
                double currentBPM = eng.getBpmFromMs(cur_ms);
                int effectiveGN = (int)(Config::HS_BASE / (std::max(0.01, ps.highSpeed) * currentBPM));
                if (lane == 1)      effectiveGN += 10;
                else if (lane == 2) effectiveGN -= 10;
                else if (lane == 3) effectiveGN += 25;
                else if (lane == 4) effectiveGN -= 25;
                else if (lane == 5) effectiveGN += 50;
                else if (lane == 6) effectiveGN -= 50;
                else if (lane == 7) effectiveGN = 1200;
                ps.greenNumber = std::clamp(effectiveGN, 1, 9999);
                ps.highSpeed = (double)Config::HS_BASE / (ps.greenNumber * currentBPM);
                continue;
            }

            // プレイキー処理
            if (lane != -1) {
                PlayEngine& eng = (playerIdx == 0) ? engine1P : engine2P;
                PlayerState& ps = players[playerIdx];
                handlePlayerButton(playerIdx, lane, isDown, hit_ms, now, eng, ps);
            }
        }
    }

    // スクラッチ + START でサドプラ/リフト操作（1P・2P 各独立）
    for (int p = 0; p < 2; ++p) {
        bool pStart = (p == 0) ? startButtonPressed : start2PButtonPressed;
        if (pStart) {
            PlayerState& ps = players[p];
            if (ps.scratchUpActive || ps.scratchDownActive) {
                int delta = ps.scratchUpActive ? -2 : 2;
                if (ps.suddenPlus == 0) {
                    ps.lift = std::clamp(ps.lift + delta, 0, 1000);
                } else {
                    ps.suddenPlus = std::clamp(ps.suddenPlus + delta, 0, 1000);
                    if (ps.suddenPlus > 0) ps.backupSudden = ps.suddenPlus;
                }
            }
        }
    }

    return true;
}

void ScenePlay::updateAssistForPlayer(double cur_ms, PlayEngine& engine, PlayerState& ps) {
    if (ps.isPlayerFailed) return;
    uint32_t now = SDL_GetTicks();
    const auto& notes = engine.getNotes();
    for (size_t i = ps.drawStartIndex; i < notes.size(); ++i) {
        const auto& n = notes[i];
        if (n.played || n.isBGM) continue;
        if (n.target_ms > cur_ms + 100) break;

        if (isAutoLane(n.lane)) {
            if (!n.isBeingPressed && cur_ms >= n.target_ms) {
                bool isPartialAuto = (Config::ASSIST_OPTION != 7);
                int resultJudge = engine.processHit(n.lane, n.target_ms, now, isPartialAuto);

                bool found = false;
                for (size_t ei = 0; ei < ps.effectCount; ++ei) {
                    if (ps.effectsBuf[ei].lane == n.lane) {
                        ps.effectsBuf[ei].startTime = now;
                        found = true;
                        break;
                    }
                }
                if (!found && ps.effectCount < PlayerState::MAX_EFFECTS)
                    ps.effectsBuf[ps.effectCount++] = {n.lane, now};
                if (ps.bombCount < PlayerState::MAX_BOMBS)
                    ps.bombAnimsBuf[ps.bombCount++] = {n.lane, now, 2};

                if (n.isLN) {
                    ps.lnHitJudge[n.lane]     = resultJudge;
                    ps.lastLNBombTime[n.lane] = now;
                }
            }
            // ★LNモード: update()でP-GREAT確定済みなのでここには来ないはずだが、
            //   CN/HCNモードは従来通り終端msでprocessReleaseを呼ぶ。
            if (n.isLN && n.isBeingPressed && Config::LN_OPTION != 2
                && cur_ms >= n.target_ms + n.duration_ms) {
                engine.processRelease(n.lane, n.target_ms + n.duration_ms, now);
                ps.lnHitJudge[n.lane]     = 0;
                ps.lastLNBombTime[n.lane] = 0;
            }
        }
    }
}


void ScenePlay::renderPlayerField(SDL_Renderer* ren, NoteRenderer& renderer,
                                   PlayEngine& engine, PlayerState& ps,
                                   int side, double cur_ms, int64_t cur_y,
                                   int fps, uint32_t now, double progress,
                                   const BMSHeader& header) {
    renderer.switchSide(side);

    // プレイヤー別のSUD+/LIFTをConfigに一時反映
    int savedSudden = Config::SUDDEN_PLUS;
    int savedLift   = Config::LIFT;
    double savedHS  = Config::HIGH_SPEED;
    Config::SUDDEN_PLUS = ps.suddenPlus;
    Config::LIFT        = ps.lift;
    Config::HIGH_SPEED  = ps.highSpeed;

    double pixels_per_y = (ps.highSpeed * 60000.0) / (475.0 * header.resolution);
    double max_visible_y = (double)Config::VISIBLE_PX / std::max(1e-9, pixels_per_y) + 1000.0;

    int scratchStatus = ps.scratchUpActive ? 1 : (ps.scratchDownActive ? 2 : 0);
    renderer.renderLanes(ren, progress, scratchStatus);

    for (const auto& bl : engine.getBeatLines()) {
        double diff_y = (double)(bl.y - cur_y);
        if (diff_y > -2000.0 && diff_y < max_visible_y)
            renderer.renderBeatLine(ren, diff_y, pixels_per_y);
    }

    const auto& allNotes = engine.getNotes();
    while (ps.drawStartIndex < allNotes.size()
           && allNotes[ps.drawStartIndex].target_ms < cur_ms - 1000.0
           && !allNotes[ps.drawStartIndex].isBeingPressed) {
        ps.drawStartIndex++;
    }

    for (size_t i = ps.drawStartIndex; i < allNotes.size(); ++i) {
        const auto& n = allNotes[i];
        double y_diff = (double)(n.y - cur_y);
        if (!n.isBeingPressed && y_diff > max_visible_y) break;
        if ((!n.played || n.isBeingPressed) && !n.isBGM && !n.isLN) {
            if ((double)(n.y - cur_y) > -5000.0)
                renderer.renderNote(ren, n, cur_y, pixels_per_y, isAutoLane(n.lane));
        }
    }
    for (size_t i = ps.drawStartIndex; i < allNotes.size(); ++i) {
        const auto& n = allNotes[i];
        double y_diff = (double)(n.y - cur_y);
        if (!n.isBeingPressed && y_diff > max_visible_y) break;
        if ((!n.played || n.isBeingPressed) && !n.isBGM && n.isLN) {
            if ((double)(n.y + n.l - cur_y) > -5000.0)
                renderer.renderNote(ren, n, cur_y, pixels_per_y, isAutoLane(n.lane));
        }
    }

    renderer.renderSuddenLift(ren);
    renderer.renderEffects(ren, ps.effectsBuf, ps.effectCount, ps.lanePressed, now);
    renderer.renderBombs(ren, ps.bombAnimsBuf, ps.bombCount, now);

    auto& judge = engine.getCurrentJudge();
    if (judge.active) {
        float p_raw = (float)(now - judge.startTime) / 500.0f;
        if (p_raw >= 1.0f) judge.active = false;
        else {
            if (judge.kind == JudgeKind::PGREAT || (now / 32) % 2 != 0)
                renderer.renderJudgment(ren, judge.kind, 0.0f, engine.getStatus().combo);
            if (judge.isFast || judge.isSlow)
                renderer.renderFastSlow(ren, judge.isFast, judge.isSlow, p_raw, judge.diffMs);
        }
    }

    renderer.renderCombo(ren, engine.getStatus().combo);
    renderer.renderGauge(ren, engine.getStatus().gauge, Config::GAUGE_OPTION, engine.getStatus().isFailed);

    if (ps.isPlayerFailed) {
        int cx = renderer.getLaneCenterX();
        renderer.drawTextCached(ren, "FAILED", cx, 300, {255, 50, 50, 255}, true, true);
    }

    Config::SUDDEN_PLUS = savedSudden;
    Config::LIFT        = savedLift;
    Config::HIGH_SPEED  = savedHS;
}

bool ScenePlay::runVS(SDL_Renderer* ren, NoteRenderer& renderer,
                       const std::string& path1P, const std::string& path2P) {
    LOG_INFO("ScenePlay", "=== runVS() start: 1P='%s' 2P='%s' ===", path1P.c_str(), path2P.c_str());
    numPlayers = 2;
    SoundManager& snd = SoundManager::getInstance();

    snd.clear();
    SDL_RenderClear(ren); SDL_RenderPresent(ren); SDL_Delay(200);
    { SDL_SetRenderDrawColor(ren, 0, 0, 0, 255); SDL_RenderClear(ren); SDL_RenderPresent(ren); }

    BMSData data1P, data2P;
    auto loadChart = [](const std::string& path) -> BMSData {
        if (BmsLoader::isBmsFile(path))
            return BmsLoader::load(path, [](float) { SDL_Event e; while (SDL_PollEvent(&e)); });
        else
            return BmsonLoader::load(path, [](float) { SDL_Event e; while (SDL_PollEvent(&e)); });
    };
    data1P = loadChart(path1P);
    data2P = loadChart(path2P);
    if (data1P.sound_channels.empty() || data2P.sound_channels.empty()) return true;

    vsHeaders[0] = data1P.header;
    vsHeaders[1] = data2P.header;

    players[0].reset();
    players[1].reset();
    players[0].greenNumber = Config::GREEN_NUMBER;
    players[0].highSpeed   = (double)Config::HS_BASE / (std::max(1, players[0].greenNumber) * data1P.header.bpm);
    players[0].suddenPlus  = Config::SUDDEN_PLUS;
    players[0].lift        = Config::LIFT;
    players[0].backupSudden = Config::SUDDEN_PLUS > 0 ? Config::SUDDEN_PLUS : 300;
    players[1].greenNumber = Config::GREEN_NUMBER;
    players[1].highSpeed   = (double)Config::HS_BASE / (std::max(1, players[1].greenNumber) * data2P.header.bpm);
    players[1].suddenPlus  = Config::SUDDEN_PLUS;
    players[1].lift        = Config::LIFT;
    players[1].backupSudden = Config::SUDDEN_PLUS > 0 ? Config::SUDDEN_PLUS : 300;
    Config::HIGH_SPEED = players[0].highSpeed;

    PlayEngine engine1P, engine2P;
    engine1P.init(data1P);
    engine2P.init(data2P);
    engine2P.skipBGM = true;

    renderer.rebuildBothLayouts();

    BgaManager bga;
    bga.init(data1P.bga_images.size());
    bga.setLayout(Config::SCREEN_WIDTH / 2);
    bga.setEvents(data1P.bga_events);
    bga.setLayerEvents(data1P.layer_events);
    bga.setPoorEvents(data1P.poor_events);

    isAssistUsed          = (Config::ASSIST_OPTION > 0);
    startButtonPressed    = false;
    start2PButtonPressed  = false;
    decideButtonPressed   = false;
    effectButtonPressed   = false;
    lastStartPressTime    = 0;
    last2PStartPressTime  = 0;

    std::string bmsonDir = "";
    { size_t ls = path1P.find_last_of("/\\");
      bmsonDir = (ls != std::string::npos) ? path1P.substr(0, ls + 1) : Config::ROOT_PATH; }
    bga.setBgaDirectory(bmsonDir);
    if (!data1P.header.bga_video.empty())
        bga.loadBgaFile(bmsonDir + data1P.header.bga_video, ren);
    for (auto const& [id, filename] : data1P.bga_images)
        bga.registerPath(id, filename);

    std::string bmsonBaseName = "";
    { size_t ls = path1P.find_last_of("/\\");
      size_t ld = path1P.find_last_of(".");
      bmsonBaseName = (ld != std::string::npos && ld > ls)
          ? path1P.substr(ls + 1, ld - ls - 1) : data1P.header.title; }

    auto showLoading = [&](const char*) {
        SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
        SDL_RenderClear(ren); SDL_RenderPresent(ren);
        SDL_Event e; while (SDL_PollEvent(&e));
    };
    showLoading("BOXWAV");
    snd.preloadBoxIndex(bmsonDir, bmsonBaseName);

    std::set<std::string> soundSet;
    std::vector<std::string> soundListBGM, soundListKeys;
    auto collectSounds = [&](const BMSData& data) {
        for (const auto& ch : data.sound_channels) {
            if (ch.name.empty() || soundSet.count(ch.name)) continue;
            soundSet.insert(ch.name);
            bool bgmOnly = !ch.notes.empty() &&
                std::all_of(ch.notes.begin(), ch.notes.end(),
                            [](const BMSNote& n) { return n.x < 1 || n.x > 8; });
            if (bgmOnly) soundListBGM.push_back(ch.name);
            else          soundListKeys.push_back(ch.name);
        }
    };
    collectSounds(data1P);
    collectSounds(data2P);
    std::vector<std::string> soundList;
    soundList.reserve(soundListBGM.size() + soundListKeys.size());
    soundList.insert(soundList.end(), soundListBGM.begin(), soundListBGM.end());
    soundList.insert(soundList.end(), soundListKeys.begin(), soundListKeys.end());

    showLoading("AUDIO");
    snd.loadSoundsInBulk(soundList, bmsonDir, bmsonBaseName, [&](int cnt, const std::string&) {
        if (cnt % 200 == 0) showLoading("AUDIO");
    });

    double videoOffsetMs = 0.0;
    if (data1P.header.bga_offset != 0) {
        double cb = data1P.header.bpm; int64_t cy = 0; double cm = 0.0;
        auto sb = data1P.bpm_events;
        std::sort(sb.begin(), sb.end(), [](const BPMEvent& a, const BPMEvent& b){ return a.y < b.y; });
        for (const auto& bE : sb) {
            if (bE.y >= data1P.header.bga_offset) break;
            cm += (double)(bE.y - cy) * 60000.0 / (cb * data1P.header.resolution);
            cy = bE.y; cb = bE.bpm;
        }
        if (cy < data1P.header.bga_offset)
            cm += (double)(data1P.header.bga_offset - cy) * 60000.0 / (cb * data1P.header.resolution);
        videoOffsetMs = cm;
    }

    double max_target_ms = 0;
    for (const auto& n : engine1P.getNotes())
        if (!n.isBGM) max_target_ms = std::max(max_target_ms, n.target_ms);
    for (const auto& n : engine2P.getNotes())
        if (!n.isBGM) max_target_ms = std::max(max_target_ms, n.target_ms);

    SDL_JoystickID joy1ID = -1, joy2ID = -1;
    if (SDL_NumJoysticks() > 0) { SDL_Joystick* j = SDL_JoystickOpen(0); if (j) joy1ID = SDL_JoystickInstanceID(j); }
    if (SDL_NumJoysticks() > 1) { SDL_Joystick* j = SDL_JoystickOpen(1); if (j) joy2ID = SDL_JoystickInstanceID(j); }

    // 待機ループ
    startTicks = SDL_GetTicks() + 99999999;
    bool waiting = true;
    while (waiting) {
        uint32_t now = SDL_GetTicks();
        if (!processInputVS(-2000.0, now, engine1P, engine2P, joy1ID, joy2ID)) return false;
        if (decideButtonPressed && !startButtonPressed) waiting = false;
        SDL_SetRenderDrawColor(ren, 10, 10, 15, 255); SDL_RenderClear(ren);
        renderer.renderBackground(ren);
        renderer.switchSide(1); renderer.renderLanes(ren, 0.0, 0);
        renderer.switchSide(2); renderer.renderLanes(ren, 0.0, 0);
        renderer.drawTextCached(ren, "PRESS DECIDE BUTTON TO START",
                                Config::SCREEN_WIDTH / 2, 450, {255, 255, 255, 255}, false, true);
        renderer.drawTextCached(ren, "VS MODE",
                                Config::SCREEN_WIDTH / 2, 350, {255, 165, 0, 255}, true, true);
        SDL_RenderPresent(ren);
#ifdef __SWITCH__
        if (!appletMainLoop()) return false;
#endif
    }
    startButtonPressed = false; decideButtonPressed = false;

    uint32_t start_ticks = SDL_GetTicks() + 2000;
    startTicks = start_ticks;
    uint32_t lastFpsTime = SDL_GetTicks();
    int frameCount = 0, fps = 0;
    bool playing = true, isAborted = false;

    while (playing) {
        uint32_t now = SDL_GetTicks();
        double cur_ms = (double)((int64_t)now - (int64_t)start_ticks);
        bga.syncTime(std::max(0.0, cur_ms - videoOffsetMs));

        if (!processInputVS(cur_ms, now, engine1P, engine2P, joy1ID, joy2ID)) {
            isAborted = true; playing = false; break;
        }

        updateAssistForPlayer(cur_ms, engine1P, players[0]);
        updateAssistForPlayer(cur_ms, engine2P, players[1]);

        // LN持続ボム
        for (int p = 0; p < 2; ++p) {
            PlayEngine& eng = (p == 0) ? engine1P : engine2P;
            PlayerState& ps = players[p];
            if (ps.isPlayerFailed) continue;
            const auto& ln = eng.getNotes();
            for (size_t i = ps.drawStartIndex; i < ln.size(); ++i) {
                const auto& n = ln[i];
                if (n.isBGM || !n.isLN || !n.isBeingPressed) continue;
                if (n.target_ms > cur_ms + 500.0) break;
                if (ps.lnHitJudge[n.lane] >= 2 && now - ps.lastLNBombTime[n.lane] >= 150) {
                    ps.lastLNBombTime[n.lane] = now;
                    int bt = (ps.lnHitJudge[n.lane] == 3) ? 1 : 2;
                    if (ps.bombCount < PlayerState::MAX_BOMBS)
                        ps.bombAnimsBuf[ps.bombCount++] = {n.lane, now, bt};
                }
            }
        }

        engine1P.update(cur_ms + 10.0, now);
        engine2P.update(cur_ms + 10.0, now);

        if (engine1P.getStatus().isFailed && !players[0].isPlayerFailed) players[0].isPlayerFailed = true;
        if (engine2P.getStatus().isFailed && !players[1].isPlayerFailed) players[1].isPlayerFailed = true;
        if (players[0].isPlayerFailed && players[1].isPlayerFailed) playing = false;

        double progress = (max_target_ms > 0) ? std::clamp(cur_ms / max_target_ms, 0.0, 1.0) : 0.0;
        int64_t cur_y_1P = engine1P.getYFromMs(cur_ms);
        int64_t cur_y_2P = engine2P.getYFromMs(cur_ms);

        SDL_SetRenderDrawColor(ren, 10, 10, 15, 255); SDL_RenderClear(ren);
        renderer.renderBackground(ren);

        renderPlayerField(ren, renderer, engine1P, players[0], 1,
                          cur_ms, cur_y_1P, fps, now, progress, vsHeaders[0]);
        renderPlayerField(ren, renderer, engine2P, players[1], 2,
                          cur_ms, cur_y_2P, fps, now, progress, vsHeaders[1]);

        renderer.drawTextCached(ren, "VS", Config::SCREEN_WIDTH / 2, 10,
                                {255, 165, 0, 255}, true, true);
        SDL_RenderPresent(ren);

        if (cur_ms > max_target_ms + 1500.0) playing = false;
        frameCount++;
        if (now - lastFpsTime >= 1000) { fps = frameCount; frameCount = 0; lastFpsTime = now; }
#ifdef __SWITCH__
        if (!appletMainLoop()) { isAborted = true; playing = false; break; }
#endif
    }

    vsStatuses[0] = engine1P.getStatus();
    vsStatuses[1] = engine2P.getStatus();
    LOG_INFO("ScenePlay", "=== VS Play loop end ===");

    snd.clear();
    bga.cleanup();
    // 【CRITICAL-3修正】非同期保存
    Config::markDirty();
    Config::saveAsync();
    if (isAborted) return false;
    return true;
}

const PlayStatus& ScenePlay::getStatus(int playerIdx) const {
    if (numPlayers == 2 && playerIdx >= 0 && playerIdx < 2)
        return vsStatuses[playerIdx];
    return status;
}

const BMSHeader& ScenePlay::getHeader(int playerIdx) const {
    if (numPlayers == 2 && playerIdx >= 0 && playerIdx < 2)
        return vsHeaders[playerIdx];
    return currentHeader;
}
