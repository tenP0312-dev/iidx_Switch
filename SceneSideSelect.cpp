#include "SceneSideSelect.hpp"
#include "Config.hpp"
#include <string>

void SceneSideSelect::init() {
    if (Config::PLAY_MODE == 1) {
        selectedSide = 2; // VS
    } else {
        selectedSide = (Config::PLAY_SIDE == 2) ? 1 : 0;
    }
}

SideSelectStep SceneSideSelect::update(SDL_Renderer* ren, NoteRenderer& renderer) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT) return SideSelectStep::SELECTING;

        if (e.type == SDL_JOYBUTTONDOWN) {
            int btn = e.jbutton.button;
            if (btn == Config::SYS_BTN_DECIDE) {
                if (selectedSide == 2) {
                    Config::PLAY_MODE = 1;
                    Config::PLAY_SIDE = 1; // VS時は1P基準
                } else {
                    Config::PLAY_MODE = 0;
                    Config::PLAY_SIDE = (selectedSide == 0) ? 1 : 2;
                }
                return SideSelectStep::FINISHED;
            }
            if (btn == Config::SYS_BTN_LEFT) {
                selectedSide = (selectedSide + 2) % 3;
            }
            if (btn == Config::SYS_BTN_RIGHT) {
                selectedSide = (selectedSide + 1) % 3;
            }
            if (btn == Config::SYS_BTN_UP || btn == Config::SYS_BTN_DOWN) {
                selectedSide = (selectedSide + 1) % 3;
            }
        }
        
        if (e.type == SDL_KEYDOWN) {
            SDL_Keycode key = e.key.keysym.sym;
            if (key == SDLK_RETURN) {
                if (selectedSide == 2) {
                    Config::PLAY_MODE = 1;
                    Config::PLAY_SIDE = 1;
                } else {
                    Config::PLAY_MODE = 0;
                    Config::PLAY_SIDE = (selectedSide == 0) ? 1 : 2;
                }
                return SideSelectStep::FINISHED;
            }
            if (key == SDLK_LEFT) {
                selectedSide = (selectedSide + 2) % 3;
            }
            if (key == SDLK_RIGHT || key == SDLK_SPACE) {
                selectedSide = (selectedSide + 1) % 3;
            }
        }
    }

    SDL_SetRenderDrawColor(ren, 10, 10, 30, 255);
    SDL_RenderClear(ren);

    SDL_Color white = {255, 255, 255, 255};
    SDL_Color yellow = {255, 255, 0, 255};
    SDL_Color gray = {120, 120, 120, 255};
    SDL_Color cyan = {0, 255, 255, 255};
    SDL_Color orange = {255, 165, 0, 255};

    renderer.drawText(ren, "SELECT PLAY SIDE", 640, 120, yellow, true, true);
    renderer.drawText(ren, "LEFT/RIGHT TO MOVE / DECIDE TO SELECT", 640, 180, gray, true, true);
    
    renderer.drawText(ren, "1P SIDE", 320, 360, (selectedSide == 0 ? white : gray), true, true);
    renderer.drawText(ren, "2P SIDE", 640, 360, (selectedSide == 1 ? white : gray), true, true);
    renderer.drawText(ren, "1P+2P VS", 960, 360, (selectedSide == 2 ? orange : gray), true, true);

    int lineX = (selectedSide == 0) ? 320 : (selectedSide == 1) ? 640 : 960;
    SDL_Rect cursor = { lineX - 70, 400, 140, 5 };
    SDL_Color cursorColor = (selectedSide == 2) ? orange : cyan;
    SDL_SetRenderDrawColor(ren, cursorColor.r, cursorColor.g, cursorColor.b, 255);
    SDL_RenderFillRect(ren, &cursor);
    
    const char* modeText = (selectedSide == 0) ? "1P" : (selectedSide == 1) ? "2P" : "1P+2P VS";
    renderer.drawText(ren, std::string("CURRENT: ") + modeText, 640, 550, cyan, true, true);

    SDL_RenderPresent(ren);
    return SideSelectStep::SELECTING;
}
