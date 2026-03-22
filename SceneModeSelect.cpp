#include "SceneModeSelect.hpp"
#include "Config.hpp"

void SceneModeSelect::init() {
    selectedMode = 0; // 0: FREE PLAY, 1: STANDARD, 2: DAN, 3: OPTION
}

ModeSelectStep SceneModeSelect::update(SDL_Renderer* ren, NoteRenderer& renderer) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT) return ModeSelectStep::SELECTING;

        if (e.type == SDL_JOYBUTTONDOWN) {
            int btn = e.jbutton.button;
            if (btn == Config::SYS_BTN_DECIDE) {
                if (selectedMode == 0) return ModeSelectStep::GO_SELECT; // FREE
                if (selectedMode == 1) return ModeSelectStep::GO_SELECT; // STANDARD
                if (selectedMode == 2) return ModeSelectStep::GO_DAN;    // DAN
                if (selectedMode == 3) return ModeSelectStep::GO_OPTION; // OPTION
            }
            if (btn == Config::SYS_BTN_DOWN) {
                selectedMode = (selectedMode + 1) % 4;
            }
            if (btn == Config::SYS_BTN_UP) {
                selectedMode = (selectedMode + 3) % 4;
            }
        }

        if (e.type == SDL_KEYDOWN) {
            SDL_Keycode key = e.key.keysym.sym;
            if (key == SDLK_z || key == SDLK_RETURN) {
                if (selectedMode == 0) return ModeSelectStep::GO_SELECT;
                if (selectedMode == 1) return ModeSelectStep::GO_SELECT;
                if (selectedMode == 2) return ModeSelectStep::GO_DAN;
                if (selectedMode == 3) return ModeSelectStep::GO_OPTION;
            }
            if (key == SDLK_DOWN || key == SDLK_SPACE) {
                selectedMode = (selectedMode + 1) % 4;
            }
            if (key == SDLK_UP) {
                selectedMode = (selectedMode + 3) % 4;
            }
        }
    }

    SDL_SetRenderDrawColor(ren, 10, 10, 30, 255);
    SDL_RenderClear(ren);

    SDL_Color white = {255, 255, 255, 255}, yellow = {255, 255, 0, 255}, gray = {120, 120, 120, 255}, cyan = {0, 255, 255, 255};

    renderer.drawText(ren, "SELECT MODE", 640, 120, yellow, true, true);
    renderer.drawText(ren, "UP/DOWN TO MOVE / DECIDE TO SELECT", 640, 180, gray, true, true);
    
    // 4項目を等間隔で表示
    renderer.drawText(ren, "FREE PLAY", 640, 240, (selectedMode == 0 ? white : gray), true, true);
    renderer.drawText(ren, "STANDARD",  640, 310, (selectedMode == 1 ? white : gray), true, true);
    renderer.drawText(ren, "DAN",       640, 380, (selectedMode == 2 ? white : gray), true, true);
    renderer.drawText(ren, "OPTION",    640, 450, (selectedMode == 3 ? white : gray), true, true);

    // カーソル
    int lineY = 240 + (selectedMode * 70);
    SDL_Rect cursor = { 640 - 100, lineY + 28, 200, 5 };
    SDL_SetRenderDrawColor(ren, cyan.r, cyan.g, cyan.b, 255);
    SDL_RenderFillRect(ren, &cursor);

    renderer.drawText(ren, "PRESS DECIDE BUTTON TO START", 640, 550, cyan, true, true);

    SDL_RenderPresent(ren);
    return ModeSelectStep::SELECTING;
}



