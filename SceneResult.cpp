#include "SceneResult.hpp"
#include "Config.hpp"
#include "ScoreManager.hpp"
#include <SDL2/SDL_image.h>

void SceneResult::run(SDL_Renderer* ren, NoteRenderer& renderer, const PlayStatus& status, const BMSHeader& header) {
    // EXオプション使用時はスコア・ランプ一切記録しない。
    // ただし MORE NOTES (EX_OPTION==3) はクリアランプのみ保存する。
    // saveIfBest 内で isMoreNotes フラグを見てスコア・コンボ更新をスキップする。
    BestScore best;
    if (Config::EX_OPTION == 0 || Config::EX_OPTION == 3) {
        ScoreManager::saveIfBest(header.title, header.chartName, (int)header.total, status);
        best = ScoreManager::loadScore(header.title, header.chartName, (int)header.total);
    }

    bool backToSelect = false;
    SDL_Event e;

    uint32_t startTime = SDL_GetTicks();
    const uint32_t MIN_DISPLAY_TIME = 500;

    SDL_PumpEvents();

    // ループ前にFAST/SLOWテキストを確定させる（ループ中は値が変わらないのでここで生成）
    char fastText[32], slowText[32];
    snprintf(fastText, sizeof(fastText), "FAST: %d", status.fastCount);
    snprintf(slowText, sizeof(slowText), "SLOW: %d", status.slowCount);

    // ランク計算もループ外で一度だけ
    std::string rank = calculateRank(status);

    while (!backToSelect) {
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) return;

            if (SDL_GetTicks() - startTime > MIN_DISPLAY_TIME) {
                if (e.type == SDL_JOYBUTTONDOWN) {
                    if (e.jbutton.button == Config::SYS_BTN_BACK) {
                        backToSelect = true;
                    }
                }
                else if (e.type == SDL_KEYDOWN) {
                    if (e.key.keysym.sym == SDLK_ESCAPE ||
                        e.key.keysym.sym == SDLK_BACKSPACE ||
                        e.key.keysym.sym == SDLK_RETURN) {
                        backToSelect = true;
                    }
                }
            }
        }

        SDL_SetRenderDrawColor(ren, 10, 10, 20, 255);
        SDL_RenderClear(ren);

        renderer.renderResult(ren, status, header, rank);

        // --- ゲージ推移グラフ ---
        int gx = 100, gy = 500, gw = 600, gh = 150;
        int opt = Config::GAUGE_OPTION;

        SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
        if (opt == 6) SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
        else SDL_SetRenderDrawColor(ren, 0, 0, 0, 180);

        SDL_Rect graphBg = {gx, gy, gw, gh};
        SDL_RenderFillRect(ren, &graphBg);
        SDL_SetRenderDrawColor(ren, 80, 80, 80, 255);
        SDL_RenderDrawRect(ren, &graphBg);

        float border = 80.0f;
        if (opt == 1) border = 60.0f;

        for (int i = 1; i <= 4; ++i) {
            int level = i * 20;
            int lineY = gy + gh - (level * gh / 100);
            if (opt < 3 && (int)level == (int)border) {
                SDL_SetRenderDrawColor(ren, 200, 50, 50, 255);
            } else {
                SDL_SetRenderDrawColor(ren, 60, 60, 60, 255);
            }
            SDL_RenderDrawLine(ren, gx, lineY, gx + gw, lineY);
        }

        if (!status.gaugeHistory.empty()) {
            int historySize = (int)status.gaugeHistory.size();
            float stepX = (float)gw / (historySize > 1 ? (historySize - 1) : 1);

            for (int i = 0; i < historySize - 1; ++i) {
                int x1 = gx + (int)(i * stepX);
                int y1 = gy + gh - (int)(status.gaugeHistory[i] * gh / 100.0f);
                int x2 = gx + (int)((i + 1) * stepX);
                int y2 = gy + gh - (int)(status.gaugeHistory[i+1] * gh / 100.0f);

                SDL_Color lineCol, fillCol;
                if (opt == 4) {
                    lineCol = {255, 255, 0, 255}; fillCol = {150, 150, 0, 100};
                } else if (opt >= 3) {
                    lineCol = {255, 60, 60, 255};  fillCol = {150, 30, 30, 100};
                } else {
                    if (status.gaugeHistory[i] >= border) {
                        lineCol = {255, 60, 60, 255};  fillCol = {150, 30, 30, 100};
                    } else {
                        lineCol = {60, 150, 255, 255}; fillCol = {30, 80, 150, 100};
                    }
                }

                SDL_SetRenderDrawColor(ren, lineCol.r, lineCol.g, lineCol.b, lineCol.a);
                SDL_RenderDrawLine(ren, x1, y1, x2, y2);
                SDL_SetRenderDrawColor(ren, fillCol.r, fillCol.g, fillCol.b, fillCol.a);
                SDL_RenderDrawLine(ren, x1, y1 + 1, x1, gy + gh - 1);
            }
        }

        // FAST/SLOW：値が変わらないのでdrawTextCachedで毎フレームのテクスチャ生成を避ける
        renderer.drawTextCached(ren, fastText, 850, 580, {0, 255, 255, 255}, false, false);
        renderer.drawTextCached(ren, slowText, 850, 610, {255, 0, 255, 255}, false, false);

        // SDL_RENDERER_PRESENTVSYNCが有効なためSDL_RenderPresentが既に16ms待機する
        // SDL_Delay(16)を重ねると実質30fpsになるため削除
        SDL_RenderPresent(ren);
    }

    // IMG_Quit() はここで呼ばない。
    // SDL_image の初期化/終了は NoteRenderer::init()/cleanup() で一度だけ行う。
    // ここで呼ぶと 2 曲目以降の BGA 画像ロードで NULL テクスチャになりクラッシュする。
    // ★修正(MINOR-3): 200ms→100ms に短縮。ボタン連打防止の猶予として100msで十分。
    SDL_Delay(100);
    SDL_FlushEvents(SDL_JOYBUTTONDOWN, SDL_JOYBUTTONUP);
}

