#include "BgaManager.hpp"
#include "Config.hpp"
#include "Logger.hpp"
#include <SDL2/SDL_image.h>
#include <cstring>
#include <algorithm>
#include <cstdio>

static const long long BMP_LOOK_AHEAD = 300000;

// ============================================================
//  init / loadBmp / preLoad
// ============================================================

void BgaManager::init(size_t expectedSize) {
    clear();
    textures.reserve(std::min((size_t)256, expectedSize));
}

void BgaManager::loadBmp(int id, const std::string& fullPath, SDL_Renderer* renderer) {
    if (textures.count(id)) return;
    SDL_Surface* surf = IMG_Load(fullPath.c_str());
    if (!surf) {
        LOG_WARN("BgaManager", "IMG_Load failed: id=%d path='%s' err=%s",
                 id, fullPath.c_str(), IMG_GetError());
        return;
    }

    BgaTextureEntry entry;
    entry.w   = surf->w;
    entry.h   = surf->h;
    entry.tex = SDL_CreateTextureFromSurface(renderer, surf);
    SDL_FreeSurface(surf);
    if (entry.tex) textures[id] = entry;
}

void BgaManager::preLoad(long long startPulse, SDL_Renderer* renderer) {
    if (isVideoMode) return;
    int nextNeededId = -1;
    auto scan = [&](const std::vector<BgaEvent>& events) {
        for (const auto& ev : events) {
            if (ev.y > startPulse + BMP_LOOK_AHEAD) break;
            if (textures.find(ev.id) == textures.end()) {
                nextNeededId = ev.id;
                return true;
            }
        }
        return false;
    };
    if (!scan(bgaEvents)) { if (!scan(layerEvents)) scan(poorEvents); }
    if (nextNeededId != -1 && idToFilename.count(nextNeededId))
        loadBmp(nextNeededId, baseDir + idToFilename[nextNeededId], renderer);
}

// ============================================================
//  loadBgaFile
// ============================================================

