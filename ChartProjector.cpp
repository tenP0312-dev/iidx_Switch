#include "ChartProjector.hpp"

double ChartProjector::getMsFromY(int64_t target_y) const {
    if (!bmsData) return 0.0;
    double total_ms = 0.0, current_bpm = bmsData->header.bpm;
    int64_t current_y = 0, res = bmsData->header.resolution;

    // BPM変化とSTOPイベントをy順にマージして処理
    size_t bi = 0, si = 0;
    const auto& bpms  = bmsData->bpm_events;
    const auto& stops = bmsData->stop_events;

    while (bi < bpms.size() || si < stops.size()) {
        // 次のイベントのy座標を決める
        int64_t next_y = INT64_MAX;
        bool isBpm  = false, isStop = false;
        if (bi < bpms.size()  && bpms[bi].y  <= next_y) { next_y = bpms[bi].y;  isBpm  = true; isStop = false; }
        if (si < stops.size() && stops[si].y  <  next_y) { next_y = stops[si].y; isBpm  = false; isStop = true; }
        if (si < stops.size() && stops[si].y == next_y && isBpm) isStop = true; // 同y: BPM変化→STOP順

        if (next_y > target_y) break;

        // current_y → next_y の時間を加算
        if (res > 0 && current_bpm > 0)
            total_ms += (double)(next_y - current_y) * (60000.0 / (current_bpm * res));
        current_y = next_y;

        if (isBpm && bi < bpms.size() && bpms[bi].y == next_y) {
            current_bpm = bpms[bi].bpm;
            ++bi;
            // 同y にSTOPもある場合はBPM変化後のbpmでSTOP時間を計算するためここでは加算しない
        }
        if (isStop && si < stops.size() && stops[si].y == next_y) {
            total_ms += stops[si].duration_ms;
            ++si;
        }
    }
    if (res > 0 && current_bpm > 0)
        total_ms += (double)(target_y - current_y) * (60000.0 / (current_bpm * res));
    return total_ms;
}

// ────────────────────────────────────────────────────────────────────────────
// カーソルキャッシュ実装
// ゲームループで毎フレーム呼ばれる getYFromMs / getBpmFromMs 専用。
// cur_ms は単調増加を前提とし、前回位置から前進するだけなので O(1) 相当。
// 後退（シーク等）時は先頭からリセットする。
// ────────────────────────────────────────────────────────────────────────────

void ChartProjector::resetCursor() const {
    cur_ms_     = -1e18;
    elapsed_ms_ = 0.0;
    cursor_y_   = 0;
    cursor_bpm_ = bmsData ? bmsData->header.bpm : 0.0;
    cursor_bi_  = 0;
    cursor_si_  = 0;
    in_stop_    = false;
}

void ChartProjector::advanceCursor(double cur_ms) const {
    if (!bmsData) return;
    if (cur_ms == cur_ms_) return; // 同一時刻は再計算しない

    if (cur_ms < cur_ms_) resetCursor(); // 後退: 先頭から再スキャン

    const auto& bpms  = bmsData->bpm_events;
    const auto& stops = bmsData->stop_events;
    const double res  = bmsData->header.resolution;

    in_stop_ = false;

    while (cursor_bi_ < bpms.size() || cursor_si_ < stops.size()) {
        int64_t next_y = INT64_MAX;
        bool isBpm = false, isStop = false;
        if (cursor_bi_ < bpms.size()  && bpms[cursor_bi_].y  <= next_y) { next_y = bpms[cursor_bi_].y;  isBpm  = true; isStop = false; }
        if (cursor_si_ < stops.size() && stops[cursor_si_].y  <  next_y) { next_y = stops[cursor_si_].y; isBpm  = false; isStop = true;  }
        if (cursor_si_ < stops.size() && stops[cursor_si_].y == next_y && isBpm) isStop = true;

        double step = (res > 0 && cursor_bpm_ > 0)
            ? (double)(next_y - cursor_y_) * (60000.0 / (cursor_bpm_ * res))
            : 0.0;

        if (elapsed_ms_ + step > cur_ms) break; // このセグメント内で cur_ms に到達

        elapsed_ms_ += step;
        cursor_y_    = next_y;

        if (isBpm && cursor_bi_ < bpms.size() && bpms[cursor_bi_].y == next_y) {
            cursor_bpm_ = bpms[cursor_bi_].bpm;
            ++cursor_bi_;
        }
        if (isStop && cursor_si_ < stops.size() && stops[cursor_si_].y == next_y) {
            double stop_dur = stops[cursor_si_].duration_ms;
            if (elapsed_ms_ + stop_dur > cur_ms) {
                in_stop_ = true; // STOP 区間内
                break;
            }
            elapsed_ms_ += stop_dur;
            ++cursor_si_;
        }
    }
    cur_ms_ = cur_ms;
}

int64_t ChartProjector::getYFromMs(double cur_ms) const {
    if (!bmsData) return 0;
    const double res = bmsData->header.resolution;
    if (cur_ms < 0) return (int64_t)(cur_ms * (bmsData->header.bpm * res / 60000.0));

    advanceCursor(cur_ms);
    if (in_stop_) return cursor_y_;
    if (res <= 0 || cursor_bpm_ <= 0) return cursor_y_;
    return cursor_y_ + (int64_t)((cur_ms - elapsed_ms_) * (cursor_bpm_ * res / 60000.0));
}

double ChartProjector::getBpmFromMs(double cur_ms) const {
    if (!bmsData) return 120.0;
    if (cur_ms < 0) return bmsData->header.bpm;
    advanceCursor(cur_ms);
    return cursor_bpm_;
}

// 【追加】既存の getMsFromY を利用して全データに時間情報を付与する
void ChartProjector::calculateAllTimestamps() {
    if (!bmsData) return;

    for (auto& channel : bmsData->sound_channels) {
        for (auto& note : channel.notes) {
            note.hit_ms = getMsFromY(note.y);
        }
    }

    for (auto& line : bmsData->lines) {
        line.hit_ms = getMsFromY(line.y);
    }
}
