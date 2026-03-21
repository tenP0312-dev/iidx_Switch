#include "NoteRenderer.hpp"
#include "Config.hpp"
#include "Logger.hpp"
#include <cmath>
#include <vector>
#include <string>
#include <algorithm>
#include <map>
#include <SDL2/SDL_image.h>

// --- レーンレイアウトキャッシュ再構築 ---
// Config の値は init() 後に変化しないため、ここで一度だけ計算する。
// 描画ループ内での getXForLane / getWidthForLane の都度計算を排除する。
void NoteRenderer::rebuildLaneLayout() {
    // レーン幅 (Config.hpp の定数で一元管理)
    // lane 1=白, 2=黒, 3=白, 4=黒, 5=白, 6=黒, 7=白, 8=スクラッチ (偶数=黒鍵)
    for (int i = 1; i <= 7; i++)
        ll.w[i] = (i % 2 == 0) ? Config::AC_KEY_BLACK : Config::AC_KEY_WHITE;
    ll.w[8] = Config::AC_SCRATCH;

    int keysWidth = 0;
    for (int i = 1; i <= 7; i++) keysWidth += ll.w[i];
    ll.totalWidth = keysWidth + ll.w[8];

    // 1P: スクラッチ左端。2P: 1Pの鏡像 (スクラッチ右端)
    if (Config::PLAY_SIDE == 1) {
        ll.baseX   = Config::LANE_BASE_X_1P;
        ll.x[8]    = ll.baseX;                  // スクラッチ左端
        int cur    = ll.baseX + ll.w[8];
        for (int i = 1; i <= 7; i++) { ll.x[i] = cur; cur += ll.w[i]; }
    } else {
        // 2P: 画面右側に鏡像配置 (baseXは鍵盤の左端)
        ll.baseX   = Config::SCREEN_WIDTH - Config::LANE_BASE_X_1P - ll.totalWidth;
        int cur    = ll.baseX;
        for (int i = 1; i <= 7; i++) { ll.x[i] = cur; cur += ll.w[i]; }
        ll.x[8]    = cur;                        // スクラッチ右端
    }

    // BGA中心X (Config.hpp の定数で位置固定)
    // BGA/UIテキストの中心X
    ll.bgaCenterX = (Config::PLAY_SIDE == 1)
        ? Config::BGA_CENTER_X_1P
        : Config::BGA_CENTER_X_2P;
    // BGA中心Y (Config::BGA_CENTER_Y で自由に調整可能)
    ll.bgaCenterY = Config::BGA_CENTER_Y;
}

// ★2P VS: 両サイドのレイアウトを事前計算
void NoteRenderer::rebuildBothLayouts() {
    int savedSide = Config::PLAY_SIDE;
    
    Config::PLAY_SIDE = 1;
    rebuildLaneLayout();
    ll1P = ll;
    
    Config::PLAY_SIDE = 2;
    rebuildLaneLayout();
    ll2P = ll;
    
    Config::PLAY_SIDE = savedSide;
    ll = (savedSide == 1) ? ll1P : ll2P;
    dualLayoutReady = true;
}

// ★2P VS: 描画中にサイドを切り替え（rebuildBothLayouts()呼び出し後のみ高速パス）
void NoteRenderer::switchSide(int side) {
    if (dualLayoutReady) {
        ll = (side == 1) ? ll1P : ll2P;
        Config::PLAY_SIDE = side;
    } else {
        Config::PLAY_SIDE = side;
        rebuildLaneLayout();
    }
}

// ============================================================
//  NoteRenderer 実装
// ============================================================

void NoteRenderer::loadAndCache(SDL_Renderer* ren, TextureRegion& region, const std::string& path) {
    region.reset();
    SDL_Surface* s = IMG_Load(path.c_str());
    if (s) {
        region.texture = SDL_CreateTextureFromSurface(ren, s);
        region.w = s->w;
        region.h = s->h;
        SDL_FreeSurface(s);
    } else {
        // printf ではログファイルに出ないため LOG_ERROR に統一
        LOG_ERROR("NoteRenderer", "loadAndCache failed: %s (IMG: %s)", path.c_str(), IMG_GetError());
    }
}

