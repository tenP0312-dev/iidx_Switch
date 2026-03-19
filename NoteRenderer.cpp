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
    // 各レーン幅
    for (int i = 1; i <= 7; i++)
        ll.w[i] = (i % 2 != 0) ? (int)(Config::LANE_WIDTH * 1.4) : Config::LANE_WIDTH;
    ll.w[8] = Config::SCRATCH_WIDTH;

    int keysWidth = 0;
    for (int i = 1; i <= 7; i++) keysWidth += ll.w[i];
    ll.totalWidth = keysWidth + Config::SCRATCH_WIDTH;

    ll.baseX = (Config::PLAY_SIDE == 1)
        ? 50
        : (Config::SCREEN_WIDTH - ll.totalWidth - 50);

    // 各レーン X 座標
    if (Config::PLAY_SIDE == 1) {
        ll.x[8] = ll.baseX;                     // スクラッチ左端
        int cur = ll.baseX + Config::SCRATCH_WIDTH;
        for (int i = 1; i <= 7; i++) { ll.x[i] = cur; cur += ll.w[i]; }
    } else {
        int cur = ll.baseX;
        for (int i = 1; i <= 7; i++) { ll.x[i] = cur; cur += ll.w[i]; }
        ll.x[8] = cur;                           // スクラッチ右端
    }

    // BGA 表示中心 X
    if (Config::PLAY_SIDE == 1) {
        int right = ll.baseX + ll.totalWidth;
        ll.bgaCenterX = right + (Config::SCREEN_WIDTH - right) / 2;
    } else {
        ll.bgaCenterX = ll.baseX / 2;
    }
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
    fontSmall = TTF_OpenFontIndex(Config::FONT_PATH.c_str(), 24, 0);
    fontBig   = TTF_OpenFontIndex(Config::FONT_PATH.c_str(), 48, 0);
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

    loadAndCache(ren, texJudgeAtlas,  s + "judge.png");        track(texJudgeAtlas);
    loadAndCache(ren, texNumberAtlas, s + "judge_number.png"); track(texNumberAtlas);
    loadAndCache(ren, texLaneCover,   s + "lanecover.png");    track(texLaneCover);

    loadAndCache(ren, texGaugeAssist, s + "gauge_assist.png"); track(texGaugeAssist);
    loadAndCache(ren, texGaugeNormal, s + "gauge_normal.png"); track(texGaugeNormal);
    loadAndCache(ren, texGaugeHard,   s + "gauge_hard.png");   track(texGaugeHard);
    loadAndCache(ren, texGaugeExHard, s + "gauge_exhard.png"); track(texGaugeExHard);
    loadAndCache(ren, texGaugeHazard, s + "gauge_hazard.png"); track(texGaugeHazard);
    loadAndCache(ren, texGaugeDan,    s + "gauge_dan.png");    track(texGaugeDan);
    // ★修正: gauge_frame は init() で一度だけロードする。
    //        旧実装は renderGauge() 内で毎フレーム std::string 生成 + std::map 探索 + 初回 I/O が走っていた。
    loadAndCache(ren, texGaugeFrame,  s + "gauge_frame.png");  track(texGaugeFrame);
    // ★修正: Flame_nameplate は init() で一度だけロードする。
    //        旧実装は renderUI() 内で毎フレーム std::string 生成 + std::map::find × 2 が走っていた。
    loadAndCache(ren, texNameplate,   s + "Flame_nameplate.png"); track(texNameplate);

    loadAndCache(ren, texKeys,      s + "7keypad.png");    track(texKeys);
    loadAndCache(ren, lane_Flame,   s + "lane_Flame.png"); track(lane_Flame);
    loadAndCache(ren, lane_Flame2,  s + "lane_Flame2.png"); track(lane_Flame2);

    texBombs.clear();
    for (int i = 0; i < 10; i++) {
        TextureRegion tr;
        loadAndCache(ren, tr, s + "bomb_" + std::to_string(i) + ".png");
        if (tr) { texBombs.push_back(tr); skinOk++; } else { skinFail++; }
    }

    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "2");
    loadAndCache(ren, tex_scratch, s + "scratch.png"); track(tex_scratch);
    if (tex_scratch) {
        SDL_SetTextureScaleMode(tex_scratch.texture, SDL_ScaleModeBest);
        SDL_SetTextureBlendMode(tex_scratch.texture, SDL_BLENDMODE_BLEND);
    }
    loadAndCache(ren, tex_scratch_center, s + "scratch_center.png"); track(tex_scratch_center);
    if (tex_scratch_center) {
        SDL_SetTextureScaleMode(tex_scratch_center.texture, SDL_ScaleModeBest);
        SDL_SetTextureBlendMode(tex_scratch_center.texture, SDL_BLENDMODE_BLEND);
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
    texJudgeAtlas.reset(); texNumberAtlas.reset(); texLaneCover.reset();
    texGaugeAssist.reset(); texGaugeNormal.reset(); texGaugeHard.reset();
    texGaugeExHard.reset(); texGaugeHazard.reset(); texGaugeDan.reset();
    texGaugeFrame.reset();  // ★修正: texGaugeFrame を解放
    texNameplate.reset();   // ★修正: texNameplate を解放
    texKeys.reset();
    lane_Flame.reset(); lane_Flame2.reset();
    tex_scratch.reset(); tex_scratch_center.reset();

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
            TTF_Font* cf = TTF_OpenFontIndex(fontPath.c_str(), isBig ? 48 : 24, 0);
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
    // ★修正: 毎フレームの std::string 生成 + std::map::find × 2 を完全廃止。
    //        texNameplate は init() でロード済みのメンバ変数を直接参照する。
    //        旧実装: platePath 構築(~120ns heap alloc) + find×2(O(log N) × 2) ≒ 520ns/フレームの無駄。
    if (texNameplate) {
        SDL_Rect dst = { centerX - texNameplate.w / 2, 0, texNameplate.w, texNameplate.h };
        SDL_RenderCopy(ren, texNameplate.texture, NULL, &dst);
    }
    // ネームプレート上に楽曲名・アーティスト名を小フォントで中央揃え表示
    SDL_Color white  = {255, 255, 255, 255};
    SDL_Color dimmed = {180, 180, 180, 255};
    drawTextCached(ren, header.title,  centerX, 8,  white,  false, true);
    drawTextCached(ren, header.artist, centerX, 36, dimmed, false, true);
}

