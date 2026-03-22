#include "SceneDanSelect.hpp"
#include "Config.hpp"
#include <string>

void SceneDanSelect::init(const DanData& data) {
    visible_.clear();
    for (const auto& c : data.courses) {
        if (c.visible) visible_.push_back(&c);
    }
    cursor_   = 0;
    selected_ = nullptr;
}

DanSelectStep SceneDanSelect::update(SDL_Renderer* ren, NoteRenderer& renderer) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT) return DanSelectStep::BACK;

        auto handleBtn = [&](int btn) -> DanSelectStep {
            if (btn == Config::SYS_BTN_DECIDE) {
                if (!visible_.empty()) {
                    selected_ = visible_[cursor_];
                    return DanSelectStep::GO_DAN;
                }
            }
            if (btn == Config::SYS_BTN_BACK) {
                return DanSelectStep::BACK;
            }
            if (btn == Config::SYS_BTN_DOWN) {
                if (!visible_.empty())
                    cursor_ = (cursor_ + 1) % (int)visible_.size();
            }
            if (btn == Config::SYS_BTN_UP) {
                if (!visible_.empty())
                    cursor_ = ((cursor_ - 1) + (int)visible_.size()) % (int)visible_.size();
            }
            return DanSelectStep::SELECTING;
        };

        if (e.type == SDL_JOYBUTTONDOWN) {
            auto r = handleBtn(e.jbutton.button);
            if (r != DanSelectStep::SELECTING) return r;
        }
        if (e.type == SDL_KEYDOWN && !e.key.repeat) {
            int btn = -1;
            switch (e.key.keysym.sym) {
                case SDLK_UP:     btn = Config::SYS_BTN_UP;     break;
                case SDLK_DOWN:   btn = Config::SYS_BTN_DOWN;   break;
                case SDLK_RETURN: btn = Config::SYS_BTN_DECIDE; break;
                case SDLK_ESCAPE: btn = Config::SYS_BTN_BACK;   break;
                default: break;
            }
            if (btn >= 0) {
                auto r = handleBtn(btn);
                if (r != DanSelectStep::SELECTING) return r;
            }
        }
    }

    // ---- 描画 ----
    SDL_SetRenderDrawColor(ren, 10, 10, 30, 255);
    SDL_RenderClear(ren);

    SDL_Color title  = {255, 215,   0, 255}; // gold
    SDL_Color white  = {255, 255, 255, 255};
    SDL_Color gray   = {120, 120, 120, 255};
    SDL_Color cyan   = {  0, 255, 255, 255};
    SDL_Color yellow = {255, 255,   0, 255};

    renderer.drawText(ren, "DAN CERTIFICATION", 640, 60, title, true, true);
    renderer.drawText(ren, "UP/DOWN: SELECT  DECIDE: START  BACK: RETURN", 640, 110, gray, true, true);

    if (visible_.empty()) {
        renderer.drawText(ren, "NO COURSES AVAILABLE", 640, 360, gray, true, true);
    } else {
        const int startY  = 200;
        const int lineH   = 60;
        const int maxShow = 8;
        int total = (int)visible_.size();

        // スクロール表示: cursor_ が常に表示範囲内に入るよう offset を計算
        int offset = 0;
        if (cursor_ >= maxShow) offset = cursor_ - maxShow + 1;

        for (int i = 0; i < maxShow && (offset + i) < total; i++) {
            int idx = offset + i;
            const DanCourse* c = visible_[idx];
            int y = startY + i * lineH;

            bool sel = (idx == cursor_);
            SDL_Color col = sel ? white : gray;
            renderer.drawText(ren, c->name.c_str(), 640, y, col, true, true);

            if (sel) {
                // 選択中カーソルライン
                SDL_Rect line = { 640 - 150, y + 28, 300, 3 };
                SDL_SetRenderDrawColor(ren, cyan.r, cyan.g, cyan.b, 255);
                SDL_RenderFillRect(ren, &line);

                // 曲数表示
                std::string info = std::to_string(c->songs.size()) + " SONGS";
                renderer.drawText(ren, info.c_str(), 900, y, yellow, true, true);
            }
        }
    }

    renderer.drawText(ren, "DECIDE TO START / BACK TO RETURN", 640, 660, cyan, true, true);
    SDL_RenderPresent(ren);
    return DanSelectStep::SELECTING;
}