bool BgaManager::loadBgaFile(const std::string& path, SDL_Renderer* renderer) {
    LOG_INFO("BgaManager", "loadBgaFile: '%s'", path.c_str());
    std::string targetPath = path;
    if (targetPath.compare(0, 5, "sdmc:") == 0) targetPath.erase(0, 5);

    if (isVideoMode) clear();

    // --- フォーマットを開く (フォールバックパス付き) ---
    int err = avformat_open_input(&pFormatCtx, targetPath.c_str(), NULL, NULL);
    if (err != 0) {
        size_t lastSlash = targetPath.find_last_of("/\\");
        std::string filename = (lastSlash != std::string::npos)
            ? targetPath.substr(lastSlash + 1) : targetPath;
        std::string fallback = Config::ROOT_PATH + "videos/" + filename;
        if (fallback.compare(0, 5, "sdmc:") == 0) fallback.erase(0, 5);
        err = avformat_open_input(&pFormatCtx, fallback.c_str(), NULL, NULL);
    }
    if (err != 0) {
        LOG_WARN("BgaManager", "loadBgaFile: could not open video '%s'", path.c_str());
        return false;
    }
    if (avformat_find_stream_info(pFormatCtx, NULL) < 0) {
        LOG_WARN("BgaManager", "loadBgaFile: find_stream_info failed '%s'", path.c_str());
        avformat_close_input(&pFormatCtx); pFormatCtx = nullptr;
        return false;
    }

    // --- ビデオストリーム検索 ---
    videoStreamIdx = -1;
    for (int i = 0; i < (int)pFormatCtx->nb_streams; i++) {
        if (pFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            videoStreamIdx = i;
            break;
        }
    }
    if (videoStreamIdx == -1) {
        avformat_close_input(&pFormatCtx); pFormatCtx = nullptr;
        return false;
    }

    AVCodecParameters* pCodecPar = pFormatCtx->streams[videoStreamIdx]->codecpar;
    int vW = pCodecPar->width;
    int vH = pCodecPar->height;

    // --- 動画制約チェック ---
    // Switch の CPU/メモリ帯域を守るため、縦 256px・30fps を超える動画は拒否する。
    // BGA は装飾なので品質より安定動作を優先する。
    AVRational avgFps = pFormatCtx->streams[videoStreamIdx]->avg_frame_rate;
    double fps = (avgFps.den > 0) ? (double)avgFps.num / avgFps.den : 30.0;

    if (vH > MAX_VIDEO_HEIGHT) {
        LOG_WARN("BgaManager", "loadBgaFile: rejected — height %d > %d limit '%s'",
                 vH, MAX_VIDEO_HEIGHT, path.c_str());
        avformat_close_input(&pFormatCtx); pFormatCtx = nullptr;
        return false;
    }
    if (fps > (double)MAX_VIDEO_FPS + 0.5) {
        LOG_WARN("BgaManager", "loadBgaFile: rejected — fps %.1f > %d limit '%s'",
                 fps, MAX_VIDEO_FPS, path.c_str());
        avformat_close_input(&pFormatCtx); pFormatCtx = nullptr;
        return false;
    }
    videoFps = fps;

    // 動画のメタ情報をログに残す。
    // 解像度・FPS・コーデックの問題は loadBgaFile の中では起きず
    // デコードスレッド側で無音で失敗するケースが多いため、ここで記録しておくと原因特定が速い。
    const AVCodecDescriptor* desc = avcodec_descriptor_get(pCodecPar->codec_id);
    LOG_INFO("BgaManager", "loadBgaFile: %dx%d %.1ffps codec=%s '%s'",
             vW, vH, fps,
             desc ? desc->name : "unknown",
             path.c_str());

    // --- コーデック初期化 ---
    const AVCodec* pCodec = avcodec_find_decoder(pCodecPar->codec_id);
    if (!pCodec) {
        avformat_close_input(&pFormatCtx); pFormatCtx = nullptr;
        return false;
    }

    pCodecCtx = avcodec_alloc_context3(pCodec);
    avcodec_parameters_to_context(pCodecCtx, pCodecPar);

    // ★ thread_count = 1: FFmpeg 内部スレッドを立てない。
    //    videoWorker スレッドをコア2に固定するため、FFmpeg が追加スレッドを
    //    立てると別コアに侵入してゲームスレッドに干渉する。
    pCodecCtx->thread_count    = 1;
    pCodecCtx->flags2         |= AV_CODEC_FLAG2_FAST;
    pCodecCtx->workaround_bugs = 1;
    // ループフィルタをスキップ: BGA は装飾なので多少ブロックノイズが出ても許容
    // デコード時間を ~15% 削減できる
    pCodecCtx->skip_loop_filter = AVDISCARD_NONREF;

    if (avcodec_open2(pCodecCtx, pCodec, NULL) < 0) {
        avcodec_free_context(&pCodecCtx); pCodecCtx = nullptr;
        avformat_close_input(&pFormatCtx); pFormatCtx = nullptr;
        return false;
    }

    pFrame    = av_frame_alloc();
    videoTexW = vW;
    videoTexH = vH;

    videoTexture = SDL_CreateTexture(renderer,
                                     SDL_PIXELFORMAT_NV12,
                                     SDL_TEXTUREACCESS_STREAMING,
                                     videoTexW, videoTexH);
    if (!videoTexture) {
        LOG_ERROR("BgaManager", "loadBgaFile: SDL_CreateTexture failed %dx%d '%s'",
                  videoTexW, videoTexH, path.c_str());
        avcodec_free_context(&pCodecCtx); pCodecCtx = nullptr;
        avformat_close_input(&pFormatCtx); pFormatCtx = nullptr;
        return false;
    }

    // ★ SPSC スロット事前確保
    // NV12 = Y(w×h) + UV(w×h/2) = w×h×3/2 バイト、ストライド = width (タイトパッキング)
    size_t slotBytes = (size_t)videoTexW * videoTexH * 3 / 2;
    for (int i = 0; i < NUM_SLOTS; i++) {
        slots[i].data.assign(slotBytes, 0);
        slots[i].pts = -1.0;
    }
    qHead.store(0, std::memory_order_relaxed);
    qTail.store(0, std::memory_order_relaxed);

    quitThread.store(false, std::memory_order_relaxed);
    isVideoMode = true;
    isReady.store(false, std::memory_order_release);

    // デコードスレッド起動（スロットサイズをログに残す：OOM時の手がかりになる）
    LOG_INFO("BgaManager", "loadBgaFile: starting decode thread slotBytes=%zuKB x%d",
             slotBytes / 1024, NUM_SLOTS);
    decodeThread = std::thread(&BgaManager::videoWorker, this);
    return true;
}

// ============================================================
//  videoWorker — SPSC Producer、コア2固定 (Switch)
// ============================================================

