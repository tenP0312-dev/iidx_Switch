#include "PlayEngine.hpp"
#include "SoundManager.hpp"
#include "Config.hpp"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <array>
#include <random>
#include <map>
#include <set>

// TOTAL値計算 (HappySky仕様)
int calculateHSRecoveryInternal(int notes) {
    if (notes <= 0) return 0;
    if (notes < 350) {
        return 80000 / (notes * 6);
    } else {
        return 80000 / (notes * 2 + 1400);
    }
}

void PlayEngine::init(BMSData& data) {
    // ★修正④: bmsData = data を削除。data は ScenePlay::run() で生存し続けるため、
    //          参照を渡すだけで安全。sound_channels (数千ノーツ分) の二重確保を回避。
    projector.init(data);

    status = PlayStatus();
    notes.clear();
    notes.reserve(data.header.totalNotes + 100);
    beatLines.clear();
    currentJudge = JudgmentDisplay();

    status.totalNotes   = 0;
    status.maxTargetMs  = 0;

    int laneMap[9];
    for (int i = 0; i <= 8; i++) laneMap[i] = i;

    // 決定論的シード: 譜面タイトル・チャート名・総ノーツ数・MORE_NOTES_COUNTから生成。
    // std::hash<string> はプラットフォーム依存で環境ごとに変わるため使用しない。
    // FNV-1a (32bit) で文字列をハッシュし、数値と混ぜて最終シードを作る。
    // 同じ譜面・同じ設定なら機種・再起動をまたいで必ず同じ配置になる。
    auto fnv1a32 = [](const std::string& s) -> uint32_t {
        uint32_t h = 2166136261u;
        for (unsigned char c : s) { h ^= c; h *= 16777619u; }
        return h;
    };
    uint32_t chartSeed = fnv1a32(data.header.title);
    chartSeed ^= fnv1a32(data.header.chartName) * 2654435761u;
    chartSeed ^= static_cast<uint32_t>(data.header.totalNotes) * 2246822519u;
    chartSeed ^= static_cast<uint32_t>(Config::MORE_NOTES_COUNT) * 3266489917u;
    // avalanche mix
    chartSeed ^= chartSeed >> 16; chartSeed *= 0x45d9f3bu; chartSeed ^= chartSeed >> 16;
    std::mt19937 g(chartSeed);

    if (Config::PLAY_OPTION == 1) { // RANDOM
        std::vector<int> kbd = {1, 2, 3, 4, 5, 6, 7};
        std::shuffle(kbd.begin(), kbd.end(), g);
        for (int i = 1; i <= 7; i++) laneMap[i] = kbd[i - 1];
    }
    else if (Config::PLAY_OPTION == 2) { // R-RANDOM
        int shift = std::uniform_int_distribution<int>(1, 6)(g);
        for (int i = 1; i <= 7; i++) laneMap[i] = ((i - 1 + shift) % 7) + 1;
    }
    else if (Config::PLAY_OPTION == 4) { // MIRROR
        for (int i = 1; i <= 7; i++) laneMap[i] = 8 - i;
    }

    struct TempNote {
        int64_t  y;
        int64_t  l;
        int      originalLane;
        uint32_t soundId;
        bool     isBGM;
    };
    std::vector<TempNote> tempNotes;
    tempNotes.reserve(data.header.totalNotes * 2);

    // FNV-1a (32bit) ラムダ。SoundManager::getHash と同一アルゴリズムで統一。
    // std::hash<std::string> はプラットフォーム依存のため使わない。
    auto fnv1a32_sound = [](const std::string& s) -> uint32_t {
        uint32_t h = 2166136261u;
        for (unsigned char c : s) { h ^= c; h *= 16777619u; }
        return h;
    };

    for (const auto& ch : data.sound_channels) {
        uint32_t sId = fnv1a32_sound(ch.name);
        for (const auto& n : ch.notes) {
            // 不可視オブジェ: x が負値（BmsLoaderが -lane として格納）
            if (n.x < 0) {
                int lane = (int)(-n.x);
                if (lane >= 1 && lane <= 8) {
                    PlayableNote pn;
                    pn.y           = n.y;
                    pn.target_ms   = projector.getMsFromY(n.y);
                    pn.lane        = lane;
                    pn.soundId     = sId;
                    pn.isInvisible = true;
                    pn.isBGM       = false;
                    laneHiddenNotes[lane].push_back(pn);
                }
                continue;
            }
            bool isBGM = (n.x < 1 || n.x > 8);
            tempNotes.push_back({n.y, n.l, (int)n.x, sId, isBGM});
        }
    }

    // laneHiddenNotes を target_ms 昇順にソート
    for (int lane = 1; lane <= 8; ++lane) {
        std::sort(laneHiddenNotes[lane].begin(), laneHiddenNotes[lane].end(),
                  [](const PlayableNote& a, const PlayableNote& b) {
                      return a.target_ms < b.target_ms;
                  });
        hiddenSearchStart[lane] = 0;
    }

    std::sort(tempNotes.begin(), tempNotes.end(), [](const TempNote& a, const TempNote& b) {
        if (a.y != b.y) return a.y < b.y;
        return a.originalLane < b.originalLane;
    });

    // ── MORE NOTES: BGMノーツを単発ノーツとしてプレイレーンに追加 ──────────
    // EX_OPTION==3 のとき、BGM単発ノーツをランダムレーンに移動して追加する。
    // LNはスキップ。追加数はMORE_NOTES_COUNTまで（BGM実数でのみキャップ、上限なし）。
    // 縦連回避: 既存ノーツとのy距離をlower_boundで正確に計算。3パスで制約を段階的に緩和。
    // LN占有区間回避: 既存プレイLN押下中のレーンを避ける。
    // この処理はランダムオプション適用前に行い、後でS-RANDOM等が適用される。
    if (Config::EX_OPTION == 2 && Config::MORE_NOTES_COUNT > 0) {
        // 1. BGM単発ノーツを収集
        std::vector<size_t> bgmCandidates;
        bgmCandidates.reserve(512);
        for (size_t i = 0; i < tempNotes.size(); ++i) {
            if (tempNotes[i].isBGM && tempNotes[i].l == 0)
                bgmCandidates.push_back(i);
        }

        // 2. 既存プレイLNの占有区間を収集
        std::unordered_map<int, std::vector<std::pair<int64_t,int64_t>>> lnOccupied;
        for (const auto& tn : tempNotes) {
            if (!tn.isBGM && tn.l > 0 && tn.originalLane >= 1 && tn.originalLane <= 7) {
                lnOccupied[tn.originalLane].emplace_back(tn.y, tn.y + tn.l);
            }
        }

        // 3. 追加ノーツを生成（上限なし: BGM実数でのみキャップ）
        int addCount = std::min((int)bgmCandidates.size(), Config::MORE_NOTES_COUNT);
        if ((int)bgmCandidates.size() > addCount) {
            std::shuffle(bgmCandidates.begin(), bgmCandidates.end(), g);
            bgmCandidates.resize(addCount);
        }

        // y座標でソートして処理
        std::sort(bgmCandidates.begin(), bgmCandidates.end(), [&](size_t a, size_t b){
            return tempNotes[a].y < tempNotes[b].y;
        });

        // 縦連回避: 各レーンの既存ノーツy座標をソート済みリストで保持
        // lower_boundで「直前の既存ノーツy」を O(logN) で取得する
        // ANTI_CONSEC_Y: resolutionベースで動的計算。
        //   16分音符1.5個分 (resolution*3/8) を基準にし、
        //   密度が高い譜面でもパス3への落下を抑制する。
        const int64_t res = data.header.resolution;
        const int64_t ANTI_CONSEC_Y = std::max<int64_t>(res * 3 / 8, 120); // 16分音符1.5個分相当
        std::vector<int64_t> existingY[9]; // lane 1-7
        for (const auto& tn : tempNotes) {
            if (!tn.isBGM && tn.originalLane >= 1 && tn.originalLane <= 7)
                existingY[tn.originalLane].push_back(tn.y);
        }
        for (int i = 1; i <= 7; i++)
            std::sort(existingY[i].begin(), existingY[i].end());

        // 追加ノーツ処理中の「直前使用y」（追加ノーツ同士の縦連管理用）
        int64_t lastAddedY[9];
        for (int i = 0; i < 9; i++) lastAddedY[i] = -999999;

        // レーン使用カウンタ: 頻繁に使われたレーンを優先度下げる
        int laneUseCount[9] = {};

        // 同一yの既存プレイノーツを事前マスク登録
        std::unordered_map<int64_t, uint8_t> moreUsedMask;
        for (const auto& tn : tempNotes) {
            if (!tn.isBGM && tn.originalLane >= 1 && tn.originalLane <= 7)
                moreUsedMask[tn.y] |= (1u << tn.originalLane);
        }

        auto checkLnBlocked = [&](int lane, int64_t y) -> bool {
            auto it = lnOccupied.find(lane);
            if (it == lnOccupied.end()) return false;
            for (const auto& seg : it->second) {
                if (y > seg.first && y < seg.second) return true;
            }
            return false;
        };

        // 既存ノーツから「y未満の直前y」を取得
        auto getPrevExistY = [&](int lane, int64_t y) -> int64_t {
            const auto& v = existingY[lane];
            if (v.empty()) return -999999;
            auto it = std::lower_bound(v.begin(), v.end(), y);
            if (it == v.begin()) return -999999;
            --it;
            return *it;
        };

        // 既存ノーツから「yより大きい直後y」を取得
        auto getNextExistY = [&](int lane, int64_t y) -> int64_t {
            const auto& v = existingY[lane];
            if (v.empty()) return INT64_MAX;
            auto it = std::upper_bound(v.begin(), v.end(), y);
            if (it == v.end()) return INT64_MAX;
            return *it;
        };

        // 直前に置いたレーンを記録（パス3の最低限縦連回避用）
        int lastChosenLane = -1;

        // ループ外で一度だけ確保。std::array はスタック上に置かれ、malloc/free が走らない。
        std::array<int, 7> lanes = {1, 2, 3, 4, 5, 6, 7};

        for (size_t idx : bgmCandidates) {
            const TempNote& src = tempNotes[idx];
            int64_t y = src.y;
            uint8_t& ymask = moreUsedMask[y];

            // レーンをuse countの少ない順に並べ、均等分散を促進
            // 毎回 {1,2,3,4,5,6,7} にリセットしてからシャッフルする
            std::iota(lanes.begin(), lanes.end(), 1);
            std::shuffle(lanes.begin(), lanes.end(), g);
            std::stable_sort(lanes.begin(), lanes.end(), [&](int a, int b){
                return laneUseCount[a] < laneUseCount[b];
            });

            int chosen = -1;
            // パス1: 直前+直後の既存ノーツ・追加ノーツ両方との縦連を回避 + LN回避
            for (int lane : lanes) {
                if (ymask & (1u << lane)) continue;
                int64_t prevY = std::max(getPrevExistY(lane, y), lastAddedY[lane]);
                int64_t nextY = getNextExistY(lane, y);
                if (y - prevY < ANTI_CONSEC_Y) continue;
                if (nextY - y < ANTI_CONSEC_Y) continue;
                if (!checkLnBlocked(lane, y)) { chosen = lane; break; }
            }
            // パス2: 追加ノーツ同士の縦連のみ回避 + 直後の既存ノーツとの縦連も回避
            if (chosen < 0) {
                for (int lane : lanes) {
                    if (ymask & (1u << lane)) continue;
                    if (y - lastAddedY[lane] < ANTI_CONSEC_Y) continue;
                    int64_t nextY = getNextExistY(lane, y);
                    if (nextY - y < ANTI_CONSEC_Y) continue;
                    if (!checkLnBlocked(lane, y)) { chosen = lane; break; }
                }
            }
            // パス3: 直後チェックのみ（直前は許容）
            if (chosen < 0) {
                for (int lane : lanes) {
                    if (ymask & (1u << lane)) continue;
                    if (lane == lastChosenLane) continue;
                    int64_t nextY = getNextExistY(lane, y);
                    if (nextY - y < ANTI_CONSEC_Y) continue;
                    if (!checkLnBlocked(lane, y)) { chosen = lane; break; }
                }
            }
            // パス4: LN回避のみ。直前・直後とも許容するが直前レーンは除外
            if (chosen < 0) {
                for (int lane : lanes) {
                    if (ymask & (1u << lane)) continue;
                    if (lane == lastChosenLane) continue;
                    if (!checkLnBlocked(lane, y)) { chosen = lane; break; }
                }
            }
            // パス5: 完全フォールバック（同一yに空きがあれば何でも置く）
            if (chosen < 0) {
                for (int lane : lanes) {
                    if (ymask & (1u << lane)) continue;
                    if (!checkLnBlocked(lane, y)) { chosen = lane; break; }
                }
            }
            if (chosen < 0) continue;

            ymask |= (1u << chosen);
            lastAddedY[chosen] = y;
            laneUseCount[chosen]++;
            lastChosenLane = chosen;

            TempNote added = src;
            added.isBGM        = false;
            added.originalLane = chosen;
            tempNotes.push_back(added);
        }

        // 追加後に再ソート
        std::sort(tempNotes.begin(), tempNotes.end(), [](const TempNote& a, const TempNote& b) {
            if (a.y != b.y) return a.y < b.y;
            return a.originalLane < b.originalLane;
        });
    }
    // ─────────────────────────────────────────────────────────────────────

    // ★修正: std::map<int64_t, std::set<int>> を廃止。
    //        旧実装は S-RANDOM 時に全ノーツ分の map ノード + set ノードを動的確保していた。
    //        2000ノーツで数千回の new/delete が発生し、init 時間が数百ms 増加していた。
    //        7鍵盤は bit0-bit7 で表現できる (lane 1-7 を bit 1-7 に対応)。
    //        unordered_map<y, uint8_t> なら: メモリ = 1バイト/Y値 (set の 1/40 以下)。
    //        ビットマスクのチェックは O(1)、キャッシュライン効率も圧倒的に改善。
    std::unordered_map<int64_t, uint8_t> usedLanesMask;
    if (Config::PLAY_OPTION == 3) // S-RANDOM 時のみ使用するので事前確保
        usedLanesMask.reserve(tempNotes.size());

    for (const auto& tn : tempNotes) {
        PlayableNote pn;
        pn.target_ms = projector.getMsFromY(tn.y);
        pn.y         = tn.y;
        pn.soundId   = tn.soundId;
        pn.isBGM     = tn.isBGM;

        bool isLegacyModel = (Config::ASSIST_OPTION == 2 || Config::ASSIST_OPTION == 4 || Config::ASSIST_OPTION == 6);

        pn.l = tn.l;
        if (pn.l > 0) {
            if (isLegacyModel) {
                pn.l         = 0;
                pn.isLN      = false;
                pn.duration_ms = 0;
            } else {
                pn.isLN      = true;
                double end_ms  = projector.getMsFromY(tn.y + tn.l);
                pn.duration_ms = end_ms - pn.target_ms;
            }
        } else {
            pn.isLN      = false;
            pn.duration_ms = 0;
        }

        if (!pn.isBGM) {
            status.totalNotes++;
            if (tn.originalLane == 8) {
                pn.lane = 8;
            } else {
                if (Config::PLAY_OPTION == 3) { // S-RANDOM
                    // 【指摘(3-3)修正】std::vector → std::array
                    // 旧: std::vector の初期化リスト構築は毎回 malloc を呼ぶ。
                    // 新: std::array はスタック上 28 バイト、ヒープアロケーションゼロ。
                    std::array<int, 7> candidates = {1, 2, 3, 4, 5, 6, 7};
                    std::shuffle(candidates.begin(), candidates.end(), g);
                    int selected = candidates[0];
                    uint8_t& mask = usedLanesMask[tn.y]; // operator[] は初回ゼロ初期化
                    for (int c : candidates) {
                        if (!(mask & (1u << c))) { // ビットが立っていなければ未使用
                            selected = c;
                            break;
                        }
                    }
                    pn.lane = selected;
                    mask |= (1u << selected); // 使用済みにマーク
                } else {
                    pn.lane = laneMap[tn.originalLane];
                }
            }
        } else {
            pn.lane = tn.originalLane;
        }

        notes.push_back(pn);

        double noteEndMs = pn.target_ms + pn.duration_ms;
        if (noteEndMs > status.maxTargetMs) status.maxTargetMs = noteEndMs;
    }

    std::sort(notes.begin(), notes.end(), [](const PlayableNote& a, const PlayableNote& b) {
        return a.target_ms < b.target_ms;
    });

    // ── BSS検出 ──────────────────────────────────────────────────────────
    // BSSチェーンの定義:
    //   lane==8 で LN が gap=32 で2本以上連続し、末尾LNの終点に単発(gap=0)が来るパターン。
    //   先頭LN → isBSSHead、中間LN → isBSSMid、末尾LN → isBSSTail
    //   単発はフラグなし（bss_eに隠れるのでそのまま描画）
    {
        std::vector<size_t> scratchIdx;
        for (size_t i = 0; i < notes.size(); ++i) {
            if (notes[i].lane == 8 && !notes[i].isBGM)
                scratchIdx.push_back(i);
        }

        size_t n = scratchIdx.size();
        size_t k = 0;
        while (k < n) {
            // LNでなければスキップ
            if (!notes[scratchIdx[k]].isLN) { ++k; continue; }

            // k から始まるLN連鎖を収集（gap <= 33 で次もLN）
            std::vector<size_t> chain;
            chain.push_back(k);
            size_t j = k + 1;
            while (j < n && notes[scratchIdx[j]].isLN) {
                PlayableNote& prev = notes[scratchIdx[j - 1]];
                PlayableNote& cur  = notes[scratchIdx[j]];
                int64_t gap = cur.y - (prev.y + prev.l);
                if (gap <= 33) { // gap=32 ± 誤差
                    chain.push_back(j);
                    ++j;
                } else {
                    break;
                }
            }

            // 連鎖末尾の次が単発(gap=0)ならBSSチェーン確定
            bool isBSS = false;
            if (chain.size() >= 2 && j < n) {
                PlayableNote& lastLN   = notes[scratchIdx[chain.back()]];
                PlayableNote& nextNote = notes[scratchIdx[j]];
                int64_t gap = nextNote.y - (lastLN.y + lastLN.l);
                if (!nextNote.isLN && std::abs(gap) <= 1) {
                    isBSS = true;
                }
            }

            if (isBSS) {
                notes[scratchIdx[chain.front()]].isBSSHead = true;
                for (size_t m = 1; m + 1 < chain.size(); ++m)
                    notes[scratchIdx[chain[m]]].isBSSMid = true;
                notes[scratchIdx[chain.back()]].isBSSTail = true;
                // 各LN(Head/Mid)に次LNのy座標を設定（middle中心位置の計算用）
                for (size_t m = 0; m + 1 < chain.size(); ++m)
                    notes[scratchIdx[chain[m]]].bssNextY = notes[scratchIdx[chain[m + 1]]].y;
                k = j + 1;
            } else {
                k = chain.back() + 1;
            }
        }
    }
    // ─────────────────────────────────────────────────────────────────────

    status.remainingNotes = status.totalNotes;
    for (const auto& l : data.lines) beatLines.push_back({projector.getMsFromY(l.y), l.y});

    baseRecoveryPerNote = (double)calculateHSRecoveryInternal(status.totalNotes);

    if (Config::GAUGE_OPTION == 5) status.gauge = (double)Config::DAN_GAUGE_START_PERCENT;
    else if (Config::GAUGE_OPTION >= 3) status.gauge = 100.0;
    else status.gauge = 22.0;

    status.isFailed  = false;
    status.isDead    = false;
    status.clearType = ClearType::NO_PLAY;

    nextUpdateIndex = 0;
    for (int i = 0; i <= 8; i++) lastSoundPerLaneId[i] = 0;

    // ★修正②: レーン別インデックスを構築。processHit で全ノーツを O(N) スキャンする代わりに
    //          そのレーンのノーツだけを O(M) で走査できるようにする。
    //          notes は target_ms 昇順にソート済みなので、各レーン内でも昇順が保たれる。
    for (int lane = 1; lane <= 8; ++lane) {
        laneNoteIndices[lane].clear();
        // ★修正(MINOR-2): レーン別インデックスを事前確保。
        // 8レーンに均等分散と仮定し、ノーツ総数/7 + 余裕分を確保する。
        // push_back 時の再アロケーション（と memcpy）を防ぐ。
        laneNoteIndices[lane].reserve(notes.size() / 7 + 16);
        laneSearchStart[lane] = 0;
        laneHiddenNotes[lane].clear();
        hiddenSearchStart[lane] = 0;
        for (size_t i = 0; i < notes.size(); ++i) {
            if (!notes[i].isBGM && notes[i].lane == lane) {
                laneNoteIndices[lane].push_back(i);
            }
        }
    }

    // ★修正：gaugeHistory を事前確保して push_back 時の再アロケーションを防ぐ
    status.gaugeHistory.clear();
    status.gaugeHistory.reserve(2000); // 最大曲長(~6分) x 200ms間隔 = 1800サンプル程度
    lastHistoryUpdateMs = -1000.0;

    // MIN/MAX BPM 計算
    minBpm_ = data.header.bpm;
    maxBpm_ = data.header.bpm;
    for (const auto& ev : data.bpm_events) {
        if (ev.bpm < minBpm_) minBpm_ = ev.bpm;
        if (ev.bpm > maxBpm_) maxBpm_ = ev.bpm;
    }
    bpmVaries_ = !data.bpm_events.empty();
}

