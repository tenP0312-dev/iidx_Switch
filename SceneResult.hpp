#ifndef SCENERESULT_HPP
#define SCENERESULT_HPP

#include <SDL2/SDL.h>
#include "NoteRenderer.hpp"
#include "CommonTypes.hpp"
#include "BMSData.hpp"

class SceneResult {
public:
    // リザルト画面のメインループを実行
    void run(SDL_Renderer* ren, NoteRenderer& renderer, const PlayStatus& status, const BMSHeader& header);

    // ★2P VS 用リザルト（スコア保存なし）
    void runVS(SDL_Renderer* ren, NoteRenderer& renderer,
               const PlayStatus& status1P, const BMSHeader& header1P,
               const PlayStatus& status2P, const BMSHeader& header2P);

private:
    // スコアからランク（AAA〜F）を計算
    std::string calculateRank(const PlayStatus& status);
};

#endif