std::string SceneResult::calculateRank(const PlayStatus& status) {
    if (status.totalNotes <= 0) return "F";

    double maxExScore = status.totalNotes * 2.0;
    double currentExScore = (status.pGreatCount * 2.0) + (status.greatCount * 1.0);
    double ratio = currentExScore / maxExScore;

    if (ratio >= 8.0/9.0) return "AAA";
    if (ratio >= 7.0/9.0) return "AA";
    if (ratio >= 6.0/9.0) return "A";
    if (ratio >= 5.0/9.0) return "B";
    if (ratio >= 4.0/9.0) return "C";
    if (ratio >= 3.0/9.0) return "D";
    if (ratio >= 2.0/9.0) return "E";
    return "F";
}

// ============================================================
// ============================================================
//  段位認定: 曲ごとのリザルト
// ============================================================
DanResultAction SceneResult::runDan(SDL_Renderer* ren, NoteRenderer& renderer,
                                    const PlayStatus& status, const BMSHeader& header,
                                    int songIndex, int totalSongs,
                                    double gauge, const std::string& courseName, bool failed) {
    std::string rank = calculateRank(status);
    DanResultAction action = DanResultAction::CONTINUE;

    uint32_t startTime = SDL_GetTicks();
    const uint32_t MIN_DISPLAY = 500;
    SDL_PumpEvents();

    SDL_Color white  = {255, 255, 255, 255};
    SDL_Color yellow = {255, 255,   0, 255};
    SDL_Color cyan   = {  0, 255, 255, 255};
    SDL_Color gray   = {150, 150, 150, 255};
    SDL_Color red    = {255,  60,  60, 255};
    SDL_Color green  = {  0, 220, 100, 255};
    SDL_Color orange = {255, 165,   0, 255};

    bool running = true;
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) { action = DanResultAction::QUIT; running = false; break; }
            if (SDL_GetTicks() - startTime > MIN_DISPLAY) {
                if (e.type == SDL_JOYBUTTONDOWN) {
                    if (e.jbutton.button == Config::SYS_BTN_BACK) {
                        action = DanResultAction::QUIT; running = false;
                    } else if (e.jbutton.button == Config::SYS_BTN_DECIDE) {
                        action = DanResultAction::CONTINUE; running = false;
                    }
                }
                if (e.type == SDL_KEYDOWN) {
                    SDL_Keycode k = e.key.keysym.sym;
                    if (k == SDLK_ESCAPE) { action = DanResultAction::QUIT; running = false; }
                    else if (k == SDLK_RETURN || k == SDLK_SPACE) { action = DanResultAction::CONTINUE; running = false; }
                }
            }
        }

        SDL_SetRenderDrawColor(ren, 8, 8, 20, 255);
        SDL_RenderClear(ren);

        // ── ヘッダー ──────────────────────────────────────────────────
        char songNo[32];
        snprintf(songNo, sizeof(songNo), "SONG %d / %d", songIndex + 1, totalSongs);
        renderer.drawText(ren, courseName,          640, 30,  orange, true,  true);
        renderer.drawText(ren, songNo,              640, 65,  cyan,   true,  true);
        renderer.drawText(ren, header.title,        640, 95,  white,  false, true);

        // ── スコア ────────────────────────────────────────────────────
        char buf[TEXT_BUF_SIZE];
        int  exScore = status.pGreatCount * 2 + status.greatCount;
        int  cx = 400, y = 140, lh = 32;

        snprintf(buf, sizeof(buf), "RANK: %s", rank.c_str());
        renderer.drawText(ren, buf, cx, y, yellow, true, false); y += lh + 4;

        snprintf(buf, sizeof(buf), "EX SCORE: %d", exScore);
        renderer.drawText(ren, buf, cx, y, white, false, false); y += lh;
        snprintf(buf, sizeof(buf), "P-GREAT:  %d", status.pGreatCount);
        renderer.drawText(ren, buf, cx, y, white, false, false); y += lh;
        snprintf(buf, sizeof(buf), "GREAT:    %d", status.greatCount);
        renderer.drawText(ren, buf, cx, y, white, false, false); y += lh;
        snprintf(buf, sizeof(buf), "GOOD:     %d", status.goodCount);
        renderer.drawText(ren, buf, cx, y, white, false, false); y += lh;
        snprintf(buf, sizeof(buf), "BAD:      %d", status.badCount);
        renderer.drawText(ren, buf, cx, y, white, false, false); y += lh;
        snprintf(buf, sizeof(buf), "POOR:     %d", status.poorCount);
        renderer.drawText(ren, buf, cx, y, white, false, false); y += lh;
        snprintf(buf, sizeof(buf), "MAX COMBO: %d", status.maxCombo);
        renderer.drawText(ren, buf, cx, y, white, false, false); y += lh;
        snprintf(buf, sizeof(buf), "FAST: %d  SLOW: %d", status.fastCount, status.slowCount);
        renderer.drawText(ren, buf, cx, y, gray,  false, false);

        // ── 残ゲージバー ──────────────────────────────────────────────
        int gx = 100, gy = 520, gw = 1080, gh = 40;
        SDL_Rect gBg = {gx, gy, gw, gh};
        SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);
        SDL_SetRenderDrawColor(ren, 30, 30, 30, 255);
        SDL_RenderFillRect(ren, &gBg);
        SDL_SetRenderDrawColor(ren, 80, 80, 80, 255);
        SDL_RenderDrawRect(ren, &gBg);

        if (gauge > 0.0) {
            int fillW = (int)(gw * gauge / 100.0);
            SDL_Rect gFill = {gx, gy, fillW, gh};
            SDL_Color gc = (gauge >= 50.0) ? green : (gauge >= 20.0) ? yellow : red;
            SDL_SetRenderDrawColor(ren, gc.r, gc.g, gc.b, 255);
            SDL_RenderFillRect(ren, &gFill);
        }
        snprintf(buf, sizeof(buf), "GAUGE: %.1f%%", gauge);
        renderer.drawText(ren, buf, gx + gw / 2, gy - 22, white, true, false);

        // ── 失格 / 続行表示 ──────────────────────────────────────────
        if (failed) {
            renderer.drawText(ren, "COURSE FAILED", 640, 590, red,  true, true);
        } else if (songIndex + 1 < totalSongs) {
            renderer.drawText(ren, "DECIDE: NEXT SONG  /  BACK: QUIT", 640, 590, gray, true, true);
        } else {
            renderer.drawText(ren, "DECIDE: FINISH  /  BACK: QUIT",    640, 590, gray, true, true);
        }

        SDL_RenderPresent(ren);
    }

    SDL_Delay(100);
    SDL_FlushEvents(SDL_JOYBUTTONDOWN, SDL_JOYBUTTONUP);
    return action;
}