void PlayEngine::update(double cur_ms, uint32_t now) {
    SoundManager& snd = SoundManager::getInstance();
    if (status.isFailed) return;

    if (cur_ms >= 0 && cur_ms - lastHistoryUpdateMs >= 200.0) {
        status.gaugeHistory.push_back((float)status.gauge);
        lastHistoryUpdateMs = cur_ms;
    }

    for (size_t i = nextUpdateIndex; i < notes.size(); ++i) {
        auto& n = notes[i];

        if (n.played) {
            if (i == nextUpdateIndex) nextUpdateIndex++;
            continue;
        }

        if (n.target_ms > cur_ms + 500.0) break;

        if (!n.isBGM) {
            lastSoundPerLaneId[n.lane] = n.soundId;
        }

        if (n.isBGM && n.target_ms <= cur_ms) {
            if (!skipBGM) snd.play(n.soundId);
            n.played = true;
            if (i == nextUpdateIndex) nextUpdateIndex++;
        }
        else if (!n.isBGM) {
            double adjusted_target = n.target_ms + Config::JUDGE_OFFSET;
            double end_ms = adjusted_target + (n.isLN ? n.duration_ms : 0);

            // ★HCNモード: ティック処理（押中→ゲージ回復、離中→POOR）
            // ティック間隔 = 16分音符1個分 = 60000 / (BPM × 4) ms
            if (Config::LN_OPTION == 1 && n.isLN && n.hcnLastTickMs >= 0.0
                && !n.played
                && cur_ms < end_ms + Config::JUDGE_POOR) {
                double bpm = projector.getBpmFromMs(cur_ms);
                double tickInterval = (bpm > 0.0) ? (60000.0 / (bpm * 4.0)) : 125.0;

                // ★ケースB対策: 離し中かつ次ティック前にPGREAT窓に入ったらPOOR1回出して強制終了
                if (!n.isBeingPressed && cur_ms >= end_ms - Config::JUDGE_PGREAT) {
                    n.played        = true;
                    n.hcnLastTickMs = -1.0;
                    status.remainingNotes--;
                    status.poorCount++;
                    status.combo = 0;
                    currentJudge.kind      = JudgeKind::POOR;
                    currentJudge.startTime = now;
                    currentJudge.active    = true;
                    currentJudge.isFast    = false;
                    currentJudge.isSlow    = false;
                    judgeManager.updateGauge(status, 0, false, baseRecoveryPerNote);
                    if (i == nextUpdateIndex) nextUpdateIndex++;
                    if (status.isFailed) { snd.stopAll(); break; }
                    continue;
                }

                if (cur_ms - n.hcnLastTickMs >= tickInterval) {
                    n.hcnLastTickMs = cur_ms; // 常にcur_msにリセット（溜まったティックの連鎖を防ぐ）
                    if (n.isBeingPressed) {
                        // 押中: ゲージ回復のみ（スコア・コンボ・判定表示なし）
                        judgeManager.updateGauge(status, 3, true, baseRecoveryPerNote);
                    } else {
                        // 離中: POOR・コンボ切れ・ゲージ削れ（1フレーム1POOR上限）
                        status.poorCount++;
                        status.combo = 0;
                        currentJudge.kind      = JudgeKind::POOR;
                        currentJudge.startTime = now;
                        currentJudge.active    = true;
                        currentJudge.isFast    = false;
                        currentJudge.isSlow    = false;
                        judgeManager.updateGauge(status, 0, false, baseRecoveryPerNote);
                        if (status.isFailed) { snd.stopAll(); break; }
                    }
                }
            }

            // ★LNモード: 押下中LNがPGREAT窓に入った瞬間にP-GREAT確定。
            // played=true になるのでprocessReleaseには来ない（空押し状態）。
            if (Config::LN_OPTION == 2
                && n.isLN && n.isBeingPressed
                && cur_ms >= end_ms - Config::JUDGE_PGREAT) {
                n.isBeingPressed = false;
                n.played         = true;
                status.remainingNotes--;
                status.pGreatCount++;
                status.combo++;
                status.exScore += 2;

                currentJudge.kind      = JudgeKind::PGREAT;
                currentJudge.startTime = now;
                currentJudge.active    = true;
                currentJudge.isFast    = false;
                currentJudge.isSlow    = false;
                currentJudge.diffMs    = 0.0;

                if (status.combo > status.maxCombo) status.maxCombo = status.combo;
                judgeManager.updateGauge(status, 3, true, baseRecoveryPerNote);

                if (i == nextUpdateIndex) nextUpdateIndex++;
                continue;
            }

            // 通常のPOOR処理（JUDGE_POOR窓を超えたノーツ）
            if (cur_ms > end_ms + Config::JUDGE_POOR) {
                n.played         = true;
                n.isBeingPressed = false;
                n.hcnLastTickMs  = -1.0;
                status.remainingNotes--;
                status.poorCount++;
                status.combo = 0;

                currentJudge.kind      = JudgeKind::POOR;
                currentJudge.startTime = now;
                currentJudge.active    = true;
                currentJudge.isFast    = false;
                currentJudge.isSlow    = false;

                judgeManager.updateGauge(status, 0, false, baseRecoveryPerNote);

                if (i == nextUpdateIndex) nextUpdateIndex++;

                if (status.isFailed) {
                    snd.stopAll();
                    break;
                }
            }
        }
    }

    if (cur_ms > status.maxTargetMs + 1000.0) {
        if (!status.isFailed) {
            int opt = Config::GAUGE_OPTION;
            if (Config::ASSIST_OPTION > 0) {
                status.clearType = ClearType::ASSIST_CLEAR;
            }
            else if (opt >= 3) {
                if (status.badCount == 0 && status.poorCount == 0) status.clearType = ClearType::FULL_COMBO;
                else if (opt == 3) status.clearType = ClearType::HARD_CLEAR;
                else if (opt == 4) status.clearType = ClearType::EX_HARD_CLEAR;
                else if (opt == 5) status.clearType = ClearType::DAN_CLEAR;
                else if (opt == 6) status.clearType = ClearType::FULL_COMBO;
            }
            else {
                double border = (opt == 1) ? 60.0 : 80.0;
                if (status.gauge >= border) {
                    if (status.badCount == 0 && status.poorCount == 0) status.clearType = ClearType::FULL_COMBO;
                    else if (opt == 1) status.clearType = ClearType::ASSIST_CLEAR;
                    else if (opt == 2) status.clearType = ClearType::EASY_CLEAR;
                    else               status.clearType = ClearType::NORMAL_CLEAR;
                } else {
                    status.isFailed  = true;
                    status.clearType = ClearType::FAILED;
                }
            }
        }
    }
}

