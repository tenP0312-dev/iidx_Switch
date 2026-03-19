#include "Scene2PDiffSelect.hpp"
#include "Config.hpp"
#include "BmsLoader.hpp"
#include "BmsonLoader.hpp"
#include <algorithm>
#include <cstdio>
#include <cmath>

#ifdef __SWITCH__
#include <switch.h>
#endif

// ============================================================
//  Scene2PDiffSelect::run
//  引数:
//    songCache  : SceneSelect の全曲キャッシュ
//    group      : 1Pが選んだ SongGroup（難易度リスト）
//    p1DiffIdx  : 1Pの難易度インデックス（group.songIndices 内）
//  戻り値:
//    2Pが選んだ譜面のフルパス。キャンセル時は ""。
// ============================================================
std::string Scene2PDiffSelect::run(SDL_Renderer* ren, NoteRenderer& renderer,
                                    const std::vector<SongEntry>& songCache,
                                    const SongGroup& group,
                                    int p1DiffIdx) {

    // ── 難易度リスト構築 ──────────────────────────────────────
    // group.songIndices が空 or isFolder のグループはそのまま返す
    if (group.isFolder || group.songIndices.empty()) return "";

    // 難易度エントリをインデックス順に収集
    struct DiffEntry {
        int   cacheIdx;
        std::string path;
        std::string chartName;
        int   level;
        std::string title;
        std::string artist;
    };
    std::vector<DiffEntry> diffs;
    diffs.reserve(group.songIndices.size());
    for (int idx : group.songIndices) {
        const SongEntry& e = songCache[idx];
        diffs.push_back({ idx, e.filename, e.chartName, e.level, e.title, e.artist });
    }

    // p1DiffIdx をクランプ
    int p1Idx = std::clamp(p1DiffIdx, 0, (int)diffs.size() - 1);

    // 2Pカーソルの初期位置: 1Pと同じ難易度
    int p2Idx = p1Idx;

    // ── カウントダウン ────────────────────────────────────────
    const uint32_t COUNTDOWN_MS = 5000; // 5秒でタイムアウト確定
    uint32_t startTime = SDL_GetTicks();

    // ── 色定数 ───────────────────────────────────────────────
    SDL_Color white  = {255, 255, 255, 255};
    SDL_Color yellow = {255, 255, 0,   255};
    SDL_Color cyan   = {0,   255, 255, 255};
    SDL_Color orange = {255, 165, 0,   255};
    SDL_Color gray   = {150, 150, 150, 255};
    SDL_Color green  = {0,   255, 128, 255};
    SDL_Color red    = {255,  50,  50, 255};

    const int CX_1P = 320;
    const int CX_2P = 960;

    while (true) {
        uint32_t now = SDL_GetTicks();
        uint32_t elapsed = now - startTime;
        bool timedOut = (elapsed >= COUNTDOWN_MS);

#ifdef __SWITCH__
        if (!appletMainLoop()) return "";
#endif

        // ── イベント処理 ─────────────────────────────────────
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) return "";

            bool needsTimerReset = false;

            if (ev.type == SDL_JOYBUTTONDOWN) {
                int btn = ev.jbutton.button;

                // キャンセル
                if (btn == Config::SYS_BTN_BACK) return "";

                // 確定
                if (btn == Config::SYS_BTN_DECIDE) {
                    if (!diffs.empty()) return diffs[p2Idx].path;
                    return "";
                }

                // 2P難易度変更: SYS_BTN_LEFT / SYS_BTN_DIFF / スクラッチ etc.
                if (btn == Config::SYS_BTN_LEFT) {
                    p2Idx = (p2Idx - 1 + (int)diffs.size()) % (int)diffs.size();
                    needsTimerReset = true;
                }
                if (btn == Config::SYS_BTN_RIGHT ||
                    btn == Config::SYS_BTN_UP    ||
                    btn == Config::SYS_BTN_DOWN  ||
                    btn == Config::SYS_BTN_DIFF) {  // ★ SYS_BTN_DIFF でも変更可能
                    p2Idx = (p2Idx + 1) % (int)diffs.size();
                    needsTimerReset = true;
                }

                // 2P コントローラのスクラッチでも変更可能（BTN_2P_LANE8_A=前、BTN_2P_LANE8_B=次）
                // 1P スクラッチも引き続き動作する
                if (btn == Config::BTN_LANE8_A || btn == Config::BTN_2P_LANE8_A) {
                    p2Idx = (p2Idx - 1 + (int)diffs.size()) % (int)diffs.size();
                    needsTimerReset = true;
                }
                if (btn == Config::BTN_LANE8_B || btn == Config::BTN_2P_LANE8_B) {
                    p2Idx = (p2Idx + 1) % (int)diffs.size();
                    needsTimerReset = true;
                }
            }

            if (ev.type == SDL_KEYDOWN) {
                SDL_Keycode k = ev.key.keysym.sym;
                if (k == SDLK_ESCAPE)                  return "";
                if (k == SDLK_RETURN || k == SDLK_KP_ENTER) {
                    if (!diffs.empty()) return diffs[p2Idx].path;
                    return "";
                }
                if (k == SDLK_LEFT) {
                    p2Idx = (p2Idx - 1 + (int)diffs.size()) % (int)diffs.size();
                    needsTimerReset = true;
                }
                if (k == SDLK_RIGHT) {
                    p2Idx = (p2Idx + 1) % (int)diffs.size();
                    needsTimerReset = true;
                }
            }

            if (needsTimerReset) {
                startTime = SDL_GetTicks();
                elapsed   = 0;
            }
        }

        // タイムアウト確定
        if (timedOut) {
            if (!diffs.empty()) return diffs[p2Idx].path;
            return "";
        }

        // ── 描画 ─────────────────────────────────────────────
        SDL_SetRenderDrawColor(ren, 10, 10, 25, 255);
        SDL_RenderClear(ren);

        // タイトル
        renderer.drawTextCached(ren, "2P  SELECT DIFFICULTY", 640, 30, orange, true, true);
        renderer.drawTextCached(ren, "LEFT/RIGHT TO SELECT  /  DECIDE TO START  /  BACK TO CANCEL",
                                640, 65, gray, false, true);

        // 縦区切り線
        SDL_SetRenderDrawColor(ren, 60, 60, 60, 255);
        SDL_RenderDrawLine(ren, 640, 90, 640, 600);

        // ── 1P側（左、固定） ─────────────────────────────────
        renderer.drawTextCached(ren, "1P", CX_1P, 100, cyan, true, true);
        {
            const DiffEntry& d1 = diffs[p1Idx];
            renderer.drawTextCached(ren, d1.title,  CX_1P, 170, white, false, true);
            renderer.drawTextCached(ren, d1.artist, CX_1P, 215, gray,  false, true);
            char buf[TEXT_BUF_SIZE];
            snprintf(buf, sizeof(buf), "[%s]  LEVEL %d", d1.chartName.c_str(), d1.level);
            renderer.drawTextCached(ren, buf, CX_1P, 255, yellow, false, true);
        }

        // 難易度一覧（1P side）
        {
            int listY = 310;
            int lineH = 30;
            for (int i = 0; i < (int)diffs.size(); ++i) {
                const DiffEntry& d = diffs[i];
                char buf[TEXT_BUF_SIZE];
                snprintf(buf, sizeof(buf), "[%s] LV.%d", d.chartName.c_str(), d.level);
                SDL_Color col = (i == p1Idx) ? cyan : gray;
                renderer.drawTextCached(ren, buf, CX_1P, listY + i * lineH, col, false, true);
            }
        }

        // ── 2P側（右、カーソルあり） ──────────────────────────
        renderer.drawTextCached(ren, "2P", CX_2P, 100, green, true, true);
        {
            const DiffEntry& d2 = diffs[p2Idx];
            renderer.drawTextCached(ren, d2.title,  CX_2P, 170, white, false, true);
            renderer.drawTextCached(ren, d2.artist, CX_2P, 215, gray,  false, true);
            char buf[TEXT_BUF_SIZE];
            snprintf(buf, sizeof(buf), "[%s]  LEVEL %d", d2.chartName.c_str(), d2.level);
            renderer.drawTextCached(ren, buf, CX_2P, 255, yellow, false, true);
        }

        // 難易度一覧（2P side: カーソル強調）
        {
            int listY = 310;
            int lineH = 30;
            for (int i = 0; i < (int)diffs.size(); ++i) {
                const DiffEntry& d = diffs[i];
                char buf[TEXT_BUF_SIZE];
                snprintf(buf, sizeof(buf), "[%s] LV.%d", d.chartName.c_str(), d.level);
                SDL_Color col = (i == p2Idx) ? green : gray;
                renderer.drawTextCached(ren, buf, CX_2P, listY + i * lineH, col, false, true);
                // カーソルアンダーライン
                if (i == p2Idx) {
                    int tw = 160; // 近似幅
                    SDL_Rect ul = { CX_2P - tw / 2, listY + i * lineH + 24, tw, 3 };
                    SDL_SetRenderDrawColor(ren, green.r, green.g, green.b, 255);
                    SDL_RenderFillRect(ren, &ul);
                }
            }
        }

        // ナビゲーション矢印（難易度が複数ある場合）
        if ((int)diffs.size() > 1) {
            renderer.drawTextCached(ren, "<", CX_2P - 120, 255, white, true, true);
            renderer.drawTextCached(ren, ">", CX_2P + 120, 255, white, true, true);
        }

        // ── カウントダウンバー ───────────────────────────────
        float countdown = 1.0f - std::min(1.0f, (float)elapsed / (float)COUNTDOWN_MS);
        int barW = (int)(countdown * Config::SCREEN_WIDTH);
        // バー色: 残り30%を切ったら赤に
        SDL_Color barColor = (countdown > 0.3f) ? cyan : red;
        SDL_Rect bar = { 0, Config::SCREEN_HEIGHT - 8, barW, 8 };
        SDL_SetRenderDrawColor(ren, barColor.r, barColor.g, barColor.b, 255);
        SDL_RenderFillRect(ren, &bar);

        // 残り秒数テキスト
        {
            char tbuf[16];
            float secLeft = (float)(COUNTDOWN_MS - elapsed) / 1000.0f;
            snprintf(tbuf, sizeof(tbuf), "%.1f", secLeft > 0.0f ? secLeft : 0.0f);
            renderer.drawTextCached(ren, tbuf, Config::SCREEN_WIDTH - 60,
                                    Config::SCREEN_HEIGHT - 36,
                                    (countdown > 0.3f) ? white : red, false, true);
        }

        SDL_RenderPresent(ren);

        // CPU節約: VSyncが無い環境向け
        SDL_Delay(1);
    }

    // ここには到達しないが念のため
    return diffs.empty() ? "" : diffs[p2Idx].path;
}



