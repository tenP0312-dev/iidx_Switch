#ifndef SCENEPLAY_HPP
#define SCENEPLAY_HPP

#include <string>
#include <vector>
#include <SDL2/SDL.h>
#include "NoteRenderer.hpp"
#include "CommonTypes.hpp"
#include "BMSData.hpp"

// 前方宣言
class PlayEngine;
class BgaManager;

// ============================================================
//  段位認定: 1曲分のプレイ引数
// ============================================================
struct DanPlayContext {
    double initialGauge = 100.0; // 引き継ぎゲージ (0〜100)
    int    gaugeOption  = 0;     // Config::GAUGE_OPTION に一時設定する値
    int    songIndex    = 0;     // 0-based (表示用)
    int    totalSongs   = 1;     // 課程の全曲数 (表示用)
};

// ============================================================
//  ★2P VS: 1プレイヤー分の状態
// ============================================================
struct PlayerState {
    static constexpr size_t MAX_EFFECTS = 128;
    static constexpr size_t MAX_BOMBS   = 128;

    ActiveEffect effectsBuf[MAX_EFFECTS];
    BombAnim     bombAnimsBuf[MAX_BOMBS];
    size_t       effectCount = 0;
    size_t       bombCount   = 0;

    bool lanePressed[9]        = {false};
    int  lnHitJudge[9]         = {};
    uint32_t lastLNBombTime[9] = {};

    bool scratchUpActive   = false;
    bool scratchDownActive = false;

    size_t drawStartIndex = 0;

    // プレイヤー別HS/サドプラ/リフト
    double highSpeed   = 1.0;
    int    greenNumber = 300;
    int    suddenPlus  = 0;
    int    lift        = 0;
    int    backupSudden = 300;

    bool isPlayerFailed = false;

    void reset() {
        effectCount = 0;
        bombCount   = 0;
        for (int i = 0; i < 9; ++i) {
            lanePressed[i]    = false;
            lnHitJudge[i]     = 0;
            lastLNBombTime[i] = 0;
        }
        scratchUpActive   = false;
        scratchDownActive = false;
        drawStartIndex    = 0;
        isPlayerFailed    = false;
    }
};

class ScenePlay {
public:
    // 1P用（既存互換）
    bool run(SDL_Renderer* ren, NoteRenderer& renderer, const std::string& bmsonPath);

    // 段位認定用: DanPlayContext でゲージ引き継ぎ・ゲージ種別を指定
    bool runDan(SDL_Renderer* ren, NoteRenderer& renderer,
                const std::string& bmsonPath, const DanPlayContext& ctx);
    double getFinalGauge() const { return finalGauge_; }

    // ★2P VS用
    bool runVS(SDL_Renderer* ren, NoteRenderer& renderer,
               const std::string& path1P, const std::string& path2P);

    // ★2P VS: プレイヤー指定でステータス/ヘッダーを取得
    const PlayStatus& getStatus(int playerIdx = 0) const;
    const BMSHeader&  getHeader(int playerIdx = 0) const;
    int getNumPlayers() const { return numPlayers; }

private:
    // --- 既存1P用メソッド ---
    bool processInput(double cur_ms, uint32_t now, PlayEngine& engine);
    void updateAssist(double cur_ms, PlayEngine& engine);
    void renderScene(SDL_Renderer* ren, NoteRenderer& renderer, PlayEngine& engine,
                     BgaManager& bga,
                     double cur_ms, int64_t cur_y, int fps, const BMSHeader& header,
                     uint32_t now, double progress);

    // --- 補助関数 ---
    bool isAutoLane(int lane);
    int getLaneFromJoystickButton(int btn);
    void fadeIn(SDL_Renderer* ren, NoteRenderer& renderer, PlayEngine& engine,
                BgaManager& bga, double cur_ms, int64_t cur_y,
                const BMSHeader& header, uint32_t baseNow, int durationMs);
    void fadeOut(SDL_Renderer* ren, NoteRenderer& renderer, PlayEngine& engine,
                 BgaManager& bga, double cur_ms, int64_t cur_y,
                 const BMSHeader& header, uint32_t baseNow, int durationMs);

    // --- ★2P VS用メソッド ---
    bool processInputVS(double cur_ms, uint32_t now,
                        PlayEngine& engine1P, PlayEngine& engine2P,
                        SDL_JoystickID joy1ID, SDL_JoystickID joy2ID);
    void updateAssistForPlayer(double cur_ms, PlayEngine& engine, PlayerState& ps);
    void renderPlayerField(SDL_Renderer* ren, NoteRenderer& renderer,
                           PlayEngine& engine, PlayerState& ps,
                           int side, double cur_ms, int64_t cur_y,
                           int fps, uint32_t now, double progress,
                           const BMSHeader& header);
    void handlePlayerButton(int playerIdx, int lane, bool isDown,
                            double hit_ms, uint32_t now,
                            PlayEngine& engine, PlayerState& ps);

    // --- メンバ変数 ---
    int numPlayers = 1;
    PlayerState players[2];          // ★2P VS用
    PlayStatus  vsStatuses[2];       // ★2P VS: run終了後にコピー
    BMSHeader   vsHeaders[2];        // ★2P VS

    // 既存1P用（互換維持）
    static constexpr size_t MAX_EFFECTS = 128;
    static constexpr size_t MAX_BOMBS   = 128;
    ActiveEffect effectsBuf[MAX_EFFECTS];
    BombAnim     bombAnimsBuf[MAX_BOMBS];
    size_t       effectCount = 0;
    size_t       bombCount   = 0;

    PlayStatus status;
    BMSHeader currentHeader;

    bool isAssistUsed = false;
    bool startButtonPressed   = false;
    bool start2PButtonPressed = false; // ★2P VS: 2PのSTARTボタン状態
    bool decideButtonPressed  = false;
    bool effectButtonPressed  = false;

    bool lanePressed[9] = {false};

    bool scratchUpActive = false;
    bool scratchDownActive = false;

    uint32_t lastStartPressTime   = 0;
    uint32_t last2PStartPressTime = 0; // ★2P VS: 2PのSTARTボタン最終押下時刻
    uint32_t startTicks = 0;
    uint32_t lastLNBombTime[9] = {};
    int      lnHitJudge[9]     = {};
    int backupSudden = 300;

    size_t drawStartIndex = 0;

    // 【指摘(3-5)修正】pixels_per_y キャッシュ（renderScene 内で使用）
    // HIGH_SPEED または resolution が変化したときだけ再計算する。
    mutable double   cachedHS_         = -1.0;
    mutable int      cachedResolution_ = -1;
    mutable double   cachedPixelsPerY_  = 1.0;
    mutable double   cachedMaxVisibleY_ = 10000.0;

    // ★デバッグ: true の間は BG・ノーツ以外を非表示（位置合わせ用）
    // 確認完了後は false に変更するか、このフラグごと削除する
    bool debugLayoutMode = false;

    // 段位認定用: runDan() が run() を呼ぶ前にセットし、run() 内で参照する
    bool         hasDanCtx_ = false;
    DanPlayContext danCtx_  = {};
    double       finalGauge_ = 100.0;
};


#endif