int PlayEngine::processHit(int lane, double cur_ms, uint32_t now, bool isAuto) {
    SoundManager& snd = SoundManager::getInstance();
    if (status.isFailed || lane < 1 || lane > 8) return 0;

    bool hitSuccess = false;
    int  finalJudge = 0;

    const auto& indices = laneNoteIndices[lane];
    size_t& startIdx    = laneSearchStart[lane];

    while (startIdx < indices.size()
           && notes[indices[startIdx]].played
           && !notes[indices[startIdx]].isBeingPressed) {
        startIdx++;
    }

    for (size_t k = startIdx; k < indices.size(); ++k) {
        auto& n = notes[indices[k]];
        if (n.played && !n.isBeingPressed) { startIdx = k + 1; continue; }
        if (n.isLN && n.isBeingPressed) continue;

        // ★HCNモード: played==false かつ LN かつ未押下 → 判定窓外でも押下開始
        // 途中からいつでも押せる。終端POOR窓内ならティック開始。
        if (Config::LN_OPTION == 1 && n.isLN && !n.played && !n.isBeingPressed) {
            double adjusted_end = (n.target_ms + n.duration_ms) + Config::JUDGE_OFFSET;
            if (cur_ms <= adjusted_end + Config::JUDGE_POOR
                && cur_ms >= n.target_ms - Config::JUDGE_BAD) {
                n.isBeingPressed = true;
                n.hcnLastTickMs  = cur_ms;
                snd.play(n.soundId);
                lastSoundPerLaneId[lane] = n.soundId;
                // 頭ノートの判定窓内なら頭ノート判定も行う
                double adjusted_target = n.target_ms + Config::JUDGE_OFFSET;
                double raw_diff = cur_ms - adjusted_target;
                double diff     = std::abs(raw_diff);
                int hcnJudgeResult = 3; // 頭判定なし（途中押し）はPGREAT相当でボム演出
                if (diff <= Config::JUDGE_BAD && !isAuto) {
                    int judgeType = 0;
                    bool isFast = (raw_diff < 0), isSlow = (raw_diff > 0);
                    if      (diff <= Config::JUDGE_PGREAT) { judgeType = 3; isFast = isSlow = false; status.pGreatCount++; status.combo++; status.exScore += 2; }
                    else if (diff <= Config::JUDGE_GREAT)  { judgeType = 2; status.greatCount++; status.combo++; status.exScore += 1; if (isFast) status.fastCount++; else status.slowCount++; }
                    else if (diff <= Config::JUDGE_GOOD)   { judgeType = 1; status.goodCount++; status.combo++; if (isFast) status.fastCount++; else status.slowCount++; }
                    else                                   { judgeType = 0; status.badCount++; status.combo = 0; isFast = isSlow = false; }
                    auto uiData = judgeManager.getJudgeUIData(judgeType);
                    currentJudge.kind      = uiData.kind;
                    currentJudge.startTime = now;
                    currentJudge.active    = true;
                    currentJudge.isFast    = isFast;
                    currentJudge.isSlow    = isSlow;
                    currentJudge.diffMs    = raw_diff;
                    if (status.combo > status.maxCombo) status.maxCombo = status.combo;
                    judgeManager.updateGauge(status, judgeType, true, baseRecoveryPerNote);
                    hcnJudgeResult = judgeType;
                }
                return hcnJudgeResult;
            }
            continue;
        }

        double adjusted_target = n.target_ms + Config::JUDGE_OFFSET;

        if (adjusted_target > cur_ms + Config::JUDGE_BAD) break;

        double raw_diff = cur_ms - adjusted_target;
        double diff     = std::abs(raw_diff);

        if (diff > Config::JUDGE_BAD) continue;

        snd.play(n.soundId);
        lastSoundPerLaneId[lane] = n.soundId;

        {
            auto& hidden = laneHiddenNotes[lane];
            size_t& hStart = hiddenSearchStart[lane];
            while (hStart < hidden.size() && hidden[hStart].played) hStart++;
            if (hStart < hidden.size()) {
                PlayableNote& hn = hidden[hStart];
                double hDiff = std::abs(cur_ms - hn.target_ms);
                if (hDiff <= Config::JUDGE_BAD) {
                    snd.play(hn.soundId);
                    lastSoundPerLaneId[lane] = hn.soundId;
                    hn.played = true;
                    hStart++;
                }
            }
        }

        if (n.isLN) {
            n.isBeingPressed = true;
            // ★HCNモード: 頭ノートヒット時にティック基準時刻をセット
            if (Config::LN_OPTION == 1) {
                n.hcnLastTickMs = cur_ms;
            }
        } else {
            n.played = true;
            status.remainingNotes--;
        }

        hitSuccess = true;

        int  judgeType = 0;
        bool isFast    = (raw_diff < 0);
        bool isSlow    = (raw_diff > 0);

        if (diff <= Config::JUDGE_PGREAT) {
            judgeType = 3;
            isFast = false; isSlow = false;
            if (!isAuto) { status.pGreatCount++; status.combo++; status.exScore += 2; }
            else if (Config::ASSIST_OPTION == 7) { status.combo++; }
        } else if (diff <= Config::JUDGE_GREAT) {
            judgeType = 2;
            if (!isAuto) {
                status.greatCount++; status.combo++; status.exScore += 1;
                if (isFast) status.fastCount++; else status.slowCount++;
            }
            else if (Config::ASSIST_OPTION == 7) { status.combo++; }
        } else if (diff <= Config::JUDGE_GOOD) {
            judgeType = 1;
            if (!isAuto) {
                status.goodCount++; status.combo++;
                if (isFast) status.fastCount++; else status.slowCount++;
            }
            else if (Config::ASSIST_OPTION == 7) { status.combo++; }
        } else {
            judgeType = 0;
            if (!isAuto) {
                status.badCount++;
                status.combo = 0;
            }
            // AUTO PLAYでBADは発生しないため、else if不要
            if (n.isLN) {
                n.isBeingPressed = false;
                n.played         = true;
                n.hcnLastTickMs  = -1.0;
                status.remainingNotes--;
            }
            isFast = false; isSlow = false;
        }

        finalJudge = judgeType;

        // ★修正: isAuto レーンは判定表示・ゲージ更新をスキップ
        if (!isAuto) {
            auto uiData = judgeManager.getJudgeUIData(judgeType);
            currentJudge.kind      = uiData.kind;
            currentJudge.startTime = now;
            currentJudge.active    = true;
            currentJudge.isFast    = isFast;
            currentJudge.isSlow    = isSlow;
            currentJudge.diffMs    = raw_diff;
            if (status.combo > status.maxCombo) status.maxCombo = status.combo;
            judgeManager.updateGauge(status, judgeType, true, baseRecoveryPerNote);
        } else if (Config::ASSIST_OPTION == 7) {
            // AUTO PLAY: コンボ加算済みのためmaxComboのみ更新
            if (status.combo > status.maxCombo) status.maxCombo = status.combo;
        }

        if (status.isFailed) snd.stopAll();
        break;
    }

    if (!hitSuccess) {
        if (lane >= 1 && lane <= 8 && lastSoundPerLaneId[lane] != 0) {
            snd.play(lastSoundPerLaneId[lane]);
            if (Config::GAUGE_OPTION == 6) {
                status.gauge     = 0.0;
                status.isFailed  = true;
                status.isDead    = true;
                snd.stopAll();
            }
        }
    }

    return finalJudge;
}

