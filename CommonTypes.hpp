#ifndef COMMONTYPES_HPP
#define COMMONTYPES_HPP

#include <string>
#include <vector>
#include <cstdint>
#include <SDL2/SDL.h>

// ============================================================
//  クリアタイプ
// ============================================================
enum class ClearType {
    NO_PLAY = 0,
    FAILED,
    ASSIST_CLEAR,
    EASY_CLEAR,
    NORMAL_CLEAR,
    HARD_CLEAR,
    EX_HARD_CLEAR,
    DAN_CLEAR,
    FULL_COMBO
};

// ============================================================
//  ★修正：判定表示用
//  std::string text を廃止し enum JudgeKind に置き換える。
//  判定が出るたびに string の heap alloc が発生していた問題を根絶。
//  文字列・色は描画側の定数テーブルで引く。
// ============================================================
enum class JudgeKind : uint8_t {
    NONE = 0,
    POOR,    // judgeType -1
    BAD,     // judgeType  0
    GOOD,    // judgeType  1
    GREAT,   // judgeType  2
    PGREAT   // judgeType  3
};

// judgeType (int) → JudgeKind の変換ヘルパー
inline JudgeKind judgeTypeToKind(int t) {
    switch (t) {
        case  3: return JudgeKind::PGREAT;
        case  2: return JudgeKind::GREAT;
        case  1: return JudgeKind::GOOD;
        case  0: return JudgeKind::BAD;
        default: return JudgeKind::POOR;
    }
}

// 描画時に使う定数テーブル（heap alloc ゼロ）
inline const char* judgeKindToText(JudgeKind k) {
    switch (k) {
        case JudgeKind::PGREAT: return "P-GREAT";
        case JudgeKind::GREAT:  return "GREAT";
        case JudgeKind::GOOD:   return "GOOD";
        case JudgeKind::BAD:    return "BAD";
        case JudgeKind::POOR:   return "POOR";
        default:                return "";
    }
}

inline SDL_Color judgeKindToColor(JudgeKind k) {
    switch (k) {
        case JudgeKind::PGREAT: return {255, 255, 255, 255};
        case JudgeKind::GREAT:  return {255, 255,   0, 255};
        case JudgeKind::GOOD:   return {255, 255,   0, 255};
        case JudgeKind::BAD:    return {255, 128,   0, 255};
        case JudgeKind::POOR:   return {255, 128,   0, 255};
        default:                return {255, 255, 255, 255};
    }
}

struct JudgmentDisplay {
    JudgeKind kind    = JudgeKind::NONE;
    uint32_t  startTime = 0;
    bool      active  = false;
    bool      isFast  = false;
    bool      isSlow  = false;
    double    diffMs  = 0.0; // pgreatからの誤差ms (DETAIL表示用)

    // 後方互換ヘルパー：描画側でテキストが必要なときに呼ぶ
    const char*  text()  const { return judgeKindToText(kind); }
    SDL_Color    color() const { return judgeKindToColor(kind); }
};

// ============================================================
//  ノーツ情報
// ============================================================
struct PlayableNote {
    double   target_ms  = 0.0;
    int64_t  y          = 0;
    int      lane       = 0;
    uint32_t soundId    = 0;    // string を廃止し数値IDで管理

    bool played         = false;
    bool isBGM          = false;

    // ロングノーツ用
    bool   isLN          = false;
    int64_t l            = 0;
    double  duration_ms  = 0.0;
    bool    isBeingPressed = false;
    bool    end_processed  = false;

    // BSS (Back Spin Scratch): スクラッチLNの終点と次ノーツ始点が重なるケース
    bool    isBSS        = false; // このノーツがBSSペアに属する（終点LN側・始点側両方にセット）
};

// ============================================================
//  小節線
// ============================================================
struct PlayableLine {
    double  target_ms = 0.0;
    int64_t y         = 0;
};

// ============================================================
//  判定エフェクト
// ============================================================
struct ActiveEffect {
    int      lane;
    uint32_t startTime;
};

// ============================================================
//  プレイ状況
//  gaugeHistory は最大サンプル数を reserve するだけで十分。
//  配列サイズが固定なら std::array も検討できるが、
//  曲の長さで変わるため vector + reserve を維持する。
// ============================================================
struct PlayStatus {
    int pGreatCount  = 0;
    int greatCount   = 0;
    int goodCount    = 0;
    int badCount     = 0;
    int poorCount    = 0;

    int fastCount    = 0;
    int slowCount    = 0;

    int combo        = 0;
    int maxCombo     = 0;
    int totalNotes   = 0;
    int remainingNotes = 0;
    double currentBpm  = 0.0;
    double maxTargetMs = 0.0;
    double gauge       = 22.0;
    bool   isFailed    = false;
    int    exScore     = 0;

    ClearType clearType = ClearType::NO_PLAY;
    bool isDead = false;

    // ★修正：初期化時に reserve して再アロケーションを防ぐ（PlayEngine::init で実施）
    std::vector<float> gaugeHistory;
};

// ============================================================
//  ベストスコア記録用
// ============================================================
struct BestScore {
    int pGreat   = 0;
    int great    = 0;
    int good     = 0;
    int bad      = 0;
    int poor     = 0;

    int fastCount = 0;
    int slowCount = 0;

    int maxCombo  = 0;
    int exScore   = 0;
    std::string rank = "F";
    ClearType clearType = ClearType::NO_PLAY;
    bool isClear = false;

    std::vector<float> gaugeHistory;
};

// ============================================================
//  動画フレーム
// ============================================================
struct VideoFrame {
    double   pts     = -1.0;
    size_t   slotIdx = SIZE_MAX; // ★修正: データレース対策用スロット番号
    uint8_t* yPtr    = nullptr;  // memoryPool 内の固定位置を指すポインタ

    int yStride = 0;
    int uStride = 0;
    int vStride = 0;
};

#endif // COMMONTYPES_HPP