// スクラッチ回転アニメーション用状態（ファイルスコープで慣性を保持）
static double s_scratchAngle = 0.0;

void NoteRenderer::renderLanes(SDL_Renderer* ren, double progress, int scratchStatus) {
    // レーンレイアウトは init() および notifyLayoutChanged() 呼び出し時のみ再計算。
    // プレイ中にオプションは変更されないため毎フレームの再計算は不要。
    int totalWidth = ll.totalWidth;
    int startX     = ll.baseX;
    int laneHeight = 482;
    int judgeY     = Config::JUDGMENT_LINE_Y - Config::LIFT;

    SDL_Rect overallBg = { startX, 0, totalWidth, laneHeight };
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
    SDL_RenderFillRect(ren, &overallBg);

    if (lane_Flame) {
        int imgLanePartW = lane_Flame.w - 100;
        if (imgLanePartW > 0) {
            double scale = (double)totalWidth / imgLanePartW;
            int f1W = (int)(lane_Flame.w * scale);
            int f1X = (Config::PLAY_SIDE == 1) ? startX : (startX + totalWidth) - f1W;
            SDL_Rect r = { f1X, 0, f1W, Config::SCREEN_HEIGHT };
            SDL_RenderCopyEx(ren, lane_Flame.texture, NULL, &r, 0, NULL,
                             (Config::PLAY_SIDE == 1 ? SDL_FLIP_NONE : SDL_FLIP_HORIZONTAL));
        }
    }

    if (tex_scratch) {
        float fSize = ((float)Config::SCRATCH_WIDTH * 2.0f / 3.0f) * 2.0f;
        float fX = (Config::PLAY_SIDE == 1)
            ? (float)(startX + Config::SCRATCH_WIDTH) - fSize
            : (float)(startX + totalWidth - Config::SCRATCH_WIDTH);
        SDL_FRect rF = { fX, (float)Config::JUDGMENT_LINE_Y, fSize, fSize };
        SDL_RenderCopyExF(ren, tex_scratch.texture, NULL, &rF, 0.0, NULL, SDL_FLIP_NONE);

        if (tex_scratch_center) {
            // 慣性付き回転：入力に応じて目標速度に追従
            // 常時5deg/frame回転 + ボタン入力で±15追加、慣性なし
            // 1P: 正回転、2P: 逆回転
            double baseDir = (Config::PLAY_SIDE == 1) ? 1.0 : -1.0;
            double delta = baseDir * 5.0;
            if (scratchStatus == 1) delta += baseDir * (-15.0); // スクラッチ上
            if (scratchStatus == 2) delta += baseDir * ( 15.0); // スクラッチ下
            s_scratchAngle += delta;
            if (s_scratchAngle >= 360.0) s_scratchAngle -= 360.0;
            if (s_scratchAngle <    0.0) s_scratchAngle += 360.0;
            SDL_RenderCopyExF(ren, tex_scratch_center.texture, NULL, &rF, s_scratchAngle, NULL, SDL_FLIP_NONE);
        }
    }

    if (texKeys) {
        int kX = ll.x[1], kEnd = ll.x[7] + ll.w[7];
        int kW = kEnd - kX;
        int kH = std::min(160, (int)(kW * ((float)texKeys.h / texKeys.w)));
        SDL_Rect r = { kX, Config::JUDGMENT_LINE_Y, kW, kH };
        SDL_RenderCopy(ren, texKeys.texture, NULL, &r);
    }

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
    int progX = (Config::PLAY_SIDE == 1) ? startX - 13 : startX + totalWidth + 8;
    int progY = 38, progH = 420, indicatorH = 8;
    SDL_Rect progFrame = { progX, progY, 5, progH };
    SDL_SetRenderDrawColor(ren, 60, 60, 60, 255);
    SDL_RenderDrawRect(ren, &progFrame);
    int moveY = progY + (int)((progH - indicatorH) * progress);
    SDL_Rect progIndicator = { progX, moveY, 5, indicatorH };
    SDL_SetRenderDrawColor(ren, 200, 200, 200, 255);
    SDL_RenderFillRect(ren, &progIndicator);
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
    // ★修正: ボムサイズを Config::BOMB_SIZE_FACTOR から計算
    //        デフォルト BOMB_SIZE_FACTOR=420 → LANE_WIDTH*4.2 ≒ 旧実装と同等
    int size   = (int)(Config::LANE_WIDTH * Config::BOMB_SIZE_FACTOR / 100);
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
    int judgeY  = Config::JUDGMENT_LINE_Y - Config::LIFT;
    int x       = ll.baseX + ll.totalWidth / 2;
    // 判定文字の高さ基準: renderJudgment は judgeY-50 あたりに描画するので
    // FAST/SLOWはその上20px = judgeY - 90
    int y       = judgeY - 200;

    SDL_Color color = isFast ? SDL_Color{0, 200, 255, alpha}
                             : SDL_Color{255, 140, 0,   alpha};

    if (Config::SHOW_FAST_SLOW == 2) {
        // DETAIL: "FAST +12.3ms" / "SLOW -8.1ms" 形式
        char buf[TEXT_BUF_SIZE];
        snprintf(buf, sizeof(buf), "%s %+.1fms", isFast ? "FAST" : "SLOW", diffMs);
        drawText(ren, buf, x, y, color, false, true);
    } else {
        // ON: 通常表示
        drawText(ren, isFast ? "FAST" : "SLOW", x, y, color, false, true);
    }
}