void PlayEngine::processRelease(int lane, double cur_ms, uint32_t now) {
    if (status.isFailed || lane < 1 || lane > 8) return;

    const auto& indices = laneNoteIndices[lane];
    size_t& startIdx    = laneSearchStart[lane];

    while (startIdx < indices.size()
           && notes[indices[startIdx]].played
           && !notes[indices[startIdx]].isBeingPressed) {
        startIdx++;
    }

    for (size_t k = startIdx; k < indices.size(); ++k) {
        auto& n = notes[indices[k]];
        if (n.played && !n.isBeingPressed) { startIdx = k + 1; continue; }

        // isBeingPressed の LN のみが対象。
        // LNモードでは update() でP-GREAT確定済みなので played=true になっており
        // ここには来ない。空押しは自然に無視される。
        if (!n.isLN || !n.isBeingPressed) continue;

        // ★HCNモード:
        // 終端 ±PGREAT窓内で離す   → PGREAT + played=true
        // 終端より早い離し          → isBeingPressed=falseのみ、ティック継続でPOOR
        // 終端+PGREAT窓〜+BAD窓    → BAD  + played=true  (CNと同じ)
        // 終端+BAD窓以降           → POOR + played=true  (CNと同じ)
        if (Config::LN_OPTION == 1) {
            double adjusted_end_hcn = (n.target_ms + n.duration_ms) + Config::JUDGE_OFFSET;
            double raw_diff_hcn     = cur_ms - adjusted_end_hcn;

            if (raw_diff_hcn >= -Config::JUDGE_PGREAT) {
                // 終端PGREAT窓以降で離した → played=trueにしてティック停止
                n.isBeingPressed = false;
                n.played         = true;
                n.hcnLastTickMs  = -1.0;
                status.remainingNotes--;

                double diff_hcn = std::abs(raw_diff_hcn);
                int judgeType;
                if (diff_hcn <= Config::JUDGE_PGREAT) {
                    // ±PGREAT窓 → PGREAT
                    status.pGreatCount++; status.combo++; judgeType = 3;
                    status.exScore += 2;
                    currentJudge.isFast = currentJudge.isSlow = false;
                } else if (diff_hcn <= Config::JUDGE_GREAT) {
                    // +PGREAT〜+GREAT → GREAT (遅離し)
                    status.greatCount++; status.combo++; judgeType = 2;
                    status.exScore += 1;
                    currentJudge.isFast = false; currentJudge.isSlow = true;
                } else if (diff_hcn <= Config::JUDGE_GOOD) {
                    // +GREAT〜+GOOD → GOOD (遅離し)
                    status.goodCount++; status.combo++; judgeType = 1;
                    currentJudge.isFast = false; currentJudge.isSlow = true;
                } else if (diff_hcn <= Config::JUDGE_BAD) {
                    // +GOOD〜+BAD → BAD (遅離し)
                    status.badCount++; status.combo = 0; judgeType = 0;
                    currentJudge.isFast = false; currentJudge.isSlow = true;
                } else {
                    // +BAD以降 → POOR
                    status.poorCount++; status.combo = 0; judgeType = -1;
                    currentJudge.isFast = currentJudge.isSlow = false;
                }

                if (judgeType >= 0) {
                    auto uiData = judgeManager.getJudgeUIData(judgeType);
                    currentJudge.kind      = uiData.kind;
                    currentJudge.startTime = now;
                    currentJudge.active    = true;
                    currentJudge.diffMs    = raw_diff_hcn;
                    if (status.combo > status.maxCombo) status.maxCombo = status.combo;
                    judgeManager.updateGauge(status, judgeType, true, baseRecoveryPerNote);
                } else {
                    currentJudge.kind      = JudgeKind::POOR;
                    currentJudge.startTime = now;
                    currentJudge.active    = true;
                    currentJudge.diffMs    = raw_diff_hcn;
                    judgeManager.updateGauge(status, 0, false, baseRecoveryPerNote);
                }
            } else {
                // 終端PGREAT窓より早い離し → isBeingPressed=falseのみ、ティック継続
                n.isBeingPressed = false;
            }
            break;
        }

        double adjusted_end = (n.target_ms + n.duration_ms) + Config::JUDGE_OFFSET;
        double raw_diff     = cur_ms - adjusted_end;  // 正=遅い, 負=早い
        double diff         = std::abs(raw_diff);

        // ★CN/LN共通: 終端より JUDGE_GOOD 以上早く離した → 強制POOR（ゲージ削れ）
        if (raw_diff < -Config::JUDGE_GOOD) {
            n.isBeingPressed = false;
            n.played         = true;
            status.remainingNotes--;
            status.poorCount++;
            status.combo = 0;

            currentJudge.kind      = JudgeKind::POOR;
            currentJudge.startTime = now;
            currentJudge.active    = true;
            currentJudge.isFast    = false;
            currentJudge.isSlow    = false;

            judgeManager.updateGauge(status, 0, false, baseRecoveryPerNote);
            break;
        }

        // 判定窓内での離し: タイミング判定
        if (diff <= Config::JUDGE_BAD) {
            n.isBeingPressed = false;
            n.played         = true;
            status.remainingNotes--;

            int  judgeType = 0;
            bool isFast    = (raw_diff < 0);
            bool isSlow    = (raw_diff > 0);

            if (diff <= Config::JUDGE_PGREAT) {
                status.pGreatCount++; status.combo++; judgeType = 3;
                status.exScore += 2;
                isFast = false; isSlow = false;
            } else if (diff <= Config::JUDGE_GREAT) {
                status.greatCount++; status.combo++; judgeType = 2;
                status.exScore += 1;
                if (isFast) status.fastCount++; else status.slowCount++;
            } else if (diff <= Config::JUDGE_GOOD) {
                status.goodCount++; status.combo++; judgeType = 1;
                if (isFast) status.fastCount++; else status.slowCount++;
            } else {
                status.badCount++; status.combo = 0; judgeType = 0;
                isFast = false; isSlow = false;
            }

            auto uiData = judgeManager.getJudgeUIData(judgeType);
            currentJudge.kind      = uiData.kind;
            currentJudge.startTime = now;
            currentJudge.active    = true;
            currentJudge.isFast    = isFast;
            currentJudge.isSlow    = isSlow;
            currentJudge.diffMs    = raw_diff;

            if (status.combo > status.maxCombo) status.maxCombo = status.combo;
            judgeManager.updateGauge(status, judgeType, true, baseRecoveryPerNote);
        } else {
            // 判定窓外（遅すぎ離し）→ POOR
            n.isBeingPressed = false;
            n.played         = true;
            status.remainingNotes--;
            status.poorCount++;
            status.combo = 0;

            currentJudge.kind      = JudgeKind::POOR;
            currentJudge.startTime = now;
            currentJudge.active    = true;
            currentJudge.isFast    = false;
            currentJudge.isSlow    = false;

            judgeManager.updateGauge(status, 0, false, baseRecoveryPerNote);
        }
        break;
    }
}

double PlayEngine::getMsFromY(int64_t target_y) const { return projector.getMsFromY(target_y); }
int64_t PlayEngine::getYFromMs(double cur_ms) const   { return projector.getYFromMs(cur_ms); }
double PlayEngine::getBpmFromMs(double cur_ms) const  { return projector.getBpmFromMs(cur_ms); }

void PlayEngine::forceFail() {
    status.isFailed  = true;
    status.isDead    = true;
    status.gauge     = 0.0;
    status.clearType = ClearType::FAILED;
}