void BgaManager::videoWorker() {
    // ★ Switch: このスレッド自身をコア2に移動する。
    //    コア0 = Switch OS システム
    //    コア1 = ゲームメインスレッド (音声・入力・描画)
    //    コア2 = BGA デコード ← このスレッド
    //    コア3 = 空き (オーディオドライバが使う場合あり)
    //
    //    svcSetThreadCoreMask の第1引数 -2 は "現在のスレッド" を示す擬似ハンドル。
    //    libnx のバージョンによって CUR_THREAD_HANDLE という定数が使える場合もある。
#ifdef __SWITCH__
    svcSetThreadCoreMask(-2, 2, (1U << 2));
#endif

    const int    w           = videoTexW;
    const int    h           = videoTexH;
    const size_t ySize       = (size_t)w * h;
    const size_t uvSize      = (size_t)w * (h / 2);
    // フレーム間隔の半分をスリープ上限にすることで CPU の無駄食いを防ぐ
    const int    halfFrameMs = (videoFps > 0.0)
                                ? std::max(1, (int)(500.0 / videoFps))
                                : 16;

    // av_packet_alloc/free: FFmpeg 3.1以降の推奨API。av_init_packet は非推奨。
    AVPacket* packet = av_packet_alloc();
    if (!packet) return;

    while (!quitThread.load(std::memory_order_relaxed)) {

        // --- キュー満杯チェック (mutex なし、acquire で tail を読む) ---
        int tail     = qTail.load(std::memory_order_relaxed);
        int nextTail = (tail + 1) % NUM_SLOTS;
        if (nextTail == qHead.load(std::memory_order_acquire)) {
            // キューが満杯 → フレーム間隔の半分だけ待機
            std::this_thread::sleep_for(std::chrono::milliseconds(halfFrameMs));
            continue;
        }

        // --- パケット読み込み ---
        if (av_read_frame(pFormatCtx, packet) < 0) {
            // EOF: BGA は1回再生で止まる (必要ならシークしてループもできる)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        if (packet->stream_index != videoStreamIdx) {
            av_packet_unref(packet);
            continue;
        }

        if (avcodec_send_packet(pCodecCtx, packet) < 0) {
            av_packet_unref(packet);
            continue;
        }
        av_packet_unref(packet);

        // --- フレーム受信ループ (1パケットから複数フレームが出ることがある) ---
        while (!quitThread.load(std::memory_order_relaxed)) {
            int ret = avcodec_receive_frame(pCodecCtx, pFrame);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
            if (ret < 0) break;

            // キュー再チェック (複数フレームデコード時に満杯になることがある)
            tail     = qTail.load(std::memory_order_relaxed);
            nextTail = (tail + 1) % NUM_SLOTS;
            if (nextTail == qHead.load(std::memory_order_acquire)) {
                // 満杯 → このフレームを捨てて次のパケットへ
                break;
            }

            // --- PTS 計算 ---
            int64_t pts = pFrame->best_effort_timestamp;
            if (pts == AV_NOPTS_VALUE) pts = 0;
            int64_t startTime = pFormatCtx->streams[videoStreamIdx]->start_time;
            if (startTime != AV_NOPTS_VALUE) pts -= startTime;
            double frameTime = pts * av_q2d(pFormatCtx->streams[videoStreamIdx]->time_base);

            // --- NV12 変換 → slots[tail].data に直接書き込む ---
            FrameSlot& slot = slots[tail];
            slot.pts = frameTime;

            uint8_t* dstY  = slot.data.data();
            uint8_t* dstUV = dstY + ySize;

            // Y 面コピー
            // ストライドが width に一致する場合は単一 memcpy で最速処理
            if (pFrame->linesize[0] == w) {
                memcpy(dstY, pFrame->data[0], ySize);
            } else {
                for (int r = 0; r < h; r++)
                    memcpy(dstY + r * w, pFrame->data[0] + r * pFrame->linesize[0], w);
            }

            // UV 面: YUV420P (planar U, V) → NV12 (interleaved UV) 変換
            // pFrame->format == AV_PIX_FMT_NV12 の場合は data[1] がすでに UV interleaved
            if (pFrame->format == AV_PIX_FMT_NV12) {
                // デコーダがネイティブ NV12 を出力した場合 — コピーのみ
                if (pFrame->linesize[1] == w) {
                    memcpy(dstUV, pFrame->data[1], uvSize);
                } else {
                    for (int r = 0; r < h / 2; r++)
                        memcpy(dstUV + r * w, pFrame->data[1] + r * pFrame->linesize[1], w);
                }
            } else {
                // YUV420P → NV12: U/V をインターリーブ
                // uint16_t で2バイト同時書き込みにより帯域を節約
                if (pFrame->linesize[1] == w / 2 && pFrame->linesize[2] == w / 2) {
                    // ストライド一致 → 内ループ展開なしで最速
                    const uint8_t* sU   = pFrame->data[1];
                    const uint8_t* sV   = pFrame->data[2];
                    uint16_t*      dUV  = reinterpret_cast<uint16_t*>(dstUV);
                    const size_t   n    = uvSize / 2; // UV ペア数
                    for (size_t j = 0; j < n; j++)
                        dUV[j] = (uint16_t)sU[j] | ((uint16_t)sV[j] << 8);
                } else {
                    for (int r = 0; r < h / 2; r++) {
                        uint16_t*      dUV = reinterpret_cast<uint16_t*>(dstUV + r * w);
                        const uint8_t* sU  = pFrame->data[1] + r * pFrame->linesize[1];
                        const uint8_t* sV  = pFrame->data[2] + r * pFrame->linesize[2];
                        for (int j = 0; j < w / 2; j++)
                            dUV[j] = (uint16_t)sU[j] | ((uint16_t)sV[j] << 8);
                    }
                }
            }

            // ★ SPSC: tail を advance して Consumer に公開する
            qTail.store(nextTail, std::memory_order_release);

            // 最初の1フレームが書けたら準備完了フラグを立てる
            if (!isReady.load(std::memory_order_relaxed))
                isReady.store(true, std::memory_order_release);
        }
    }

    av_packet_free(&packet);
}