void NoteRenderer::init(SDL_Renderer* ren) {
    TTF_Init();
    IMG_Init(IMG_INIT_PNG | IMG_INIT_JPG);
    // TTCファイルはTTF_OpenFontIndexでインデックス0を指定して読む
    // TTF_OpenFont は TTC に対して index=0 相当だが Switch で失敗するケースがあるため明示指定
    fontSmall = TTF_OpenFontIndex(Config::FONT_PATH.c_str(), Config::FONT_SIZE_SMALL, 0);
    fontBig   = TTF_OpenFontIndex(Config::FONT_PATH.c_str(), Config::FONT_SIZE_BIG,   0);
    if (!fontSmall || !fontBig) {
        LOG_ERROR("NoteRenderer", "init: font load failed path='%s' small=%s big=%s",
                  Config::FONT_PATH.c_str(),
                  fontSmall ? "OK" : "FAIL",
                  fontBig   ? "OK" : "FAIL");
    }

    std::string s = Config::ROOT_PATH + "Skin/";
    int skinOk = 0, skinFail = 0;
    // スキンロード結果を集計するラムダ（失敗時は loadAndCache 内で LOG_ERROR が出る）
    auto track = [&](TextureRegion& r) { if (r) skinOk++; else skinFail++; };

    loadAndCache(ren, texBackground, s + "Flame_BG.png");       track(texBackground);
    loadAndCache(ren, texNoteWhite,  s + "note_white.png");      track(texNoteWhite);
    loadAndCache(ren, texNoteBlue,   s + "note_blue.png");       track(texNoteBlue);
    loadAndCache(ren, texNoteRed,    s + "note_red.png");        track(texNoteRed);

    loadAndCache(ren, texNoteWhite_LN,       s + "note_white_ln.png");         track(texNoteWhite_LN);
    loadAndCache(ren, texNoteWhite_LN_Active1, s + "note_white_ln_active1.png"); track(texNoteWhite_LN_Active1);
    loadAndCache(ren, texNoteWhite_LN_Active2, s + "note_white_ln_active2.png"); track(texNoteWhite_LN_Active2);
    loadAndCache(ren, texNoteBlue_LN,        s + "note_blue_ln.png");          track(texNoteBlue_LN);
    loadAndCache(ren, texNoteBlue_LN_Active1,  s + "note_blue_ln_active1.png"); track(texNoteBlue_LN_Active1);
    loadAndCache(ren, texNoteBlue_LN_Active2,  s + "note_blue_ln_active2.png"); track(texNoteBlue_LN_Active2);
    loadAndCache(ren, texNoteRed_LN,         s + "note_red_ln.png");           track(texNoteRed_LN);
    loadAndCache(ren, texNoteRed_LN_Active1,   s + "note_red_ln_active1.png");  track(texNoteRed_LN_Active1);
    loadAndCache(ren, texNoteRed_LN_Active2,   s + "note_red_ln_active2.png");  track(texNoteRed_LN_Active2);

    loadAndCache(ren, texNoteWhite_LNS, s + "note_white_lns.png"); track(texNoteWhite_LNS);
    loadAndCache(ren, texNoteWhite_LNE, s + "note_white_lne.png"); track(texNoteWhite_LNE);
    loadAndCache(ren, texNoteBlue_LNS,  s + "note_blue_lns.png");  track(texNoteBlue_LNS);
    loadAndCache(ren, texNoteBlue_LNE,  s + "note_blue_lne.png");  track(texNoteBlue_LNE);
    loadAndCache(ren, texNoteRed_LNS,   s + "note_red_lns.png");   track(texNoteRed_LNS);
    loadAndCache(ren, texNoteRed_LNE,   s + "note_red_lne.png");   track(texNoteRed_LNE);
    loadAndCache(ren, texNoteRed_BSS_S,   s + "note_red_bss_s.png");      track(texNoteRed_BSS_S);
    loadAndCache(ren, texNoteRed_BSS_Mid, s + "note_red_bss_middle.png"); track(texNoteRed_BSS_Mid);
    loadAndCache(ren, texNoteRed_BSS_E,   s + "note_red_bss_e.png");      track(texNoteRed_BSS_E);

    loadAndCache(ren, texKeybeamWhite, s + "beam_white.png"); track(texKeybeamWhite);
    loadAndCache(ren, texKeybeamBlue,  s + "beam_blue.png");  track(texKeybeamBlue);
    loadAndCache(ren, texKeybeamRed,   s + "beam_red.png");   track(texKeybeamRed);

    loadAndCache(ren, texJudgePGreatBlue,  s + "judge_p_great_blue.png");  track(texJudgePGreatBlue);
    loadAndCache(ren, texJudgePGreatPink,  s + "judge_p_great_pink.png");  track(texJudgePGreatPink);
    loadAndCache(ren, texJudgePGreatWhite, s + "judge_p_great_white.png"); track(texJudgePGreatWhite);
    loadAndCache(ren, texJudgeGreat,       s + "judge_great_yellow.png");  track(texJudgeGreat);
    loadAndCache(ren, texJudgeGood,        s + "judge_good.png");          track(texJudgeGood);
    loadAndCache(ren, texJudgeBad,         s + "judge_bad.png");           track(texJudgeBad);
    loadAndCache(ren, texJudgePoor,        s + "judge_poor.png");          track(texJudgePoor);
    loadAndCache(ren, texNumberAtlas, s + "judge_number.png"); track(texNumberAtlas);
    loadAndCache(ren, texLaneCover,   s + "lanecover.png");    track(texLaneCover);

    loadAndCache(ren, texGaugeNumber,       s + "gauge_number.png");        track(texGaugeNumber);
    loadAndCache(ren, texGaugeNumberDetail, s + "gauge_number_detail.png"); track(texGaugeNumberDetail);
    loadAndCache(ren, texHsNumber,          s + "hs_number.png");           track(texHsNumber);
    loadAndCache(ren, texScoreNumber,       s + "score_number.png");        track(texScoreNumber);
    loadAndCache(ren, texTurntable,         s + "turn_center.png");         track(texTurntable);
    loadAndCache(ren, texGaugeUp,           s + "gauge_up.png");            track(texGaugeUp);
    loadAndCache(ren, texGaugeAssist, s + "gauge_assist.png"); track(texGaugeAssist);
    loadAndCache(ren, texGaugeNormal, s + "gauge_normal.png"); track(texGaugeNormal);
    loadAndCache(ren, texGaugeHard,   s + "gauge_hard.png");   track(texGaugeHard);
    loadAndCache(ren, texGaugeExHard, s + "gauge_exhard.png"); track(texGaugeExHard);
    loadAndCache(ren, texGaugeHazard, s + "gauge_hazard.png"); track(texGaugeHazard);
    loadAndCache(ren, texGaugeDan,    s + "gauge_dan.png");    track(texGaugeDan);
    // ★修正: gauge_frame は init() で一度だけロードする。
    //        旧実装は renderGauge() 内で毎フレーム std::string 生成 + std::map 探索 + 初回 I/O が走っていた。

    texBombs.clear();
    for (int i = 0; i < 10; i++) {
        TextureRegion tr;
        loadAndCache(ren, tr, s + "bomb_" + std::to_string(i) + ".png");
        if (tr) { texBombs.push_back(tr); skinOk++; } else { skinFail++; }
    }

    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");

    // スキンロード結果サマリ。失敗があれば WARN レベルで目立たせる。
    // 見た目強化でテクスチャを追加したときに「追加したファイルが正しく読めているか」の確認にも使える。
    if (skinFail == 0) {
        LOG_INFO("NoteRenderer", "init done: skin textures OK=%d FAIL=0 bombs=%zu",
                 skinOk, texBombs.size());
    } else {
        LOG_WARN("NoteRenderer", "init done: skin textures OK=%d FAIL=%d bombs=%zu — missing files above",
                 skinOk, skinFail, texBombs.size());
    }

    rebuildLaneLayout();
}

void NoteRenderer::cleanup() {
    clearTextCache();
    if (fontSmall) TTF_CloseFont(fontSmall);
    if (fontBig)   TTF_CloseFont(fontBig);

    texBackground.reset();
    texNoteWhite.reset(); texNoteBlue.reset(); texNoteRed.reset();
    texNoteWhite_LN.reset(); texNoteWhite_LN_Active1.reset(); texNoteWhite_LN_Active2.reset();
    texNoteBlue_LN.reset();  texNoteBlue_LN_Active1.reset();  texNoteBlue_LN_Active2.reset();
    texNoteRed_LN.reset();   texNoteRed_LN_Active1.reset();   texNoteRed_LN_Active2.reset();
    texNoteWhite_LNS.reset(); texNoteWhite_LNE.reset();
    texNoteBlue_LNS.reset();  texNoteBlue_LNE.reset();
    texNoteRed_LNS.reset();   texNoteRed_LNE.reset();
    texNoteRed_BSS_S.reset(); texNoteRed_BSS_Mid.reset(); texNoteRed_BSS_E.reset();
    texKeybeamWhite.reset(); texKeybeamBlue.reset(); texKeybeamRed.reset();
    texJudgePGreatBlue.reset(); texJudgePGreatPink.reset(); texJudgePGreatWhite.reset();
    texJudgeGreat.reset(); texJudgeGood.reset(); texJudgeBad.reset(); texJudgePoor.reset();
    texNumberAtlas.reset(); texLaneCover.reset();
    texGaugeNumber.reset(); texGaugeNumberDetail.reset(); texHsNumber.reset(); texScoreNumber.reset(); texTurntable.reset(); texGaugeUp.reset();
    texGaugeAssist.reset(); texGaugeNormal.reset(); texGaugeHard.reset();
    texGaugeExHard.reset(); texGaugeHazard.reset(); texGaugeDan.reset();

    for (auto& b : texBombs) b.reset();
    texBombs.clear();

    for (auto& pair : textureCache) pair.second.reset();
    textureCache.clear();

    for (auto& pair : customFontCache) if (pair.second) TTF_CloseFont(pair.second);
    customFontCache.clear();

    IMG_Quit();
    TTF_Quit();
}

void NoteRenderer::renderBackground(SDL_Renderer* ren) {
    if (texBackground) {
        if (Config::PLAY_SIDE == 1) {
            SDL_RenderCopy(ren, texBackground.texture, NULL, NULL);
        } else {
            SDL_RenderCopyEx(ren, texBackground.texture, NULL, NULL, 0, NULL, SDL_FLIP_HORIZONTAL);
        }
    }
}

// drawText: ローディング画面など毎フレーム内容が変わる箇所専用。
// ゲームループ内で固定テキストに使うことは厳禁。
void NoteRenderer::drawText(SDL_Renderer* ren, const std::string& text, int x, int y,
                             SDL_Color color, bool isBig, bool isCenter, bool isRight,
                             const std::string& fontPath) {
    if (text.empty()) return;
    TTF_Font* targetFont = isBig ? fontBig : fontSmall;
    if (!fontPath.empty()) {
        if (customFontCache.find(fontPath) == customFontCache.end()) {
            TTF_Font* cf = TTF_OpenFontIndex(fontPath.c_str(), isBig ? Config::FONT_SIZE_BIG : Config::FONT_SIZE_SMALL, 0);
            if (cf) customFontCache[fontPath] = cf;
        }
        if (customFontCache.count(fontPath)) targetFont = customFontCache[fontPath];
    }
    if (!targetFont) return;
    SDL_Surface* s = TTF_RenderUTF8_Blended(targetFont, text.c_str(), color);
    if (!s) return;
    SDL_Texture* t = SDL_CreateTextureFromSurface(ren, s);
    if (t) {
        int drawX = x;
        if (isCenter)    drawX = x - s->w / 2;
        else if (isRight) drawX = x - s->w;
        SDL_Rect dst = { drawX, y, s->w, s->h };
        SDL_RenderCopy(ren, t, NULL, &dst);
        SDL_DestroyTexture(t);
    }
    SDL_FreeSurface(s);
}