// ============================================================
//  ★2P VS リザルト
// ============================================================
void SceneResult::runVS(SDL_Renderer* ren, NoteRenderer& renderer,
                         const PlayStatus& status1P, const BMSHeader& header1P,
                         const PlayStatus& status2P, const BMSHeader& header2P) {
    // 2P VS時はスコア保存しない
    std::string rank1P = calculateRank(status1P);
    std::string rank2P = calculateRank(status2P);

    bool backToSelect = false;
    uint32_t startTime = SDL_GetTicks();
    const uint32_t MIN_DISPLAY_TIME = 500;
    SDL_PumpEvents();

    int ex1 = status1P.pGreatCount * 2 + status1P.greatCount;
    int ex2 = status2P.pGreatCount * 2 + status2P.greatCount;
    const char* winnerText = (ex1 > ex2) ? "1P WIN!" : (ex2 > ex1) ? "2P WIN!" : "DRAW!";

    while (!backToSelect) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) return;
            if (SDL_GetTicks() - startTime > MIN_DISPLAY_TIME) {
                if (e.type == SDL_JOYBUTTONDOWN) {
                    if (e.jbutton.button == Config::SYS_BTN_BACK ||
                        e.jbutton.button == Config::SYS_BTN_DECIDE) backToSelect = true;
                }
                if (e.type == SDL_KEYDOWN) {
                    if (e.key.keysym.sym == SDLK_ESCAPE ||
                        e.key.keysym.sym == SDLK_RETURN) backToSelect = true;
                }
            }
        }

        SDL_SetRenderDrawColor(ren, 10, 10, 20, 255);
        SDL_RenderClear(ren);

        SDL_Color white  = {255, 255, 255, 255};
        SDL_Color yellow = {255, 255, 0, 255};
        SDL_Color cyan   = {0, 255, 255, 255};
        SDL_Color orange = {255, 165, 0, 255};
        SDL_Color gray   = {150, 150, 150, 255};
        SDL_Color green  = {0, 255, 128, 255};

        renderer.drawTextCached(ren, "VS RESULT", 640, 30, orange, true, true);
        renderer.drawTextCached(ren, header1P.title, 640, 70, white, false, true);

        SDL_Color winColor = (ex1 > ex2) ? cyan : (ex2 > ex1) ? green : yellow;
        renderer.drawTextCached(ren, winnerText, 640, 110, winColor, true, true);

        int lx = 320, rx = 960;
        int yStart = 170;
        int lineH = 35;

        auto drawPlayerResult = [&](int cx, const PlayStatus& st, const std::string& rank,
                                    const BMSHeader& hdr, const char* label) {
            int y = yStart;
            renderer.drawTextCached(ren, label, cx, y, cyan, true, true); y += lineH + 10;

            char buf[TEXT_BUF_SIZE];
            snprintf(buf, sizeof(buf), "[%s] LV.%d", hdr.chartName.c_str(), hdr.level);
            renderer.drawTextCached(ren, buf, cx, y, gray, false, true); y += lineH;

            renderer.drawTextCached(ren, std::string("RANK: ") + rank, cx, y, yellow, true, true); y += lineH + 5;

            int exScore = st.pGreatCount * 2 + st.greatCount;
            snprintf(buf, sizeof(buf), "EX SCORE: %d", exScore);
            renderer.drawTextCached(ren, buf, cx, y, white, false, true); y += lineH;
            snprintf(buf, sizeof(buf), "P-GREAT: %d", st.pGreatCount);
            renderer.drawTextCached(ren, buf, cx, y, white, false, true); y += lineH;
            snprintf(buf, sizeof(buf), "GREAT:   %d", st.greatCount);
            renderer.drawTextCached(ren, buf, cx, y, white, false, true); y += lineH;
            snprintf(buf, sizeof(buf), "GOOD:    %d", st.goodCount);
            renderer.drawTextCached(ren, buf, cx, y, white, false, true); y += lineH;
            snprintf(buf, sizeof(buf), "BAD:     %d", st.badCount);
            renderer.drawTextCached(ren, buf, cx, y, white, false, true); y += lineH;
            snprintf(buf, sizeof(buf), "POOR:    %d", st.poorCount);
            renderer.drawTextCached(ren, buf, cx, y, white, false, true); y += lineH;
            snprintf(buf, sizeof(buf), "MAX COMBO: %d", st.maxCombo);
            renderer.drawTextCached(ren, buf, cx, y, white, false, true); y += lineH;
            snprintf(buf, sizeof(buf), "FAST: %d  SLOW: %d", st.fastCount, st.slowCount);
            renderer.drawTextCached(ren, buf, cx, y, gray, false, true);
        };

        drawPlayerResult(lx, status1P, rank1P, header1P, "1P");
        drawPlayerResult(rx, status2P, rank2P, header2P, "2P");

        SDL_SetRenderDrawColor(ren, 80, 80, 80, 255);
        SDL_RenderDrawLine(ren, 640, yStart, 640, 680);

        SDL_RenderPresent(ren);
    }

    // ★修正(MINOR-3): 200ms→100ms に短縮。ボタン連打防止の猶予として100msで十分。
    SDL_Delay(100);
    SDL_FlushEvents(SDL_JOYBUTTONDOWN, SDL_JOYBUTTONUP);
}