// ============================================================
//  syncTime
// ============================================================

void BgaManager::syncTime(double ms) {
    sharedVideoElapsed.store(ms / 1000.0, std::memory_order_release);
}

// ============================================================
//  render — SPSC Consumer、mutex なしのホットパス
// ============================================================

void BgaManager::render(long long currentPulse, SDL_Renderer* renderer, int x, int y, double cur_ms) {
    // デコードスレッドが最初のフレームを書くまで描画スキップ
    if (isVideoMode && !isReady.load(std::memory_order_acquire)) return;

    // BGA 表示中心 X は NoteRenderer::rebuildLaneLayout() が計算した値を
    // setLayout() 経由で受け取って使う。ここで独自にレーン幅を再計算しない。
    int dynamicCenterX = cachedBgaCenterX;

    // 描画サイズ: 高さ512に合わせてアスペクト比を保つ
    int renderH = 512, renderW = 512;
    if (isVideoMode && videoTexture) {
        if (videoTexH > 0) renderW = (int)(512.0f * (float)videoTexW / (float)videoTexH);
    } else if (lastDisplayedId != -1) {
        auto it = textures.find(lastDisplayedId);
        if (it != textures.end() && it->second.h > 0)
            renderW = (int)(512.0f * (float)it->second.w / (float)it->second.h);
    }

    SDL_Rect dst = { dynamicCenterX - renderW / 2,
                     Config::SCREEN_HEIGHT / 2 - renderH / 2,
                     renderW, renderH };

    // --- BGA イベントインデックス更新 ---
    while (currentEventIndex < bgaEvents.size()
           && bgaEvents[currentEventIndex].y <= currentPulse) {
        lastDisplayedId = bgaEvents[currentEventIndex++].id;
    }
    while (currentLayerIndex < layerEvents.size()
           && layerEvents[currentLayerIndex].y <= currentPulse) {
        lastLayerId = layerEvents[currentLayerIndex++].id;
    }
    while (currentPoorIndex < poorEvents.size()
           && poorEvents[currentPoorIndex].y <= currentPulse) {
        lastPoorId = poorEvents[currentPoorIndex++].id;
    }

    // --- ビデオフレームアップロード (ロックフリー) ---
    if (isVideoMode && videoTexture) {
        double currentTime = sharedVideoElapsed.load(std::memory_order_acquire);

        // ★ SPSC Consumer:
        //    [head, tail) の範囲にある "pts <= currentTime" のフレームのうち
        //    最も新しいもの (= tail に一番近いもの) を探して表示する。
        //    それより古いフレームは head を advance することでスロットを解放し、
        //    Worker が再利用できるようにする。
        //
        //    PTS は単調増加なので、head から scan して pts > currentTime になったら即 break。
        //    最後に見つかった bestIdx が今表示すべきフレーム。

        int head    = qHead.load(std::memory_order_relaxed);
        int tail    = qTail.load(std::memory_order_acquire);
        int bestIdx = -1;
        int scanIdx = head;

        while (scanIdx != tail) {
            if (slots[scanIdx].pts <= currentTime + 0.001) { // 浮動小数点誤差を微量許容
                bestIdx = scanIdx;
                scanIdx = (scanIdx + 1) % NUM_SLOTS;
            } else {
                break; // PTS は単調増加 → 以降は必ず未来フレーム
            }
        }

        if (bestIdx >= 0) {
            // bestIdx のデータを SDL テクスチャへアップロードしてから head を advance する。
            // 順序が逆だと Worker がまだ読み中のスロットを上書きするリスクがある。
            const uint8_t* src    = slots[bestIdx].data.data();
            const size_t   yBytes = (size_t)videoTexW * videoTexH;
            const size_t   uvBytes = yBytes / 2;

            void* pixels; int pitch;
            if (SDL_LockTexture(videoTexture, NULL, &pixels, &pitch) == 0) {
                uint8_t* yDst  = (uint8_t*)pixels;
                uint8_t* uvDst = yDst + (ptrdiff_t)pitch * videoTexH;

                if (pitch == videoTexW) {
                    // ★ ストライド一致 → Y / UV それぞれ1回の memcpy で完了
                    //    行ごとループを廃止することで Switch の NEON 最適化が効きやすくなる
                    memcpy(yDst,  src,          yBytes);
                    memcpy(uvDst, src + yBytes, uvBytes);
                } else {
                    // ストライド不一致 (まれ) → 行ごとコピー
                    for (int r = 0; r < videoTexH; r++)
                        memcpy(yDst + (ptrdiff_t)r * pitch, src + r * videoTexW, videoTexW);
                    const uint8_t* uvSrc = src + yBytes;
                    for (int r = 0; r < videoTexH / 2; r++)
                        memcpy(uvDst + (ptrdiff_t)r * pitch, uvSrc + r * videoTexW, videoTexW);
                }
                SDL_UnlockTexture(videoTexture);
            }

            // アップロード完了後に head を advance → Worker がスロットを再利用できる
            qHead.store((bestIdx + 1) % NUM_SLOTS, std::memory_order_release);
        }

        SDL_RenderCopy(renderer, videoTexture, NULL, &dst);

    } else {
        // BMP/PNG モード
        if (lastDisplayedId != -1) {
            auto it = textures.find(lastDisplayedId);
            if (it != textures.end() && it->second.tex)
                SDL_RenderCopy(renderer, it->second.tex, NULL, &dst);
        }
    }

    // レイヤー・ミス画像
    if (lastLayerId != -1) {
        auto it = textures.find(lastLayerId);
        if (it != textures.end() && it->second.tex)
            SDL_RenderCopy(renderer, it->second.tex, NULL, &dst);
    }
    if (showPoor && lastPoorId != -1) {
        auto it = textures.find(lastPoorId);
        if (it != textures.end() && it->second.tex)
            SDL_RenderCopy(renderer, it->second.tex, NULL, &dst);
    }
}

