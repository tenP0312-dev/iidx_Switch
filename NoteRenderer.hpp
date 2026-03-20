#ifndef NOTERENDERER_HPP
#define NOTERENDERER_HPP

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <string>
#include <unordered_map>
#include <list>
#include <vector>
#include <map>
#include "BmsonLoader.hpp"
#include "CommonTypes.hpp"

// ボムアニメーション管理用 (ScenePlayとNoteRenderer間で共有)
struct BombAnim {
    int      lane;
    uint32_t startTime;
    int      judgeType; // 0:なし, 1:P-GREAT, 2:GREAT
};

/**
 * @brief テクスチャとサイズ情報をペアで管理し、SDL_QueryTextureを不要にする
 */
struct TextureRegion {
    SDL_Texture* texture = nullptr;
    int w = 0;
    int h = 0;

    explicit operator bool() const { return texture != nullptr; }

    void reset() {
        if (texture) SDL_DestroyTexture(texture);
        texture = nullptr;
        w = h = 0;
    }
};

// --- キャッシュキーとハッシュ関数 ---
struct TextCacheKey {
    std::string text;
    uint32_t    color_rgba;
    bool        isBig;
    std::string fontPath;

    bool operator==(const TextCacheKey& other) const {
        return color_rgba == other.color_rgba
            && isBig     == other.isBig
            && text      == other.text
            && fontPath  == other.fontPath;
    }
};

struct TextCacheKeyHash {
    std::size_t operator()(const TextCacheKey& k) const {
        // ★修正：fontPath をハッシュに含める。
        // 以前は operator== で fontPath を比較しているのにハッシュが fontPath を無視していた。
        // これはハッシュマップの契約違反（同一キーは同一ハッシュを返さなければならない）であり、
        // 同フォント以外のエントリが同じバケツに入って衝突が増大していた。
        std::size_t h = std::hash<std::string>{}(k.text);
        h ^= std::hash<uint32_t>{}(k.color_rgba) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<bool>{}(k.isBig)          + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<std::string>{}(k.fontPath) + 0x9e3779b9 + (h << 6) + (h >> 2);
        return h;
    }
};

class NoteRenderer {
public:
    void init(SDL_Renderer* ren);
    void cleanup();
    void clearTextCache();

    // drawText     : 毎フレーム変化するテキスト専用（ローディング・デバッグ表示等）
    // drawTextCached: 変化が少ないテキスト専用（ゲームループ内はこちらを使うこと）
    void drawText(SDL_Renderer* ren, const std::string& text, int x, int y,
                  SDL_Color color, bool isBig,
                  bool isCenter = false, bool isRight = false,
                  const std::string& fontPath = "");

    void drawTextCached(SDL_Renderer* ren, const std::string& text, int x, int y,
                        SDL_Color color, bool isBig,
                        bool isCenter = false, bool isRight = false,
                        const std::string& fontPath = "");

    void drawImage(SDL_Renderer* ren, const std::string& path,
                   int x, int y, int w, int h, int alpha = 255);

    void renderNote(SDL_Renderer* ren, const PlayableNote& note,
                  int64_t cur_y, double pixels_per_y, bool isAuto = false);

    void renderBackground(SDL_Renderer* ren);
    void renderLanes(SDL_Renderer* ren, double progress, int scratchStatus = 0);
    void renderSuddenLift(SDL_Renderer* ren); // SUDDEN_PLUS/LIFTオーバーレイ（ノーツ描画後に呼ぶ）
    void renderBeatLine(SDL_Renderer* ren, double diff_y, double pixels_per_y);
    void renderHitEffect(SDL_Renderer* ren, int lane, float progress);
    void renderBomb(SDL_Renderer* ren, int lane, int frame);

    // ★修正：renderJudgment は JudgeKind ベースのオーバーロードを追加し、
    //        文字列比較ループをなくす
    void renderJudgment(SDL_Renderer* ren, JudgeKind kind,
                        float progress, int combo = 0);
    // 後方互換用（内部で kind に変換して上のオーバーロードを呼ぶ）
    void renderJudgment(SDL_Renderer* ren, const std::string& text,
                        float progress, SDL_Color color, int combo = 0);

    void renderCombo(SDL_Renderer* ren, int combo);
    void renderEffects(SDL_Renderer* ren,
                       ActiveEffect* buf, size_t& count,
                       const bool* lanePressed, uint32_t now);
    void renderBombs(SDL_Renderer* ren,
                     BombAnim* buf, size_t& count,
                     uint32_t now);
    void renderFastSlow(SDL_Renderer* ren, bool isFast, bool isSlow, float progress, double diffMs = 0.0);

    void renderGauge(SDL_Renderer* ren, double gaugeValue,
                     int gaugeOption, bool isFailed);
    void renderUI(SDL_Renderer* ren, const BMSHeader& header,
                  int fps, double bpm, int exScore);
    void renderDecisionInfo(SDL_Renderer* ren, const BMSHeader& header);
    void renderLoading(SDL_Renderer* ren, int current, int total,
                       const std::string& filename);
    void renderResult(SDL_Renderer* ren, const PlayStatus& status,
                      const BMSHeader& header, const std::string& rank);