void NoteRenderer::drawTextCached(SDL_Renderer* ren, const std::string& text, int x, int y,
                                   SDL_Color color, bool isBig, bool isCenter, bool isRight,
                                   const std::string& fontPath) {
    if (text.empty()) return;
    uint32_t rgba = (color.r << 24) | (color.g << 16) | (color.b << 8) | color.a;
    TextCacheKey key = { text, rgba, isBig, fontPath };

    auto it = textTextureCache.find(key);
    if (it != textTextureCache.end()) {
        lruList.erase(it->second.lruIt);
        lruList.push_front(key);
        it->second.lruIt = lruList.begin();
        int drawX = x;
        if (isCenter)    drawX = x - it->second.w / 2;
        else if (isRight) drawX = x - it->second.w;
        SDL_Rect dst = { drawX, y, it->second.w, it->second.h };
        SDL_RenderCopy(ren, it->second.texture, NULL, &dst);
    } else {
        if (textTextureCache.size() >= MAX_TEXT_CACHE) {
            TextCacheKey oldestKey = lruList.back();
            SDL_DestroyTexture(textTextureCache[oldestKey].texture);
            textTextureCache.erase(oldestKey);
            lruList.pop_back();
        }
        TTF_Font* targetFont = isBig ? fontBig : fontSmall;
        if (!fontPath.empty() && customFontCache.count(fontPath)) targetFont = customFontCache[fontPath];
        if (!targetFont) return;
        SDL_Surface* s = TTF_RenderUTF8_Blended(targetFont, text.c_str(), color);
        if (s) {
            SDL_Texture* t = SDL_CreateTextureFromSurface(ren, s);
            if (t) {
                lruList.push_front(key);
                textTextureCache[key] = { t, s->w, s->h, lruList.begin() };
                int drawX = x;
                if (isCenter)    drawX = x - s->w / 2;
                else if (isRight) drawX = x - s->w;
                SDL_Rect dst = { drawX, y, s->w, s->h };
                SDL_RenderCopy(ren, t, NULL, &dst);
            }
            SDL_FreeSurface(s);
        }
    }
}

void NoteRenderer::clearTextCache() {
    for (auto& pair : textTextureCache) if (pair.second.texture) SDL_DestroyTexture(pair.second.texture);
    textTextureCache.clear();
    lruList.clear();
}

void NoteRenderer::drawImage(SDL_Renderer* ren, const std::string& path,
                              int x, int y, int w, int h, int alpha) {
    if (path.empty()) return;
    // ★修正: find() → operator[] → operator[] の3回探索を廃止。
    //        try_emplace は「キーがなければデフォルト挿入、あれば既存」を1回の探索で返す。
    //        O(3 log N) → O(log N)。初回のみ loadAndCache が走り、以降はキャッシュヒット。
    auto [it, inserted] = textureCache.try_emplace(path);
    if (inserted) {
        loadAndCache(ren, it->second, path);
    }
    const TextureRegion& tr = it->second;
    if (tr) {
        SDL_SetTextureAlphaMod(tr.texture, (Uint8)alpha);
        SDL_Rect dst = { x, y, w, h };
        SDL_RenderCopy(ren, tr.texture, NULL, &dst);
    }
}

void NoteRenderer::renderDecisionInfo(SDL_Renderer* ren, const BMSHeader& header) {
    SDL_Color white  = {255, 255, 255, 255};
    SDL_Color gray   = {180, 180, 180, 255};
    SDL_Color yellow = {255, 255, 0, 255};
    int centerX = 640;
    drawTextCached(ren, header.genre,   centerX, 220, gray, false, true);
    drawTextCached(ren, header.title,   centerX, 270, white, true, true);
    drawTextCached(ren, header.artist,  centerX, 340, white, false, true);
    // ★修正: "[" + chartName + "]  LEVEL " + std::to_string() による3回のヒープアロケーションを廃止。
    //        snprintf でスタックバッファに書いて drawTextCached のキャッシュを活用する。
    char levelInfo[128];
    snprintf(levelInfo, sizeof(levelInfo), "[%s]  LEVEL %d", header.chartName.c_str(), header.level);
    drawTextCached(ren, levelInfo, centerX, 375, yellow, false, true);
}

void NoteRenderer::renderUI(SDL_Renderer* ren, const BMSHeader& header, int fps, double bpm, int exScore) {
    int centerX = ll.bgaCenterX;
    SDL_Color white  = {255, 255, 255, 255};
    SDL_Color dimmed = {180, 180, 180, 255};
    drawTextCached(ren, header.title,  centerX, 8,  white,  false, true);
    drawTextCached(ren, header.artist, centerX, 36, dimmed, false, true);
    renderScore(ren, exScore);
    renderHiSpeed(ren);
}

void NoteRenderer::renderLanes(SDL_Renderer* ren, double progress, int scratchStatus) {
    // レーンレイアウトは init() および notifyLayoutChanged() 呼び出し時のみ再計算。
    // プレイ中にオプションは変更されないため毎フレームの再計算は不要。
    int totalWidth = ll.totalWidth;
    int startX     = ll.baseX;
    int laneHeight = 482;
    int judgeY     = Config::JUDGMENT_LINE_Y - Config::LIFT;

    if (Config::SUDDEN_PLUS > 0) {
        // SUDDEN_PLUSオーバーレイはrenderSuddenLift()で描画（ノーツ描画後）
    }
    if (Config::LIFT > 0) {
        // LIFTオーバーレイはrenderSuddenLift()で描画（ノーツ描画後）
    }
    // 各レーンの区切り線
    SDL_SetRenderDrawColor(ren, 50, 50, 50, 255);
    for (int i = 1; i <= 8; i++) {
        int lx = ll.x[i];
        SDL_RenderDrawLine(ren, lx, 0, lx, laneHeight);
    }
    int rightEdge = startX + totalWidth;
    SDL_RenderDrawLine(ren, rightEdge, 0, rightEdge, laneHeight);

    // 判定ライン
    SDL_SetRenderDrawColor(ren, 255, 0, 0, 255);
    SDL_RenderDrawLine(ren, startX, judgeY, startX + totalWidth, judgeY);

    // プログレスバー (楽曲進行)
    int progX = (Config::PLAY_SIDE == 1)
        ? startX - Config::PROGRESS_BAR_X_OFFSET
        : startX + totalWidth + (Config::PROGRESS_BAR_X_OFFSET - 5);
    int progY = Config::PROGRESS_BAR_Y;
    int progH = Config::PROGRESS_BAR_H;
    int indicatorH = 8;
    SDL_Rect progFrame = { progX, progY, 5, progH };
    SDL_SetRenderDrawColor(ren, 60, 60, 60, 255);
    SDL_RenderDrawRect(ren, &progFrame);
    int moveY = progY + (int)((progH - indicatorH) * progress);
    SDL_Rect progIndicator = { progX, moveY, 4, indicatorH };
    SDL_SetRenderDrawColor(ren, 200, 200, 200, 255);
    SDL_RenderFillRect(ren, &progIndicator);
}

void NoteRenderer::renderLaneDividers(SDL_Renderer* ren) {
    int laneHeight = 482;
    int judgeY     = Config::JUDGMENT_LINE_Y - Config::LIFT;
    SDL_SetRenderDrawColor(ren, 50, 50, 50, 255);
    for (int i = 1; i <= 8; i++) {
        SDL_RenderDrawLine(ren, ll.x[i], 0, ll.x[i], laneHeight);
    }
    SDL_RenderDrawLine(ren, ll.baseX + ll.totalWidth, 0, ll.baseX + ll.totalWidth, laneHeight);
    SDL_SetRenderDrawColor(ren, 255, 0, 0, 255);
    SDL_RenderDrawLine(ren, ll.baseX, judgeY, ll.baseX + ll.totalWidth, judgeY);
}

