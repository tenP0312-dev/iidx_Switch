#pragma once
#include <SDL2/SDL.h>
#include "NoteRenderer.hpp"
#include "DanData.hpp"

// 段位認定フロー全体を管理するシーン
// ScenePlay::runDan → SceneResult::runDan をコース内曲数分ループし、
// 最後に合否画面を表示する。
class SceneDan {
public:
    // コースを受け取り、フローを最後まで実行する。
    // アプリに戻る際は常にこのメソッドが返る。
    void run(SDL_Renderer* ren, NoteRenderer& renderer, const DanCourse& course);
};
