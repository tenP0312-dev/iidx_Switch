#ifndef BMSLOADER_HPP
#define BMSLOADER_HPP

#include <string>
#include <functional>
#include "BMSData.hpp"

/**
 * @brief BMS / BME / BML 形式の譜面ファイルローダー
 *
 * BmsonLoader と同一インターフェースを提供する。
 * ScenePlay / SongManager は拡張子で分岐して呼び出す。
 *
 * 対応フォーマット：
 *   .bms  … 5key + scratch (P1側 ch11-15, ch16=scratch)
 *   .bme  … 7key + scratch (P1側 ch11-17, ch16=scratch)
 *   .bml  … LN専用 (ch51-57, ch56=scratch LN)
 *
 * LN表現：
 *   #LNTYPE 1 (RDM, default) … ch51-59 で start/end ペア
 *   #LNTYPE 2 (MGQ)          … ch51-59 で連続非00が1LN
 *   #LNOBJ xx                … ch11-19 内の指定indexがLN終端
 *
 * 座標系：bmson と統一するため measure/position → y (pulse) へ変換する。
 *   resolution = 960  (= BMS 1小節192分割 × 5 でスケーリング)
 *   1小節 = resolution pulse
 *   実際の pulse = measure * resolution + position / msg_len * resolution
 *
 * チャンネルマッピング (P1側 7key):
 *   BMS ch  | bmson x | 説明
 *   --------|---------|------
 *   11      | 2       | key 1
 *   12      | 3       | key 2
 *   13      | 4       | key 3
 *   14      | 5       | key 4
 *   15      | 6       | key 5
 *   16      | 1       | scratch
 *   17      | 7       | key 6
 *   18      | 8       | key 7 (free zone / pedal)
 *   51-58   | 同上    | LNチャンネル (ch5x → chx = 1x - 40)
 */
class BmsLoader {
public:
    /**
     * @brief 譜面フルロード
     * @param path  .bms / .bme / .bml ファイルパス
     * @param onProgress 進捗コールバック [0.0, 1.0] (nullptr可)
     */
    static BMSData   load(const std::string& path,
                          std::function<void(float)> onProgress = nullptr);

    /**
     * @brief ヘッダーのみ高速ロード (選曲画面用)
     * @param path  .bms / .bme / .bml ファイルパス
     */
    static BMSHeader loadHeader(const std::string& path);

    /**
     * @brief 拡張子が BMS 系かどうかを判定するユーティリティ
     */
    static bool isBmsFile(const std::string& path);
};

#endif // BMSLOADER_HPP