void NoteRenderer::renderBpm(SDL_Renderer* ren, double currentBpm, double minBpm, double maxBpm, bool bpmVaries) {
    if (Config::BPM_SHOW == 0 || !texHsNumber) return;
    bool is2P = (Config::PLAY_SIDE == 2);

    const int numCols = 10;
    const int srcCW   = texHsNumber.w / numCols;
    const int srcCH   = texHsNumber.h;
    int align          = is2P ? Config::BPM_ALIGN_2P        : Config::BPM_ALIGN_1P;
    int scaleCur       = is2P ? Config::BPM_SCALE_2P        : Config::BPM_SCALE_1P;
    int scaleMM        = is2P ? Config::BPM_MINMAX_SCALE_2P : Config::BPM_MINMAX_SCALE_1P;
    int curX           = is2P ? Config::BPM_CUR_X_2P        : Config::BPM_CUR_X_1P;
    int curY           = is2P ? Config::BPM_CUR_Y_2P        : Config::BPM_CUR_Y_1P;
    int minX           = is2P ? Config::BPM_MIN_X_2P        : Config::BPM_MIN_X_1P;
    int minY           = is2P ? Config::BPM_MIN_Y_2P        : Config::BPM_MIN_Y_1P;
    int maxX           = is2P ? Config::BPM_MAX_X_2P        : Config::BPM_MAX_X_1P;
    int maxY           = is2P ? Config::BPM_MAX_Y_2P        : Config::BPM_MAX_Y_1P;

    auto drawNum = [&](double val, int baseX, int baseY, int scale) {
        const int dH = std::max(1, srcCH * scale / 100);
        const int dW = (srcCH > 0) ? (srcCW * dH / srcCH) : srcCW;
        int iv = (int)std::round(val);
        if (iv < 0) iv = 0;
        char buf[12];
        snprintf(buf, sizeof(buf), "%d", iv);
        int len    = (int)strlen(buf);
        int totalW = len * dW;
        int startX;
        switch (align) {
            case 1:  startX = baseX - totalW / 2; break;
            case 2:  startX = baseX - totalW;     break;
            default: startX = baseX;               break;
        }
        for (int ci = 0; ci < len; ci++) {
            char c = buf[ci];
            if (c < '0' || c > '9') continue;
            int colIdx = c - '0';
            SDL_Rect src = { colIdx * srcCW, 0, srcCW, srcCH };
            SDL_Rect dst = { startX + ci * dW, baseY, dW, dH };
            SDL_RenderCopy(ren, texHsNumber.texture, &src, &dst);
        }
    };

    drawNum(currentBpm, curX, curY, scaleCur);

    if (bpmVaries || Config::BPM_SHOW_MINMAX == 1) {
        drawNum(minBpm, minX, minY, scaleMM);
        drawNum(maxBpm, maxX, maxY, scaleMM);
    }
}

void NoteRenderer::renderHiSpeed(SDL_Renderer* ren) {
    if (Config::HS_DISP_SHOW == 0 || !texHsNumber) return;
    bool is2P = (Config::PLAY_SIDE == 2);

    const int numCols = 10;
    const int srcCW   = texHsNumber.w / numCols;
    const int srcCH   = texHsNumber.h;
    int scale = is2P ? Config::HS_DISP_SCALE_2P : Config::HS_DISP_SCALE_1P;
    int align = is2P ? Config::HS_DISP_ALIGN_2P : Config::HS_DISP_ALIGN_1P;
    int baseX = is2P ? Config::HS_DISP_X_2P     : Config::HS_DISP_X_1P;
    int baseY = is2P ? Config::HS_DISP_Y_2P     : Config::HS_DISP_Y_1P;
    const int dH = std::max(1, srcCH * scale / 100);
    const int dW = (srcCH > 0) ? (srcCW * dH / srcCH) : srcCW;

    int iv = (int)std::round(Config::HIGH_SPEED * 100.0);
    if (iv < 0) iv = 0;
    char buf[12];
    snprintf(buf, sizeof(buf), "%d", iv);
    int len    = (int)strlen(buf);
    int totalW = len * dW;
    int startX;
    switch (align) {
        case 1:  startX = baseX - totalW / 2; break;
        case 2:  startX = baseX - totalW;     break;
        default: startX = baseX;               break;
    }
    for (int ci = 0; ci < len; ci++) {
        char c = buf[ci];
        if (c < '0' || c > '9') continue;
        int colIdx = c - '0';
        SDL_Rect src = { colIdx * srcCW, 0, srcCW, srcCH };
        SDL_Rect dst = { startX + ci * dW, baseY, dW, dH };
        SDL_RenderCopy(ren, texHsNumber.texture, &src, &dst);
    }
}

void NoteRenderer::updateTurntable(int scratchStatus, uint32_t now) {
    if (turntableLastMs_ == 0) {
        turntableLastMs_ = now;
        return;
    }
    double deltaMs = (double)(now - turntableLastMs_);
    turntableLastMs_ = now;

    int speedVal;
    switch (scratchStatus) {
        case 1:  speedVal = Config::TURNTABLE_SPEED_A;      break;
        case 2:  speedVal = Config::TURNTABLE_SPEED_B;      break;
        default: speedVal = Config::TURNTABLE_SPEED_NORMAL; break;
    }
    // speedVal 10 = 360 deg/sec
    double degreesPerMs = speedVal * 36.0 / 1000.0;
    turntableAngle_ += degreesPerMs * deltaMs;
    if (turntableAngle_ >= 360.0)  turntableAngle_ -= 360.0;
    if (turntableAngle_ < 0.0)     turntableAngle_ += 360.0;
}

void NoteRenderer::renderTurntable(SDL_Renderer* ren) {
    if (Config::TURNTABLE_SHOW == 0 || !texTurntable) return;
    bool is2P = (Config::PLAY_SIDE == 2);
    int scale = is2P ? Config::TURNTABLE_SCALE_2P : Config::TURNTABLE_SCALE_1P;
    float cx  = (float)(is2P ? Config::TURNTABLE_X_2P : Config::TURNTABLE_X_1P);
    float cy  = (float)(is2P ? Config::TURNTABLE_Y_2P : Config::TURNTABLE_Y_1P);
    float dw  = (float)std::max(1, texTurntable.w * scale / 100);
    float dh  = (float)std::max(1, texTurntable.h * scale / 100);
    SDL_FRect  dst    = { cx - dw * 0.5f, cy - dh * 0.5f, dw, dh };
    SDL_FPoint center = { dw * 0.5f, dh * 0.5f };
    SDL_RenderCopyExF(ren, texTurntable.texture, nullptr, &dst,
                      turntableAngle_, &center, SDL_FLIP_NONE);
}

void NoteRenderer::renderScore(SDL_Renderer* ren, int score) {
    if (Config::SCORE_SHOW == 0 || !texScoreNumber) return;
    bool is2P = (Config::PLAY_SIDE == 2);

    const int numCols = 10;
    const int srcCW   = texScoreNumber.w / numCols;
    const int srcCH   = texScoreNumber.h;
    int scale = is2P ? Config::SCORE_SCALE_2P : Config::SCORE_SCALE_1P;
    int align = is2P ? Config::SCORE_ALIGN_2P : Config::SCORE_ALIGN_1P;
    int baseX = is2P ? Config::SCORE_X_2P     : Config::SCORE_X_1P;
    int baseY = is2P ? Config::SCORE_Y_2P     : Config::SCORE_Y_1P;
    const int dH = std::max(1, srcCH * scale / 100);
    const int dW = (srcCH > 0) ? (srcCW * dH / srcCH) : srcCW;

    const int digits     = 4;
    int clampedScore     = std::clamp(score, 0, 9999);
    int d[4] = {
        clampedScore / 1000,
        (clampedScore / 100) % 10,
        (clampedScore / 10)  % 10,
        clampedScore         % 10
    };

    // 先頭の0を薄くする（最低1桁は常に表示）
    int firstNonZero = 0;
    while (firstNonZero < digits - 1 && d[firstNonZero] == 0) firstNonZero++;

    const int totalW = digits * dW;
    int startX;
    switch (align) {
        case 1:  startX = baseX - totalW / 2; break;
        case 2:  startX = baseX - totalW;     break;
        default: startX = baseX;               break;
    }

    for (int ci = 0; ci < digits; ci++) {
        SDL_Rect src = { d[ci] * srcCW, 0, srcCW, srcCH };
        SDL_Rect dst = { startX + ci * dW, baseY, dW, dH };
        if (ci < firstNonZero)
            SDL_SetTextureAlphaMod(texScoreNumber.texture, 64); // 25%
        SDL_RenderCopy(ren, texScoreNumber.texture, &src, &dst);
        if (ci < firstNonZero)
            SDL_SetTextureAlphaMod(texScoreNumber.texture, 255);
    }
}

