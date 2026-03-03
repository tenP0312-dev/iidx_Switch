#ifndef SCENEPLAY_HPP
#define SCENEPLAY_HPP

#include <string>
#include <vector>
#include <SDL2/SDL.h>
#include "SoundManager.hpp"
#include "NoteRenderer.hpp"
#include "CommonTypes.hpp"
#include "BMSData.hpp"

// 前方宣言
class PlayEngine;
class BgaManager; 

class ScenePlay {
public:
    bool run(SDL_Renderer* ren, SoundManager& snd, NoteRenderer& renderer, const std::string& bmsonPath);
    const PlayStatus& getStatus() const { return status; }
    const BMSHeader& getHeader() const { return currentHeader; }

private:
    // --- 内部処理用関数（重複を削除し、ここに集約） ---
    bool processInput(double cur_ms, uint32_t now, SoundManager& snd, PlayEngine& engine);
    void updateAssist(double cur_ms, PlayEngine& engine, SoundManager& snd);
    void renderScene(SDL_Renderer* ren, NoteRenderer& renderer, PlayEngine& engine, 
                     BgaManager& bga, 
                     double cur_ms, int64_t cur_y, int fps, const BMSHeader& header, 
                     uint32_t now, double progress);

    // --- 補助関数 ---
    bool isAutoLane(int lane);
    int getLaneFromJoystickButton(int btn);

    // --- メンバ変数 ---
    // ★修正: effects / bombAnims を std::vector から固定サイズ配列に変更。
    //        旧実装は reserve(64) していたが、連打譜面で容量を超えた瞬間に
    //        realloc + コピーが走り、入力応答にスパイクが発生していた。
    //        リズムゲームでの入力遅延は致命的。
    //        8レーン × 同時押し = 最大8エフェクト。128で絶対に枯渇しない。
    //        固定配列なら push/pop が O(1) 保証、ヒープアロケーション ゼロ。
    static constexpr size_t MAX_EFFECTS = 128;
    static constexpr size_t MAX_BOMBS   = 128;
    ActiveEffect effectsBuf[MAX_EFFECTS];
    BombAnim     bombAnimsBuf[MAX_BOMBS];
    size_t       effectCount = 0;
    size_t       bombCount   = 0;

    PlayStatus status;          
    BMSHeader currentHeader;

    bool isAssistUsed = false;
    bool startButtonPressed = false;     
    bool effectButtonPressed = false;    

    bool lanePressed[9] = {false}; 

    bool scratchUpActive = false;
    bool scratchDownActive = false;

    uint32_t lastStartPressTime = 0;
    uint32_t startTicks = 0; // ★修正(CRITICAL-1): 曲開始時刻。ev.timestamp から cur_ms を計算するために保持。
    uint32_t lastLNBombTime[9] = {};  // LN押下中ボム: レーンごとの最終発火時刻
    int      lnHitJudge[9]     = {};  // LN押下時の判定結果 (0=なし,2=GREAT,3=PGREAT)
    int backupSudden = 300; 
    
    // 最適化用インデックス
    size_t drawStartIndex = 0; 
};

#endif








