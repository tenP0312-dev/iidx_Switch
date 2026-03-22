#ifndef SCENEMODESELECT_HPP
#define SCENEMODESELECT_HPP

#include <SDL2/SDL.h>
#include "NoteRenderer.hpp"

enum class ModeSelectStep {
    SELECTING,
    GO_SELECT,
    GO_OPTION,
    GO_DAN
};

class SceneModeSelect {
public:
    void init();
    ModeSelectStep update(SDL_Renderer* ren, NoteRenderer& renderer);

    // main.cppからのエラーを解消するために追加（既存ロジック100%継承）
    int getSelect() const { return selectedMode; }

private:
    enum class InternalStep { SIDE_SELECT, MODE_SELECT };
    InternalStep currentStep;
    int selectedSide;
    int selectedMode;
};

#endif