void NoteRenderer::renderNote(SDL_Renderer* ren, const PlayableNote& note,
                               int64_t cur_y, double pixels_per_y, bool isAuto) {
    int x = ll.x[note.lane], w = ll.w[note.lane];
    int judgeY = Config::JUDGMENT_LINE_Y - Config::LIFT;

    // headY: Y座標の差 × 定数（BPMに一切依存しない）
    // cur_y がBPM連動で進むことでソフランが表現される
    // ★修正(CRITICAL-3): ノーツ描画位置には VISUAL_OFFSET を使う。
    //        JUDGE_OFFSET は判定タイミングのみに影響し、見た目位置とは独立。
    int headY = judgeY - (int)((note.y - cur_y) * pixels_per_y) - Config::VISUAL_OFFSET;
    if (note.isLN && note.isBeingPressed) headY = judgeY - Config::VISUAL_OFFSET;

    // ★修正(MAJOR-2): テクスチャ色選択を分岐 3 段 → テーブル引きに変更。
    // レーン番号から色インデックスを決定: 8→赤(0), 偶数→青(1), 奇数→白(2)
    // CPU の分岐予測ミスを回避し、毎フレーム 100-500 回の呼び出しでサイクルを節約する。
    struct LaneTexSet {
        const TextureRegion *target, *lnB, *lnA1, *lnA2, *lnS, *lnE;
    };
    const LaneTexSet texSets[3] = {
        { &texNoteRed,   &texNoteRed_LN,   &texNoteRed_LN_Active1,   &texNoteRed_LN_Active2,   &texNoteRed_LNS,   &texNoteRed_LNE },
        { &texNoteBlue,  &texNoteBlue_LN,  &texNoteBlue_LN_Active1,  &texNoteBlue_LN_Active2,  &texNoteBlue_LNS,  &texNoteBlue_LNE },
        { &texNoteWhite, &texNoteWhite_LN, &texNoteWhite_LN_Active1, &texNoteWhite_LN_Active2, &texNoteWhite_LNS, &texNoteWhite_LNE },
    };
    int colorIdx = (note.lane == 8) ? 0 : (note.lane % 2 == 0) ? 1 : 2;
    const LaneTexSet& ts = texSets[colorIdx];
    const TextureRegion *target = ts.target, *lnB = ts.lnB, *lnA1 = ts.lnA1, *lnA2 = ts.lnA2, *lnS = ts.lnS, *lnE = ts.lnE;

    if (note.isLN) {
        int tailY = judgeY - (int)((note.y + note.l - cur_y) * pixels_per_y) - Config::VISUAL_OFFSET;

        bool shouldDraw = note.isBeingPressed
            || !(tailY > judgeY || headY < (int)Config::SUDDEN_PLUS - 5000);

        if (shouldDraw) {
            SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
            const TextureRegion* body = note.isBeingPressed
                ? ((SDL_GetTicks() / 100 % 2 == 0) ? lnA1 : lnA2)
                : lnB;

            int dTY = std::max(tailY, (int)Config::SUDDEN_PLUS);
            int dHY = std::min(headY, judgeY);

            // ボディ: 始点下端〜終点下端まで（終点テクスチャと重なるがその後で上描き）
            if (dHY > dTY && body && *body) {
                SDL_Rect r = { x + 4, dTY, w - 8, dHY - dTY };
                SDL_RenderCopy(ren, body->texture, NULL, &r);
            }

            // 終点テクスチャ（ボディより後に描くので上に来る）
            const TextureRegion* endTex = nullptr;
            if (note.isBSSTail) {
                endTex = (texNoteRed_BSS_E)   ? &texNoteRed_BSS_E   : lnE;
            } else if (note.isBSSMid || note.isBSSHead) {
                endTex = (texNoteRed_BSS_Mid) ? &texNoteRed_BSS_Mid : lnE;
            } else {
                endTex = (lnE && *lnE) ? lnE : target;
            }

            // middle: 自LN終点と次LN始点の中間に中心を合わせる
            int endDrawY = tailY;
            if ((note.isBSSHead || note.isBSSMid) && note.bssNextY != 0 && endTex && *endTex) {
                int nextHeadY = judgeY - (int)((note.bssNextY - cur_y) * pixels_per_y) - Config::VISUAL_OFFSET;
                int centerY   = (tailY + nextHeadY) / 2;
                endDrawY = centerY + endTex->h / 2;
            }

            if (endDrawY >= (int)Config::SUDDEN_PLUS && endTex && *endTex && endDrawY <= judgeY + endTex->h) {
                SDL_Rect r = { x + 2, endDrawY - endTex->h, w - 4, endTex->h };
                SDL_RenderCopy(ren, endTex->texture, NULL, &r);
            }
            SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);
        }
    }

    // 始点描画
    // BSS中間・末尾LNは始点キャップを描かない（前のLNとbss_middleで繋がっているため）
    if (!(headY < (int)Config::SUDDEN_PLUS || headY > judgeY + 20)) {
        if (note.isBSSMid || note.isBSSTail) {
            // 始点キャップなし
        } else if (note.isBSSHead) {
            // 先頭LN → bss_s
            const TextureRegion* headTex = (texNoteRed_BSS_S) ? &texNoteRed_BSS_S : lnS;
            if (headTex && *headTex) {
                SDL_Rect r = { x + 2, headY - headTex->h, w - 4, headTex->h };
                SDL_RenderCopy(ren, headTex->texture, NULL, &r);
            }
        } else {
            // 通常LN or 単発
            const TextureRegion* headTex = (note.isLN && lnS && *lnS) ? lnS : target;
            if (headTex && *headTex) {
                SDL_Rect r = { x + 2, headY - headTex->h, w - 4, headTex->h };
                SDL_RenderCopy(ren, headTex->texture, NULL, &r);
            }
        }
    }
}

void NoteRenderer::renderSuddenLift(SDL_Renderer* ren) {
    // レーンレイアウトは init() および notifyLayoutChanged() 呼び出し時のみ再計算。
    int totalWidth = ll.totalWidth;
    int startX     = ll.baseX;
    int laneHeight = 482;
    int judgeY     = Config::JUDGMENT_LINE_Y - Config::LIFT;

    if (Config::SUDDEN_PLUS > 0) {
        int sH = std::min(Config::SUDDEN_PLUS, laneHeight);
        SDL_Rect dR = { startX, 0, totalWidth, sH };
        if (texLaneCover) {
            SDL_Rect sR = { 0, texLaneCover.h - sH, texLaneCover.w, sH };
            SDL_RenderCopy(ren, texLaneCover.texture, &sR, &dR);
        } else {
            SDL_SetRenderDrawColor(ren, 20, 20, 20, 255); SDL_RenderFillRect(ren, &dR);
        }
    }
    if (Config::LIFT > 0) {
        SDL_Rect dR = { startX, judgeY, totalWidth, Config::LIFT };
        if (texLaneCover) {
            SDL_Rect sR = { 0, 0, texLaneCover.w, std::min(Config::LIFT, texLaneCover.h) };
            SDL_RenderCopy(ren, texLaneCover.texture, &sR, &dR);
        } else {
            SDL_SetRenderDrawColor(ren, 20, 20, 20, 255); SDL_RenderFillRect(ren, &dR);
        }
    }
}

