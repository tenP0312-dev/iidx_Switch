#pragma once
#include <SDL2/SDL.h>
#include "NoteRenderer.hpp"
#include "DanData.hpp"

enum class DanSelectStep { SELECTING, GO_DAN, BACK };

class SceneDanSelect {
public:
    void init(const DanData& data);
    DanSelectStep update(SDL_Renderer* ren, NoteRenderer& renderer);

    // 選択されたコースを返す（GO_DAN 時に呼ぶ）
    const DanCourse& selectedCourse() const { return *selected_; }

private:
    // visible なコースのみ
    std::vector<const DanCourse*> visible_;
    int cursor_ = 0;
    const DanCourse* selected_ = nullptr;
};
