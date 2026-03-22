#include "SceneDan.hpp"
#include "ScenePlay.hpp"
#include "SceneResult.hpp"
#include "Config.hpp"
#include "Logger.hpp"
#include <SDL2/SDL.h>
#include <string>
#include <algorithm>
#include <cmath>

// ============================================================
//  合否画面
// ============================================================
static void showFinalResult(SDL_Renderer* ren, NoteRenderer& renderer,
                            const DanCourse& course, bool passed, double finalGauge) {
    bool done = false;
    while (!done) {
#ifdef __SWITCH__
        if (!appletMainLoop()) break;
#endif
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) { done = true; break; }
            auto check = [&](int btn) {
                if (btn == Config::SYS_BTN_DECIDE || btn == Config::SYS_BTN_BACK)
                    done = true;
            };
            if (e.type == SDL_JOYBUTTONDOWN) check(e.jbutton.button);
            if (e.type == SDL_KEYDOWN && !e.key.repeat) {
                switch (e.key.keysym.sym) {
                    case SDLK_RETURN: case SDLK_ESCAPE: done = true; break;
                    default: break;
                }
            }
        }

        SDL_SetRenderDrawColor(ren, 5, 5, 20, 255);
        SDL_RenderClear(ren);

        SDL_Color gold   = {255, 215,   0, 255};
        SDL_Color white  = {255, 255, 255, 255};
        SDL_Color green  = {  0, 220,   0, 255};
        SDL_Color red    = {220,  50,  50, 255};
        SDL_Color cyan   = {  0, 255, 255, 255};

        renderer.drawText(ren, course.name.c_str(), 640, 100, gold, true, true);

        if (passed) {
            renderer.drawText(ren, "COURSE CLEAR!", 640, 260, green, true, true);
        } else {
            renderer.drawText(ren, "COURSE FAILED", 640, 260, red, true, true);
        }

        // 最終ゲージバー
        int barW = 400, barH = 30;
        int barX = 640 - barW / 2, barY = 360;
        SDL_Rect bg  = { barX, barY, barW, barH };
        SDL_SetRenderDrawColor(ren, 40, 40, 40, 255);
        SDL_RenderFillRect(ren, &bg);

        double clampedGauge = std::max(0.0, std::min(100.0, finalGauge));
        int fillW = (int)std::round(barW * clampedGauge / 100.0);
        if (fillW > 0) {
            SDL_Rect fill = { barX, barY, fillW, barH };
            SDL_Color gc = (clampedGauge >= 50.0) ? green
                         : (clampedGauge >= 20.0) ? SDL_Color{255,200,0,255}
                                                   : red;
            SDL_SetRenderDrawColor(ren, gc.r, gc.g, gc.b, 255);
            SDL_RenderFillRect(ren, &fill);
        }
        // ゲージ枠
        SDL_SetRenderDrawColor(ren, 200, 200, 200, 255);
        SDL_RenderDrawRect(ren, &bg);

        char gaugeBuf[32];
        std::snprintf(gaugeBuf, sizeof(gaugeBuf), "GAUGE: %.1f%%", clampedGauge);
        renderer.drawText(ren, gaugeBuf, 640, 410, white, true, true);

        renderer.drawText(ren, "DECIDE / BACK: RETURN", 640, 640, cyan, true, true);
        SDL_RenderPresent(ren);
    }
}

// ============================================================
//  SceneDan::run
// ============================================================
void SceneDan::run(SDL_Renderer* ren, NoteRenderer& renderer, const DanCourse& course) {
    if (course.songs.empty()) return;

    const int totalSongs = (int)course.songs.size();
    const int gaugeOption = danGaugeToOption(course.gaugeType);
    double gauge = course.startGauge;
    bool courseFailed = false;

    LOG_INFO("SceneDan", "course='%s' songs=%d gaugeOption=%d startGauge=%.1f",
             course.name.c_str(), totalSongs, gaugeOption, gauge);

    ScenePlay  scenePlay;
    SceneResult sceneResult;

    for (int i = 0; i < totalSongs; i++) {
        const std::string& path = course.songs[i].path;
        std::string fullPath = Config::ROOT_PATH + path;

        LOG_INFO("SceneDan", "song %d/%d path='%s' gauge=%.1f",
                 i + 1, totalSongs, fullPath.c_str(), gauge);

        // ---- プレイ ----
        DanPlayContext ctx;
        ctx.initialGauge = gauge;
        ctx.gaugeOption  = gaugeOption;
        ctx.songIndex    = i;
        ctx.totalSongs   = totalSongs;

        bool playFinished = scenePlay.runDan(ren, renderer, fullPath, ctx);
        gauge = scenePlay.getFinalGauge();

        LOG_INFO("SceneDan", "song %d finished normal=%d finalGauge=%.1f",
                 i + 1, playFinished ? 1 : 0, gauge);

        // プレイ中断 → 失格として合否画面へ
        if (!playFinished) {
            courseFailed = true;
            // 曲ごとリザルトは出さずに合否へ
            break;
        }

        // ---- 曲ごとリザルト ----
        // PlayEngine が gauge 種別に応じて isFailed をセットする:
        //   NORMAL/EASY : 曲終了時にゲージが規定値(80%/60%)未満なら true
        //   HARD/EX_HARD/DAN : プレイ中にゲージが 0% になったら true
        bool thisSongFailed = scenePlay.getStatus().isFailed;

        DanResultAction action = sceneResult.runDan(
            ren, renderer,
            scenePlay.getStatus(), scenePlay.getHeader(),
            i, totalSongs,
            gauge, course.name, thisSongFailed
        );

        if (action == DanResultAction::QUIT) {
            // BACKで離脱 → 失格
            courseFailed = true;
            break;
        }

        if (thisSongFailed) {
            // HARD/DAN: ゲージ0到達で失格。残り曲は飛ばす
            courseFailed = true;
            break;
        }
    }

    // ---- 合否画面 ----
    bool passed = !courseFailed;
    showFinalResult(ren, renderer, course, passed, gauge);
    LOG_INFO("SceneDan", "course '%s' %s finalGauge=%.1f",
             course.name.c_str(), passed ? "PASSED" : "FAILED", gauge);
}
