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

int64_t ChartProjector::getYFromMs(double cur_ms) const {
    if (!bmsData) return 0;
    double res = bmsData->header.resolution;
    if (cur_ms < 0) return (int64_t)(cur_ms * (bmsData->header.bpm * res / 60000.0));

    double elapsed_ms = 0, current_bpm = bmsData->header.bpm;
    int64_t current_y = 0;

    const auto& bpms  = bmsData->bpm_events;
    const auto& stops = bmsData->stop_events;
    size_t bi = 0, si = 0;

    while (bi < bpms.size() || si < stops.size()) {
        int64_t next_y = INT64_MAX;
        bool isBpm = false, isStop = false;
        if (bi < bpms.size()  && bpms[bi].y  <= next_y) { next_y = bpms[bi].y;  isBpm  = true; isStop = false; }
        if (si < stops.size() && stops[si].y  <  next_y) { next_y = stops[si].y; isBpm  = false; isStop = true; }
        if (si < stops.size() && stops[si].y == next_y && isBpm) isStop = true;

        // current_y → next_y の時間
        double step = (res > 0 && current_bpm > 0)
            ? (double)(next_y - current_y) * (60000.0 / (current_bpm * res))
            : 0.0;

        if (elapsed_ms + step > cur_ms) break;
        elapsed_ms += step;
        current_y = next_y;

        if (isBpm && bi < bpms.size() && bpms[bi].y == next_y) {
            current_bpm = bpms[bi].bpm;
            ++bi;
        }
        if (isStop && si < stops.size() && stops[si].y == next_y) {
            double stop_dur = stops[si].duration_ms;
            if (elapsed_ms + stop_dur > cur_ms) {
                // STOP 中なので y は動かない
                return current_y;
            }
            elapsed_ms += stop_dur;
            ++si;
        }
    }
    if (res <= 0 || current_bpm <= 0) return current_y;
    return current_y + (int64_t)((cur_ms - elapsed_ms) * (current_bpm * res / 60000.0));
}

double ChartProjector::getBpmFromMs(double cur_ms) const {
    if (!bmsData) return 120.0;
    if (cur_ms < 0) return bmsData->header.bpm;
    double elapsed_ms = 0, current_bpm = bmsData->header.bpm, res = bmsData->header.resolution;
    int64_t current_y = 0;

    const auto& bpms  = bmsData->bpm_events;
    const auto& stops = bmsData->stop_events;
    size_t bi = 0, si = 0;

    while (bi < bpms.size() || si < stops.size()) {
        int64_t next_y = INT64_MAX;
        bool isBpm = false, isStop = false;
        if (bi < bpms.size()  && bpms[bi].y  <= next_y) { next_y = bpms[bi].y;  isBpm  = true; isStop = false; }
        if (si < stops.size() && stops[si].y  <  next_y) { next_y = stops[si].y; isBpm  = false; isStop = true; }
        if (si < stops.size() && stops[si].y == next_y && isBpm) isStop = true;

        double step = (res > 0 && current_bpm > 0)
            ? (double)(next_y - current_y) * (60000.0 / (current_bpm * res))
            : 0.0;
        if (elapsed_ms + step > cur_ms) break;
        elapsed_ms += step;
        current_y = next_y;

        if (isBpm && bi < bpms.size() && bpms[bi].y == next_y) {
            current_bpm = bpms[bi].bpm;
            ++bi;
        }
        if (isStop && si < stops.size() && stops[si].y == next_y) {
            double stop_dur = stops[si].duration_ms;
            if (elapsed_ms + stop_dur > cur_ms) return current_bpm;
            elapsed_ms += stop_dur;
            ++si;
        }
    }
    return current_bpm;
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