void NoteRenderer::renderBeatLine(SDL_Renderer* ren, double diff_y, double pixels_per_y) {
    int judgeY = Config::JUDGMENT_LINE_Y - Config::LIFT;
    // 小節線も描画オフセットに従う
    int y      = judgeY - (int)(diff_y * pixels_per_y) - Config::VISUAL_OFFSET;
    if (y < Config::SUDDEN_PLUS || y > judgeY) return;
    SDL_SetRenderDrawColor(ren, 60, 60, 70, 255);
    SDL_RenderDrawLine(ren, ll.baseX, y, ll.baseX + ll.totalWidth, y);
}

void NoteRenderer::renderHitEffect(SDL_Renderer* ren, int lane, float progress) {
    int fullW  = ll.w[lane];
    int cX     = ll.x[lane] + fullW / 2;
    int judgeY = Config::JUDGMENT_LINE_Y - Config::LIFT;
    int curW   = (int)(fullW * (1.0f - progress));
    if (curW < 1) return;
    const TextureRegion* beam = (lane == 8) ? &texKeybeamRed
                              : (lane % 2 == 0 ? &texKeybeamBlue : &texKeybeamWhite);
    if (beam && *beam) {
        SDL_SetTextureBlendMode(beam->texture, SDL_BLENDMODE_ADD);
        SDL_SetTextureAlphaMod(beam->texture, (Uint8)((1.0f - progress) * 200));
        SDL_Rect r = { cX - curW / 2, judgeY - 300, curW, 300 };
        SDL_RenderCopy(ren, beam->texture, NULL, &r);
    }
}

void NoteRenderer::renderBomb(SDL_Renderer* ren, int lane, int frame) {
    if (texBombs.empty() || frame < 0 || frame >= (int)texBombs.size()) return;
    int fullW  = ll.w[lane];
    int cX     = ll.x[lane] + (fullW / 2);
    int judgeY = Config::JUDGMENT_LINE_Y - Config::LIFT;
    int size   = Config::BOMB_SIZE;
    const TextureRegion& tr = texBombs[frame];
    if (tr) {
        SDL_SetTextureBlendMode(tr.texture, SDL_BLENDMODE_ADD);
        SDL_Rect r = { cX - size / 2, judgeY - size / 2, size, size };
        SDL_RenderCopy(ren, tr.texture, NULL, &r);
    }
}

void NoteRenderer::renderEffects(SDL_Renderer* ren,
                                  ActiveEffect* buf, size_t& count,
                                  const bool* lanePressed, uint32_t now) {
    size_t write = 0;
    for (size_t read = 0; read < count; ++read) {
        ActiveEffect& eff = buf[read];
        // ホールド中はタイマーをリセットし続ける
        if (eff.lane >= 1 && eff.lane <= 7 && lanePressed && lanePressed[eff.lane])
            eff.startTime = now;
        float duration = (eff.lane == 8) ? 200.0f : 100.0f;
        float p = (float)(now - eff.startTime) / duration;
        if (p < 1.0f) {
            renderHitEffect(ren, eff.lane, p);
            if (write != read) buf[write] = eff;
            write++;
        }
    }
    count = write;
}

void NoteRenderer::renderBombs(SDL_Renderer* ren,
                                 BombAnim* buf, size_t& count,
                                 uint32_t now) {
    size_t write = 0;
    for (size_t read = 0; read < count; ++read) {
        BombAnim& ba = buf[read];
        float p = (float)(now - ba.startTime) / (float)Config::BOMB_DURATION_MS;
        if (p < 1.0f) {
            if (ba.judgeType == 1 || ba.judgeType == 2)
                renderBomb(ren, ba.lane, (int)(p * 10));
            if (write != read) buf[write] = ba;
            write++;
        }
    }
    count = write;
}

void NoteRenderer::renderFastSlow(SDL_Renderer* ren, bool isFast, bool isSlow, float progress, double diffMs) {
    if (Config::SHOW_FAST_SLOW == 0) return;
    if (!isFast && !isSlow) return;
    Uint8 alpha = (Uint8)(255 * (1.0f - std::min(1.0f, progress)));
    int x = ll.baseX + ll.totalWidth / 2;
    int y = (Config::JUDGMENT_LINE_Y - Config::FASTSLOW_Y_OFFSET) - Config::LIFT;

    SDL_Color color = isFast ? SDL_Color{0, 200, 255, alpha}
                             : SDL_Color{255, 140, 0,   alpha};

    if (Config::SHOW_FAST_SLOW == 2) {
        // DETAIL: "FAST +12.3ms" / "SLOW -8.1ms" 形式
        char buf[TEXT_BUF_SIZE];
        snprintf(buf, sizeof(buf), "%s %+.1fms", isFast ? "FAST" : "SLOW", diffMs);
        drawText(ren, buf, x, y, color, false, true);
    } else {
        // ON: 固定文字列なのでキャッシュ版を使用（毎フレームテクスチャ生成を回避）
        drawTextCached(ren, isFast ? "FAST" : "SLOW", x, y, color, false, true);
    }
}


void NoteRenderer::renderJudgment(SDL_Renderer* ren, JudgeKind kind, float progress, int combo) {
    if (kind == JudgeKind::NONE || !texNumberAtlas) return;

    // KindKind → 使用テクスチャを選択
    // P-GREAT は3コマ点滅、それ以外は固定
    const TextureRegion* jTex = nullptr;
    bool isPGreat = false;
    int  colorIdx = 3; // コンボ数字の色: 3=白
    switch (kind) {
        case JudgeKind::PGREAT: {
            isPGreat = true;
            int frame = SDL_GetTicks() / 50 % 3;
            jTex = (frame == 0) ? &texJudgePGreatBlue
                 : (frame == 1) ? &texJudgePGreatPink
                 :                &texJudgePGreatWhite;
            colorIdx = SDL_GetTicks() / 50 % 3; // P-GREAT は色付きコンボ
            break;
        }
        case JudgeKind::GREAT: jTex = &texJudgeGreat; break;
        case JudgeKind::GOOD:  jTex = &texJudgeGood;  break;
        case JudgeKind::BAD:   jTex = &texJudgeBad;   break;
        default:               jTex = &texJudgePoor;  break;
    }
    if (!jTex || !*jTex) return;

    float s = 0.6f;
    int nw = texNumberAtlas.w / 4, nh = texNumberAtlas.h / 10;
    int dJW = (int)(jTex->w * s), dJH = (int)(jTex->h * s);
    int dNW = (int)(nw * s),       dNH = (int)(nh * s);

    // combo を文字列化
    char comboStr[16] = {};
    int  comboLen = 0;
    if (combo > 0) {
        comboLen = snprintf(comboStr, sizeof(comboStr), "%d", combo);
    }

    // センタリング: 判定画像の実サイズ基準なので全種で正確に中央に来る
    int tW = dJW + (comboLen > 0 ? 20 + comboLen * (dNW - 10) : 0);
    int centerX = ll.baseX + ll.totalWidth / 2;
    int sX = centerX - tW / 2;
    int dY = (Config::JUDGMENT_LINE_Y - Config::JUDGE_Y_OFFSET) - Config::LIFT;
    Uint8 alpha = (Uint8)(255 * (1.0f - progress));

    SDL_Rect jD = { sX, dY, dJW, dJH };
    SDL_SetTextureAlphaMod(jTex->texture, alpha);
    SDL_RenderCopy(ren, jTex->texture, NULL, &jD);

    if (comboLen > 0) {
        int curX = sX + dJW + 20;
        SDL_SetTextureAlphaMod(texNumberAtlas.texture, alpha);
        int colorIdx = isPGreat ? (SDL_GetTicks() / 50 % 3) : 3;
        for (int ci = 0; ci < comboLen; ++ci) {
            int digit = comboStr[ci] - '0';
            SDL_Rect nS = { colorIdx * nw, digit * nh, nw, nh };
            SDL_Rect nD = { curX, dY - (dNH - dJH) / 2, dNW, dNH };
            SDL_RenderCopy(ren, texNumberAtlas.texture, &nS, &nD);
            curX += (dNW - 10);
        }
    }
}