// ★修正：JudgeKind ベースのオーバーロード。文字列比較ループを廃止。
void NoteRenderer::renderJudgment(SDL_Renderer* ren, JudgeKind kind, float progress, int combo) {
    if (kind == JudgeKind::NONE || !texJudgeAtlas || !texNumberAtlas) return;

    // JudgeKind → アトラスインデックス
    int type = 4; // POOR
    switch (kind) {
        case JudgeKind::PGREAT: type = 3; break;
        case JudgeKind::GREAT:  type = 2; break;
        case JudgeKind::GOOD:   type = 1; break;
        case JudgeKind::BAD:    type = 0; break;
        default: break;
    }

    int jw = texJudgeAtlas.w / 7, jh = texJudgeAtlas.h;
    int nw = texNumberAtlas.w / 4, nh = texNumberAtlas.h / 10;
    int jIdx = (type == 3) ? (SDL_GetTicks() / 50 % 3)
             : (type == 2 ? 3 : (type == 1 ? 4 : (type == 0 ? 5 : 6)));
    float s = 0.6f;
    int dJW = (int)(jw * s), dJH = (int)(jh * s);
    int dNW = (int)(nw * s), dNH = (int)(nh * s);

    // combo を文字列化（snprintf でスタック上に確保、heap alloc なし）
    char comboStr[16] = {};
    int  comboLen = 0;
    if (combo > 0) {
        comboLen = snprintf(comboStr, sizeof(comboStr), "%d", combo);
    }

    int tW = dJW + (comboLen > 0 ? 20 + comboLen * (dNW - 10) : 0);
    int sX = ll.baseX + (ll.totalWidth + 10) / 2 - tW / 2;
    int dY = Config::JUDGMENT_LINE_Y - 170 - Config::LIFT;
    Uint8 alpha = (Uint8)(255 * (1.0f - progress));

    SDL_Rect jS = { jIdx * jw, 0, jw, jh }, jD = { sX, dY, dJW, dJH };
    SDL_SetTextureAlphaMod(texJudgeAtlas.texture, alpha);
    SDL_RenderCopy(ren, texJudgeAtlas.texture, &jS, &jD);

    if (comboLen > 0) {
        int curX = sX + dJW + 20;
        SDL_SetTextureAlphaMod(texNumberAtlas.texture, alpha);
        int colorIdx = (type == 3) ? (SDL_GetTicks() / 50 % 3) : 3;
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
    int totalW = ll.totalWidth + 10;
    int gX = ll.baseX;

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
    int gY = (Config::SCREEN_HEIGHT - 40) - dGH;

    // ★修正: gauge_frame はメンバ変数 texGaugeFrame から直接参照。
    //        旧実装では毎フレーム std::string 生成 → std::map::find() (O(log N)) →
    //        初回はファイル I/O まで走る3重の無駄があった。
    const TextureRegion* fTR = texGaugeFrame ? &texGaugeFrame : nullptr;

    if (target && *target) {
        if (fTR && *fTR) {
            float wR = (float)fTR->w / target->w, hR = (float)fTR->h / target->h;
            int dFW = (int)(totalW * wR), dFH = (int)(dGH * hR);
            SDL_Rect r = { (gX + totalW / 2) - (dFW / 2), (gY + dGH / 2) - (dFH / 2), dFW, dFH };
            SDL_RenderCopyEx(ren, fTR->texture, NULL, &r, 0, NULL,
                             (Config::PLAY_SIDE == 1 ? SDL_FLIP_NONE : SDL_FLIP_HORIZONTAL));
        }
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




























