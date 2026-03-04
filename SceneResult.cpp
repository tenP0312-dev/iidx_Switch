#include "SceneResult.hpp"
#include "Config.hpp"
#include "ScoreManager.hpp"
#include <SDL2/SDL_image.h>

void SceneResult::run(SDL_Renderer* ren, NoteRenderer& renderer, const PlayStatus& status, const BMSHeader& header) {
    // EXオプション使用時はスコア・ランプ一切記録しない
    BestScore best;
    if (Config::EX_OPTION == 0) {
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

    IMG_Quit();
    SDL_Delay(200);
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

