#ifndef SCENESIDESELECT_HPP
#define SCENESIDESELECT_HPP

#include <SDL2/SDL.h>
#include "NoteRenderer.hpp"

enum class SideSelectStep {
    SELECTING,
    FINISHED
};

class SceneSideSelect {
public:
    void init();
    SideSelectStep update(SDL_Renderer* ren, NoteRenderer& renderer);

private:
    int selectedSide; // 0: 1P, 1: 2P, 2: 1P+2P (VS)
};

#endif