// ============================================================
//  clear / cleanup
// ============================================================

void BgaManager::clear() {
    // ① デコードスレッドを停止する
    quitThread.store(true, std::memory_order_release);
    if (decodeThread.joinable()) decodeThread.join();

    // ② テクスチャ解放
    for (auto& pair : textures) if (pair.second.tex) SDL_DestroyTexture(pair.second.tex);
    textures.clear();
    if (videoTexture) { SDL_DestroyTexture(videoTexture); videoTexture = nullptr; }
    videoTexW = 0; videoTexH = 0;

    // ③ FFmpeg コンテキスト解放
    if (pFrame)     { av_frame_free(&pFrame);            pFrame     = nullptr; }
    if (pCodecCtx)  { avcodec_free_context(&pCodecCtx); pCodecCtx  = nullptr; }
    if (pFormatCtx) { avformat_close_input(&pFormatCtx); pFormatCtx = nullptr; }

    // ④ SPSC スロット解放 (shrink_to_fit でメモリを OS に返す)
    for (int i = 0; i < NUM_SLOTS; i++) {
        slots[i].data.clear();
        slots[i].data.shrink_to_fit();
        slots[i].pts = -1.0;
    }
    qHead.store(0, std::memory_order_relaxed);
    qTail.store(0, std::memory_order_relaxed);

    // ⑤ 状態リセット
    isVideoMode = false;
    isReady.store(false, std::memory_order_release);
    quitThread.store(false, std::memory_order_relaxed);
    videoFps = 30.0;

    currentEventIndex = 0; currentLayerIndex = 0; currentPoorIndex = 0;
    lastDisplayedId   = -1; lastLayerId = -1; lastPoorId = -1;
}

void BgaManager::cleanup() { clear(); }





