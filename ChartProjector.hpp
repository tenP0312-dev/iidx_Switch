#ifndef CHARTPROJECTOR_HPP
#define CHARTPROJECTOR_HPP

#include "BMSData.hpp"
#include <cstdint>

class ChartProjector {
public:
    void init(BMSData& data) {
        bmsData = &data;
        resetCursor();
        calculateAllTimestamps();
    }

    double  getMsFromY(int64_t target_y) const; // ロード時専用 O(N) — ゲームループでは呼ばない
    int64_t getYFromMs(double cur_ms) const;     // ゲームループ用 — カーソルキャッシュで O(1)
    double  getBpmFromMs(double cur_ms) const;   // 同上

    double getDurationMs(int64_t y_start, int64_t y_end) const {
        return getMsFromY(y_end) - getMsFromY(y_start);
    }

private:
    void calculateAllTimestamps();
    void resetCursor() const;
    void advanceCursor(double cur_ms) const; // カーソルを cur_ms まで前進（後退時はリセット）

    BMSData* bmsData = nullptr;

    // ゲームループ用前進カーソル（mutable: const メソッドから更新）
    mutable double   cur_ms_      = -1e18; // 最後に計算した時刻
    mutable double   elapsed_ms_  = 0.0;   // カーソル位置での経過時間
    mutable int64_t  cursor_y_    = 0;     // カーソル位置の y 座標
    mutable double   cursor_bpm_  = 0.0;   // カーソル位置での BPM
    mutable size_t   cursor_bi_   = 0;     // 次の BPM イベントインデックス
    mutable size_t   cursor_si_   = 0;     // 次の STOP イベントインデックス
    mutable bool     in_stop_     = false; // カーソルが STOP 中かどうか
};

#endif