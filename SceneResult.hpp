#ifndef SCENERESULT_HPP
#define SCENERESULT_HPP

#include <SDL2/SDL.h>
#include "NoteRenderer.hpp"
#include "CommonTypes.hpp"
#include "BMSData.hpp"
#include <string>

enum class DanResultAction { CONTINUE, QUIT };

class SceneResult {
public:
    // リザルト画面のメインループを実行
    void run(SDL_Renderer* ren, NoteRenderer& renderer, const PlayStatus& status, const BMSHeader& header);

    // 段位認定用: 曲ごとのリザルト + 残ゲージ表示
    // failed=true の場合は課程失格表示。戻り値で次の動作を通知。
    DanResultAction runDan(SDL_Renderer* ren, NoteRenderer& renderer,
                           const PlayStatus& status, const BMSHeader& header,
                           int songIndex, int totalSongs,
                           double gauge, const std::string& courseName, bool failed);

    // ★2P VS 用リザルト（スコア保存なし）
    void runVS(SDL_Renderer* ren, NoteRenderer& renderer,
               const PlayStatus& status1P, const BMSHeader& header1P,
               const PlayStatus& status2P, const BMSHeader& header2P);

private:
    // スコアからランク（AAA〜F）を計算
    std::string calculateRank(const PlayStatus& status);
};

#endif
