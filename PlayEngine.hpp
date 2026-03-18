#ifndef PLAYENGINE_HPP
#define PLAYENGINE_HPP

#include <vector>
#include <array>
#include <string>
#include <SDL2/SDL.h>
#include "CommonTypes.hpp"
#include "BMSData.hpp"
#include "ChartProjector.hpp"
#include "JudgeManager.hpp"

class PlayEngine {
public:
    void init(BMSData& data);
    void update(double cur_ms, uint32_t now);
    // isAuto: オートレーンからの打鍵。スコア・コンボ・ゲージを加算しない。
    // ただし ASSIST_OPTION==7（完全オート）の場合は呼び出し側が isAuto=false を渡す。
    int processHit(int lane, double cur_ms, uint32_t now, bool isAuto = false);
    void processRelease(int lane, double cur_ms, uint32_t now);
    void forceFail();

    double getMsFromY(int64_t target_y) const;
    int64_t getYFromMs(double cur_ms) const;
    double getBpmFromMs(double cur_ms) const;

    // ★修正①: const ref 版を追加。ScenePlay ゲームループは毎フレームこちらを使い、
    //          gaugeHistory を含む PlayStatus 全体のコピーを発生させない。
    const PlayStatus& getStatus() const { return status; }
    PlayStatus&       getStatus()       { return status; }

    const std::vector<PlayableNote>& getNotes() const { return notes; }
    const std::vector<PlayableLine>& getBeatLines() const { return beatLines; }
    JudgmentDisplay& getCurrentJudge() { return currentJudge; }
    uint32_t lastSoundPerLaneId[9];

    // ★2P VS: 2P側エンジンはBGM再生をスキップ（1P側で再生するため）
    bool skipBGM = false;

private:
    // ★修正④: BMSData bmsData を削除。init() では呼び出し元の data を直接参照し、
    //          projector にもその参照を渡す。ScenePlay::run() 中は data が生存するため安全。
    std::vector<PlayableNote> notes;
    std::vector<PlayableLine> beatLines;
    PlayStatus status;
    JudgmentDisplay currentJudge;

    ChartProjector projector;
    JudgeManager judgeManager;

    double baseRecoveryPerNote = 0.0;
    size_t nextUpdateIndex = 0;
    double lastHistoryUpdateMs = -1000.0;

    // ★修正②: レーン別ノーツインデックス。processHit の O(N) 全スキャンを O(1) に変える。
    //          laneNoteIndices[lane] = notes[] 内でそのレーンに属するインデックスのリスト（target_ms 昇順）
    //          laneSearchStart[lane] = 次に検索を始めるべき laneNoteIndices 内の位置
    std::array<std::vector<size_t>, 9> laneNoteIndices;
    std::array<size_t, 9> laneSearchStart = {};

    // 不可視オブジェ: レーン別のキュー（target_ms 昇順）。
    // キー入力時に先頭から消費し、可視ノーツの soundId を上書き発音する。
    std::array<std::vector<PlayableNote>, 9> laneHiddenNotes;
    std::array<size_t, 9> hiddenSearchStart = {};
};

#endif