    // ★修正⑥: ScenePlay::renderScene 等で重複計算されていたレーン座標を
    //           ll キャッシュ経由で公開。rebuildLaneLayout() と二重管理になるバグを防ぐ。
    int getLaneBaseX()      const { return ll.baseX; }
    int getLaneTotalWidth() const { return ll.totalWidth; }
    int getLaneCenterX()    const { return ll.baseX + ll.totalWidth / 2; }
    int getBgaCenterX()     const { return ll.bgaCenterX; }
    int getBgaCenterY()     const { return ll.bgaCenterY; }

    // オプション画面でレーン幅・スクラッチ幅が変更されたときに呼ぶ。
    // init() 以外でレイアウトを更新できる唯一の窓口。
    void notifyLayoutChanged() { rebuildLaneLayout(); }
    // ★2P VS: 両サイドのレイアウトを事前計算
    void rebuildBothLayouts();
    // ★2P VS: 描画中にサイドを切り替え
    void switchSide(int side);

private:
    TTF_Font* fontSmall = nullptr;
    TTF_Font* fontBig   = nullptr;

    struct CacheEntry {
        SDL_Texture* texture;
        int w, h;
        std::list<TextCacheKey>::iterator lruIt;
    };
    std::unordered_map<TextCacheKey, CacheEntry, TextCacheKeyHash> textTextureCache;
    std::list<TextCacheKey> lruList;
    static constexpr size_t MAX_TEXT_CACHE = 256;

    std::map<std::string, TextureRegion> textureCache;
    std::map<std::string, TTF_Font*>     customFontCache;

    TextureRegion texBackground;
    TextureRegion texNoteWhite, texNoteBlue, texNoteRed;
    TextureRegion texNoteWhite_LN, texNoteWhite_LN_Active1, texNoteWhite_LN_Active2;
    TextureRegion texNoteBlue_LN,  texNoteBlue_LN_Active1,  texNoteBlue_LN_Active2;
    TextureRegion texNoteRed_LN,   texNoteRed_LN_Active1,   texNoteRed_LN_Active2;
    TextureRegion texNoteWhite_LNS, texNoteWhite_LNE;
    TextureRegion texNoteBlue_LNS,  texNoteBlue_LNE;
    TextureRegion texNoteRed_LNS,   texNoteRed_LNE;

    // BSS (Back Spin Scratch) 専用テクスチャ
    TextureRegion texNoteRed_BSS_S, texNoteRed_BSS_Mid, texNoteRed_BSS_E;
    TextureRegion texKeybeamWhite, texKeybeamBlue, texKeybeamRed;
    // 判定表示テクスチャ (各ファイル独立 → 画像ごとに実サイズでセンタリング)
    TextureRegion texJudgePGreatBlue;   // judge_p_great_blue.png  (P-GREAT 点滅コマ1)
    TextureRegion texJudgePGreatPink;   // judge_p_great_pink.png  (P-GREAT 点滅コマ2)
    TextureRegion texJudgePGreatWhite;  // judge_p_great_white.png (P-GREAT 点滅コマ3)
    TextureRegion texJudgeGreat;        // judge_great_yellow.png  (GREAT)
    TextureRegion texJudgeGood;         // judge_good.png          (GOOD)
    TextureRegion texJudgeBad;          // judge_bad.png           (BAD)
    TextureRegion texJudgePoor;         // judge_poor.png          (POOR)
    TextureRegion texNumberAtlas;
    std::vector<TextureRegion> texBombs;
    TextureRegion texLaneCover;
    TextureRegion texGaugeAssist, texGaugeNormal, texGaugeHard,
                  texGaugeExHard, texGaugeHazard, texGaugeDan;
    TextureRegion texGaugeNumber;        // gauge_number.png       : 0〜9 (10等分)
    TextureRegion texGaugeNumberDetail;  // gauge_number_detail.png: .0123456789 (11等分)

    void loadAndCache(SDL_Renderer* ren, TextureRegion& region, const std::string& path);

    // レーン座標キャッシュ（init() 時に一度だけ計算、描画ループ内で再計算しない）
    struct LaneLayout {
        int x[9] = {};   // lanes 1-8（インデックス0は未使用）
        int w[9] = {};
        int baseX      = 0;
        int totalWidth = 0;  // 全鍵盤幅 + スクラッチ幅
        int bgaCenterX = 0;
        int bgaCenterY = 0;   // BGA中心Y座標
    } ll;
    LaneLayout ll1P, ll2P;  // ★2P VS: 両サイドのレイアウトキャッシュ
    bool dualLayoutReady = false;

    void rebuildLaneLayout();
};

#endif // NOTERENDERER_HPP