// 後方互換用オーバーロード（旧 std::string 版）
void NoteRenderer::renderJudgment(SDL_Renderer* ren, const std::string& text,
                                   float progress, SDL_Color /*color*/, int combo) {
    JudgeKind kind = JudgeKind::POOR;
    if      (text == "P-GREAT") kind = JudgeKind::PGREAT;
    else if (text == "GREAT")   kind = JudgeKind::GREAT;
    else if (text == "GOOD")    kind = JudgeKind::GOOD;
    else if (text == "BAD")     kind = JudgeKind::BAD;
    renderJudgment(ren, kind, progress, combo);
}

void NoteRenderer::renderCombo(SDL_Renderer* /*ren*/, int /*combo*/) {
    // 未実装
}

void NoteRenderer::renderGauge(SDL_Renderer* ren, double gaugeValue, int gaugeOption, bool isFailed) {
    int totalW = Config::GAUGE_W;
    int gX = (Config::PLAY_SIDE == 1) ? Config::GAUGE_X_1P : Config::GAUGE_X_2P;

    const TextureRegion* target = nullptr;
    if (!isFailed) {
        if (gaugeOption == 3)      target = &texGaugeHard;
        else if (gaugeOption == 4) target = &texGaugeExHard;
        else if (gaugeOption == 5) target = &texGaugeDan;
        else if (gaugeOption == 6) target = &texGaugeHazard;
        else if (gaugeOption == 1) target = &texGaugeAssist;
        else                       target = &texGaugeNormal;
    }

    int dGH = 12;
    if (target && *target)
        dGH = std::min(25, (int)(totalW * ((float)target->h / target->w)));
    int gY = (Config::SCREEN_HEIGHT - Config::GAUGE_BOTTOM_MARGIN) - dGH;

    if (target && *target) {
        SDL_SetTextureAlphaMod(target->texture, 60);
        SDL_Rect bgR = { gX, gY, totalW, dGH };
        SDL_RenderCopyEx(ren, target->texture, NULL, &bgR, 0, NULL,
                         (Config::PLAY_SIDE == 1 ? SDL_FLIP_NONE : SDL_FLIP_HORIZONTAL));
        SDL_SetTextureAlphaMod(target->texture, 255);

        float segW  = (float)totalW / 50.0f;
        float sSegW = (float)target->w / 50.0f;
        // GAUGE_DISPLAY_TYPE==1: 2刻み偶数表示、それ以外: そのまま
        int displayVal = std::clamp((int)gaugeValue, 0, 100);
        int activeS = (Config::GAUGE_DISPLAY_TYPE == 1) ? ((displayVal / 2) * 2) / 2 : displayVal / 2;

        // ★修正: rand() を完全廃止。
        //        rand() はグローバル状態を持ちスレッド不安全。srand() 未設定では毎回同じ列が出る。
        //        SDL_GetTicks() ベースの決定論的アニメーションに統一する。
        //        旧条件: rand() % 100 < 50 (= 50%の確率で点滅)
        //        新条件: (SDL_GetTicks() / 60 + i) % 2 == 0 (セグメント i ごとに位相をずらした点滅)
        //        これによりランダムではなく「交互点滅」になるが視覚的には同等で deterministic。
        uint32_t ticks = SDL_GetTicks();
        for (int i = 0; i < 50; i++) {
            if (i < activeS && (i == activeS - 1 || i < activeS - 4
                || (ticks / 60 + i) % 2 == 0)) {
                int cP = (int)(i * segW), nP = (int)((i + 1) * segW);
                int dx = (Config::PLAY_SIDE == 1) ? (gX + cP) : (gX + totalW - nP);
                SDL_Rect dR = { dx, gY, nP - cP, dGH };
                SDL_Rect sR = { (int)(i * sSegW), 0,
                                (int)((i + 1) * sSegW) - (int)(i * sSegW), target->h };
                SDL_RenderCopyEx(ren, target->texture, &sR, &dR, 0, NULL,
                                 (Config::PLAY_SIDE == 1 ? SDL_FLIP_NONE : SDL_FLIP_HORIZONTAL));
            }
        }
    } else if (isFailed) {
        SDL_SetRenderDrawColor(ren, 60, 60, 60, 255);
        SDL_Rect r = { gX, gY, totalW, dGH };
        SDL_RenderFillRect(ren, &r);
    }

    // ─── ゲージ％数字描画 ────────────────────────────────────────────────────
    // GAUGE_NUM_SHOW: 0=非表示
    //                 1=整数表示 (GAUGE_DISPLAY_TYPE==1 なら2%刻み、0なら1%刻み)
    //                 2=小数表示 (0.1%刻み、gauge_number_detail.png 使用)
    //
    // gauge_number.png        : "0123456789"  を横に10等分 → 1文字 = w/10
    // gauge_number_detail.png : ".0123456789" を横に11等分 → 1文字 = w/11
    //   先頭インデックス 0 = ドット、1〜10 = '0'〜'9'
    //
    // X座標は GAUGE_NUM_X_1P / GAUGE_NUM_X_2P (スクリーン絶対値) で独立管理。
    // ★右揃え: numX を文字列右端の座標として扱う。
    // Y座標は gY + GAUGE_NUM_Y_OFFSET。
    if (Config::GAUGE_NUM_SHOW > 0) {
        const bool useDetail = (Config::GAUGE_NUM_SHOW == 2);
        const TextureRegion& numTex = useDetail ? texGaugeNumberDetail : texGaugeNumber;
        if (numTex) {
            // 1文字のソース幅・高さ (テクスチャを等分)
            const int numCols = useDetail ? 11 : 10;
            const int srcCW   = numTex.w / numCols;
            const int srcCH   = numTex.h;

            // 表示値の決定
            double val = std::clamp(gaugeValue, 0.0, 100.0);

            // 文字配列に展開 (最大6文字: "100.0\0")
            char buf[8];
            if (useDetail) {
                // 0.1%刻み: "100.0", "99.9", "0.0"
                int iv = (int)val;
                int fv = (int)std::round((val - iv) * 10.0);
                if (fv >= 10) { iv++; fv = 0; }
                iv = std::clamp(iv, 0, 100);
                snprintf(buf, sizeof(buf), "%d.%d", iv, fv);
            } else {
                // 整数: GAUGE_DISPLAY_TYPEに従い2%または1%刻み
                int iv = (int)std::round(val);
                if (Config::GAUGE_DISPLAY_TYPE == 1) iv = (iv / 2) * 2; // 2%刻み
                snprintf(buf, sizeof(buf), "%d", std::clamp(iv, 0, 100));
            }
            const int numLen = (int)strlen(buf);

            // 描画サイズ: 高さをゲージ高さに合わせ、幅はアスペクト比で決定。
            // GAUGE_NUM_SCALE(%) でさらにスケーリング。100=そのまま、50=半分。
            const int dH  = (srcCH > 0)
                ? (srcCH * Config::GAUGE_NUM_SCALE / 100)
                : srcCH;
            const int dW  = (srcCH > 0) ? (srcCW * dH / srcCH) : srcCW;

            // GAUGE_NUM_ALIGN: 0=左揃え, 1=中央揃え, 2=右揃え
            // numX は揃えの基準点（左端/中心/右端）として解釈される。
            const int numX = (Config::PLAY_SIDE == 1) ? Config::GAUGE_NUM_X_1P : Config::GAUGE_NUM_X_2P;
            const int numY = gY + Config::GAUGE_NUM_Y_OFFSET;
            const int totalTextW = numLen * dW;
            int drawStartX;
            switch (Config::GAUGE_NUM_ALIGN) {
                case 1:  drawStartX = numX - totalTextW / 2; break; // 中央揃え
                case 2:  drawStartX = numX - totalTextW;     break; // 右揃え
                default: drawStartX = numX;                  break; // 左揃え (0)
            }

            SDL_SetTextureAlphaMod(numTex.texture, 220);
            for (int ci = 0; ci < numLen; ci++) {
                const char c = buf[ci];
                int colIdx = -1;
                if (useDetail) {
                    if (c == '.')      colIdx = 0;          // ドット = インデックス0
                    else if (c >= '0' && c <= '9') colIdx = (c - '0') + 1; // '0'=1, '9'=10
                } else {
                    if (c >= '0' && c <= '9') colIdx = c - '0'; // '0'=0, '9'=9
                }
                if (colIdx < 0) continue;

                SDL_Rect src = { colIdx * srcCW, 0, srcCW, srcCH };
                SDL_Rect dst = { drawStartX + ci * dW, numY, dW, dH };
                SDL_RenderCopy(ren, numTex.texture, &src, &dst);
            }
            SDL_SetTextureAlphaMod(numTex.texture, 255);
        }
    }
    // ────────────────────────────────────────────────────────────────────────
}

void NoteRenderer::renderGaugeUp(SDL_Renderer* ren, double gaugeValue, uint32_t now) {
    if (Config::GAUGE_UP_SHOW == 0 || !texGaugeUp) return;
    bool is2P = (Config::PLAY_SIDE == 2);

    // 初回フレームは基準値だけ設定してスキップ
    if (gaugeUpLastValue_ < 0.0) {
        gaugeUpLastValue_ = gaugeValue;
        return;
    }

    // 2% 区切りをまたいで上昇したらフラッシュ開始
    int curTick  = (int)(gaugeValue       / 2.0);
    int prevTick = (int)(gaugeUpLastValue_ / 2.0);
    if (curTick > prevTick) {
        gaugeUpEndMs_ = now + 500; // 0.5秒
    }
    gaugeUpLastValue_ = gaugeValue;

    if (now >= gaugeUpEndMs_) return; // 未発火 or 終了済み

    // 2f周期(≈33ms)で点滅: 前半16ms=ON, 後半17ms=OFF
    if ((now % 33) >= 16) return;

    int scale = is2P ? Config::GAUGE_UP_SCALE_2P : Config::GAUGE_UP_SCALE_1P;
    int gx    = is2P ? Config::GAUGE_UP_X_2P     : Config::GAUGE_UP_X_1P;
    int gy    = is2P ? Config::GAUGE_UP_Y_2P     : Config::GAUGE_UP_Y_1P;
    int dw = std::max(1, texGaugeUp.w * scale / 100);
    int dh = std::max(1, texGaugeUp.h * scale / 100);
    SDL_Rect dst = { gx, gy, dw, dh };
    SDL_RenderCopy(ren, texGaugeUp.texture, nullptr, &dst);
}

void NoteRenderer::renderLoading(SDL_Renderer* ren, int current, int total, const std::string& filename) {
    SDL_SetRenderDrawColor(ren, 0, 0, 5, 255); SDL_RenderClear(ren);
    // ローディング画面は drawText で構わない（描画頻度が低く、内容も変わる）
    drawText(ren, "NOW LOADING...", 640, 300, {255, 255, 255, 255}, true, true);
    SDL_Rect bO = { 100, 380, 1080, 20 };
    SDL_SetRenderDrawColor(ren, 40, 40, 40, 255); SDL_RenderDrawRect(ren, &bO);
    float p = (total > 0) ? (float)current / total : 0;
    SDL_Rect bI = { 102, 382, (int)(1076 * p), 16 };
    SDL_SetRenderDrawColor(ren, 0, 120, 255, 255); SDL_RenderFillRect(ren, &bI);
    drawText(ren, filename, 640, 420, {150, 150, 150, 255}, false, true);
}

void NoteRenderer::renderResult(SDL_Renderer* ren, const PlayStatus& status,
                                 const BMSHeader& header, const std::string& rank) {
    SDL_SetRenderDrawColor(ren, 5, 5, 10, 255); SDL_RenderClear(ren);
    SDL_Color white  = {255, 255, 255, 255};
    SDL_Color yellow = {255, 255,   0, 255};
    drawTextCached(ren, header.title,       640, 50,  white, true, true);
    drawTextCached(ren, "RANK: " + rank,    640, 120, yellow, true, true);
    int sY = 240, sp = 45;

    // ★修正: drawText(... + std::to_string()) による毎フレームのヒープアロケーションを廃止。
    //        snprintf でスタックバッファに書き、drawTextCached でテクスチャを再利用する。
    //        値はリザルト画面中に変化しないので、drawTextCached のキャッシュが毎フレーム完全ヒットする。
    char pgBuf[48], grBuf[48], gdBuf[48], bdBuf[48], prBuf[48], mcBuf[48], exBuf[48];
    snprintf(pgBuf, sizeof(pgBuf), "P-GREAT : %d", status.pGreatCount);
    snprintf(grBuf, sizeof(grBuf), "GREAT   : %d", status.greatCount);
    snprintf(gdBuf, sizeof(gdBuf), "GOOD    : %d", status.goodCount);
    snprintf(bdBuf, sizeof(bdBuf), "BAD     : %d", status.badCount);
    snprintf(prBuf, sizeof(prBuf), "POOR    : %d", status.poorCount);
    snprintf(mcBuf, sizeof(mcBuf), "MAX COMBO : %d", status.maxCombo);
    snprintf(exBuf, sizeof(exBuf), "EX SCORE  : %d", (status.pGreatCount * 2) + status.greatCount);

    drawTextCached(ren, pgBuf, 400, sY,        white,  false);
    drawTextCached(ren, grBuf, 400, sY + sp,   white,  false);
    drawTextCached(ren, gdBuf, 400, sY + sp*2, white,  false);
    drawTextCached(ren, bdBuf, 400, sY + sp*3, white,  false);
    drawTextCached(ren, prBuf, 400, sY + sp*4, white,  false);
    drawTextCached(ren, mcBuf, 680, sY,        yellow, false);
    drawTextCached(ren, exBuf, 680, sY + sp,   white,  false);

    // ★修正: clearText / clearColor の毎フレーム std::string 構築 + switch 分岐を廃止。
    //        ClearType を整数インデックスとして constexpr テーブルを直接引く。
    //        ヒープアロケーション ゼロ、switch の分岐予測ミスもゼロ、O(1) テーブル引き。
    //        テーブル順は ClearType enum の定義順 (NO_PLAY=0, FAILED=1, ..., FULL_COMBO=8) と一致させる。
    struct ClearDisplay { const char* text; SDL_Color color; };
    static constexpr ClearDisplay CLEAR_TABLE[] = {
        {"NO PLAY",       {100, 100, 100, 255}},  // NO_PLAY
        {"FAILED",        {100, 100, 100, 255}},  // FAILED
        {"ASSIST CLEAR",  {180, 100, 255, 255}},  // ASSIST_CLEAR
        {"EASY CLEAR",    {150, 255, 100, 255}},  // EASY_CLEAR
        {"NORMAL CLEAR",  {  0, 200, 255, 255}},  // NORMAL_CLEAR
        {"HARD CLEAR",    {255,   0,   0, 255}},  // HARD_CLEAR
        {"EX-HARD CLEAR", {255, 255,   0, 255}},  // EX_HARD_CLEAR
        {"DAN CLEAR",     {200,   0, 100, 255}},  // DAN_CLEAR
        {"FULL COMBO",    {255, 255, 255, 255}},  // FULL_COMBO
    };
    int clearIdx = std::clamp((int)status.clearType, 0, (int)(std::size(CLEAR_TABLE) - 1));
    drawTextCached(ren, CLEAR_TABLE[clearIdx].text, 640, 550, CLEAR_TABLE[clearIdx].color, true, true);

    if ((SDL_GetTicks() / 500) % 2 == 0)
        drawTextCached(ren, "PRESS ANY BUTTON TO EXIT", 640, 650, {150, 150, 150, 255}, true, true);
}
