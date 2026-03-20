#include "BgaManager.hpp"
#include "Config.hpp"
#include "Logger.hpp"
#include <SDL2/SDL_image.h>
#include <cstring>
#include <algorithm>
#include <cstdio>

static const long long BMP_LOOK_AHEAD = 300000;

// ============================================================
//  FFmpegAllocator — 静的メンバ定義
// ============================================================

alignas(32) uint8_t        FFmpegAllocator::heap[FFmpegAllocator::FFMPEG_HEAP_SIZE];
size_t                     FFmpegAllocator::heapPos   = 0;
bool                       FFmpegAllocator::installed = false;
FFmpegAllocator::BlockHeader* FFmpegAllocator::freeLists[FFmpegAllocator::N_BINS] = {};

// ------------------------------------------------------------
//  ffmpegMalloc
//
//  1. サイズが MAX_BIN_SIZE 以下なら対応ビンのフリーリストを探す。
//     ヒットしたらそのブロックを返す (O(1))。
//  2. フリーリストが空 or サイズ超過ならバンプ確保する。
//  3. アリーナ枯渇時のみ ::malloc フォールバック。
// ------------------------------------------------------------
void* FFmpegAllocator::ffmpegMalloc(size_t size, void* /*opaque*/) {
    if (size == 0) size = 1;

    // ---- フリーリスト検索 (小サイズのみ) ----
    if (size <= MAX_BIN_SIZE) {
        size_t idx = binIdx(size);
        // idx が N_BINS を超える場合のガード (alignUp後に MAX_BIN_SIZE ちょうどになるケース等)
        if (idx < N_BINS && freeLists[idx]) {
            BlockHeader* blk = freeLists[idx];
            freeLists[idx] = blk->nextFree;
            blk->nextFree  = nullptr;
            // ペイロードはゼロクリアしない (FFmpegが自分で初期化する)
            return reinterpret_cast<uint8_t*>(blk) + HDR;
        }
    }

    // ---- バンプ確保 ----
    // 実際に確保するペイロードサイズは ALIGN に切り上げる
    size_t payloadAligned = alignUp(size);
    size_t need = HDR + payloadAligned;
    if (heapPos + need > FFMPEG_HEAP_SIZE) {
        LOG_ERROR("FFmpegAllocator",
                  "arena full: need=%zu pos=%zu cap=%zu — falling back to malloc",
                  need, heapPos, FFMPEG_HEAP_SIZE);
        return ::malloc(size);
    }
    uint8_t* block = heap + heapPos;
    heapPos += need;

    auto* hdr     = reinterpret_cast<BlockHeader*>(block);
    hdr->size     = payloadAligned; // 切り上げ後のサイズを保存 (free 時にビン計算で使う)
    hdr->nextFree = nullptr;
    return block + HDR;
}

// ------------------------------------------------------------
//  ffmpegRealloc
//
//  アリーナ内のポインタ: 新規確保 + memcpy + 旧ブロックをフリーリストに戻す。
//  アリーナ外 (フォールバック malloc 由来): ::realloc に委譲。
// ------------------------------------------------------------
void* FFmpegAllocator::ffmpegRealloc(void* ptr, size_t size, void* opaque) {
    if (!ptr)      return ffmpegMalloc(size, opaque);
    if (size == 0) { ffmpegFree(ptr, opaque); return nullptr; }

    uint8_t* p = static_cast<uint8_t*>(ptr);
    if (p < heap || p >= heap + FFMPEG_HEAP_SIZE) {
        return ::realloc(ptr, size);
    }

    auto* hdr = reinterpret_cast<BlockHeader*>(p - HDR);
    // サイズが同じか小さい場合はそのまま返す (FFmpegではよくある)
    if (size <= hdr->size) return ptr;

    void* newPtr = ffmpegMalloc(size, opaque);
    if (!newPtr) return nullptr;
    memcpy(newPtr, ptr, hdr->size);
    ffmpegFree(ptr, opaque); // 旧ブロックをフリーリストに返す
    return newPtr;
}

// ------------------------------------------------------------
//  ffmpegFree
//
//  アリーナ内のポインタ:
//    MAX_BIN_SIZE 以下なら対応ビンのフリーリストに積む。
//    超過サイズは「穴」として放棄 (reset() で一括回収)。
//  アリーナ外 (フォールバック malloc 由来): ::free する。
// ------------------------------------------------------------
void FFmpegAllocator::ffmpegFree(void* ptr, void* /*opaque*/) {
    if (!ptr) return;
    uint8_t* p = static_cast<uint8_t*>(ptr);
    if (p < heap || p >= heap + FFMPEG_HEAP_SIZE) {
        ::free(ptr);
        return;
    }

    auto* hdr = reinterpret_cast<BlockHeader*>(p - HDR);
    if (hdr->size <= MAX_BIN_SIZE) {
        size_t idx = binIdx(hdr->size);
        if (idx < N_BINS) {
            // フリーリストの先頭に積む (LIFO: キャッシュ効率が良い)
            hdr->nextFree  = freeLists[idx];
            freeLists[idx] = hdr;
            return;
        }
    }
    // 大きなブロックは放棄 (reset() まで穴になる)
}

// ------------------------------------------------------------
//  av_malloc / av_realloc / av_free — FFmpeg シンボル上書き
//
//  FFmpeg の libavutil は av_malloc / av_free を通常シンボルで export する。
//  同名の関数をこのファイルで定義すると、リンカが「強いシンボル」として
//  優先し、FFmpeg 内部のアロケーションが全てここに来る。
//  Makefile に -Wl,--allow-multiple-definition が必要。
// ------------------------------------------------------------
extern "C" {

void* av_malloc(size_t size) {
    return FFmpegAllocator::ffmpegMalloc(size, nullptr);
}

void* av_realloc(void* ptr, size_t size) {
    return FFmpegAllocator::ffmpegRealloc(ptr, size, nullptr);
}

void av_free(void* ptr) {
    FFmpegAllocator::ffmpegFree(ptr, nullptr);
}

void av_freep(void* arg) {
    void** ptr = static_cast<void**>(arg);
    if (ptr && *ptr) {
        FFmpegAllocator::ffmpegFree(*ptr, nullptr);
        *ptr = nullptr;
    }
}

} // extern "C"

// ------------------------------------------------------------
//  install / uninstall / reset
// ------------------------------------------------------------
void FFmpegAllocator::install() {
    if (installed) return;
    installed = true;
    LOG_INFO("FFmpegAllocator",
             "installed (freelist arena): size=%zuKB bins=%zu maxBin=%zu @ %p",
             FFMPEG_HEAP_SIZE / 1024, N_BINS, MAX_BIN_SIZE, (void*)heap);
}

void FFmpegAllocator::uninstall() {
    LOG_INFO("FFmpegAllocator", "uninstall: no-op (symbol override method)");
}

void FFmpegAllocator::reset() {
    LOG_INFO("FFmpegAllocator", "reset: freed %zuKB (pos=%zu)", heapPos / 1024, heapPos);
    heapPos = 0;
    // フリーリストもクリア (アリーナが先頭に戻るのでどうせ無効になる)
    for (size_t i = 0; i < N_BINS; i++) freeLists[i] = nullptr;
}

size_t FFmpegAllocator::usedBytes() { return heapPos; }

// ============================================================
//  init / loadBmp / preLoad
// ============================================================

void BgaManager::init(size_t expectedSize) {
    FFmpegAllocator::install();
    clear();
    textures.reserve(std::min((size_t)256, expectedSize));
}

void BgaManager::loadBmp(int id, const std::string& fullPath, SDL_Renderer* renderer) {
    SDL_Surface* surf = IMG_Load(fullPath.c_str());
    if (!surf) {
        LOG_WARN("BgaManager", "IMG_Load failed: id=%d path='%s' err=%s",
                 id, fullPath.c_str(), IMG_GetError());
        return;
    }
    SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surf);
    int w = surf->w, h = surf->h;
    SDL_FreeSurface(surf);
    if (tex) textures[id] = { tex, w, h };
}

void BgaManager::preLoad(long long startPulse, SDL_Renderer* renderer) {
    for (const auto& ev : bgaEvents) {
        if (ev.y < startPulse - BMP_LOOK_AHEAD) continue;
        if (ev.y > startPulse + BMP_LOOK_AHEAD) break;
        if (textures.count(ev.id)) continue;
        auto it = idToFilename.find(ev.id);
        if (it == idToFilename.end()) continue;
        loadBmp(ev.id, baseDir + it->second, renderer);
    }
}

// ============================================================
//  loadBgaFile
// ============================================================

bool BgaManager::loadBgaFile(const std::string& path, SDL_Renderer* renderer) {
    LOG_INFO("BgaManager", "loadBgaFile: '%s'", path.c_str());
    std::string targetPath = path;
    if (targetPath.compare(0, 5, "sdmc:") == 0) targetPath.erase(0, 5);

    if (isVideoMode) clear();

    static const std::vector<std::string> VIDEO_EXTS = {
        ".mp4", ".MP4", ".wmv", ".WMV", ".avi", ".AVI",
        ".mov", ".MOV", ".m4v", ".M4V", ".mpg", ".MPG", ".mpeg", ".MPEG"
    };

    auto getStem = [](const std::string& p) -> std::string {
        size_t slash = p.find_last_of("/\\");
        std::string fname = (slash != std::string::npos) ? p.substr(slash + 1) : p;
        size_t dot = fname.find_last_of('.');
        return (dot != std::string::npos) ? fname.substr(0, dot) : fname;
    };
    auto getDir = [](const std::string& p) -> std::string {
        size_t slash = p.find_last_of("/\\");
        return (slash != std::string::npos) ? p.substr(0, slash + 1) : "";
    };
    auto getFilename = [](const std::string& p) -> std::string {
        size_t slash = p.find_last_of("/\\");
        return (slash != std::string::npos) ? p.substr(slash + 1) : p;
    };

    std::vector<std::string> candidates;
    candidates.push_back(targetPath);
    std::string dir      = getDir(targetPath);
    std::string stem     = getStem(targetPath);
    std::string filename = getFilename(targetPath);
    std::string videosDir = Config::ROOT_PATH + "videos/";
    for (const auto& ext : VIDEO_EXTS) {
        std::string candidate = dir + stem + ext;
        if (candidate != targetPath) candidates.push_back(candidate);
    }
    candidates.push_back(videosDir + filename);
    for (const auto& ext : VIDEO_EXTS) {
        candidates.push_back(videosDir + stem + ext);
    }

    int err = -1;
    std::string resolvedPath;
    for (const auto& cand : candidates) {
        std::string p = cand;
        if (p.compare(0, 5, "sdmc:") == 0) p.erase(0, 5);
        err = avformat_open_input(&pFormatCtx, p.c_str(), NULL, NULL);
        if (err == 0) {
            resolvedPath = p;
            if (p != targetPath)
                LOG_INFO("BgaManager", "loadBgaFile: resolved to '%s'", p.c_str());
            break;
        }
    }
    if (err != 0) {
        LOG_WARN("BgaManager", "loadBgaFile: could not open video '%s'", path.c_str());
        return false;
    }
    if (avformat_find_stream_info(pFormatCtx, NULL) < 0) {
        LOG_WARN("BgaManager", "loadBgaFile: find_stream_info failed '%s'", resolvedPath.c_str());
        avformat_close_input(&pFormatCtx); pFormatCtx = nullptr;
        return false;
    }

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

    const AVCodecDescriptor* desc = avcodec_descriptor_get(pCodecPar->codec_id);
    LOG_INFO("BgaManager", "loadBgaFile: %dx%d %.1ffps codec=%s '%s'",
             vW, vH, fps, desc ? desc->name : "unknown", path.c_str());

    const AVCodec* pCodec = avcodec_find_decoder(pCodecPar->codec_id);
    if (!pCodec) {
        avformat_close_input(&pFormatCtx); pFormatCtx = nullptr;
        return false;
    }

    pCodecCtx = avcodec_alloc_context3(pCodec);
    avcodec_parameters_to_context(pCodecCtx, pCodecPar);
    pCodecCtx->thread_count     = 1;
    pCodecCtx->flags2          |= AV_CODEC_FLAG2_FAST;
    pCodecCtx->workaround_bugs  = 1;
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

    size_t slotBytes = (size_t)videoTexW * videoTexH * 3 / 2;
    for (int i = 0; i < NUM_SLOTS; i++) {
        slots[i].data.resize(slotBytes, 0);
        slots[i].pts = -1.0;
    }
    qHead.store(0, std::memory_order_relaxed);
    qTail.store(0, std::memory_order_relaxed);

    quitThread.store(false, std::memory_order_relaxed);
    isVideoMode = true;
    isReady.store(false, std::memory_order_release);

    LOG_INFO("BgaManager", "loadBgaFile: starting decode thread slotBytes=%zuKB x%d arena=%zuKB",
             slotBytes / 1024, NUM_SLOTS, FFmpegAllocator::usedBytes() / 1024);
    decodeThread = std::thread(&BgaManager::videoWorker, this);
    return true;
}

// ============================================================
//  videoWorker — SPSC Producer、コア2固定 (Switch)
// ============================================================

void BgaManager::videoWorker() {
#ifdef __SWITCH__
    svcSetThreadCoreMask(-2, 2, (1U << 2));
#endif

    const int    w           = videoTexW;
    const int    h           = videoTexH;
    const size_t ySize       = (size_t)w * h;
    const size_t uvSize      = (size_t)w * (h / 2);
    const int    halfFrameMs = (videoFps > 0.0)
                                ? std::max(1, (int)(500.0 / videoFps))
                                : 16;

    AVPacket* packet = av_packet_alloc();
    if (!packet) return;

    while (!quitThread.load(std::memory_order_relaxed)) {

        int tail     = qTail.load(std::memory_order_relaxed);
        int nextTail = (tail + 1) % NUM_SLOTS;
        if (nextTail == qHead.load(std::memory_order_acquire)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(halfFrameMs));
            continue;
        }

        if (av_read_frame(pFormatCtx, packet) < 0) {
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

        while (!quitThread.load(std::memory_order_relaxed)) {
            int ret = avcodec_receive_frame(pCodecCtx, pFrame);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
            if (ret < 0) break;

            tail     = qTail.load(std::memory_order_relaxed);
            nextTail = (tail + 1) % NUM_SLOTS;
            if (nextTail == qHead.load(std::memory_order_acquire)) break;

            int64_t pts = pFrame->best_effort_timestamp;
            if (pts == AV_NOPTS_VALUE) pts = 0;
            int64_t startTime = pFormatCtx->streams[videoStreamIdx]->start_time;
            if (startTime != AV_NOPTS_VALUE) pts -= startTime;
            double frameTime = pts * av_q2d(pFormatCtx->streams[videoStreamIdx]->time_base);

            {
                const double maxLookAhead = (videoFps > 0.0) ? (1.0 / videoFps) * 2.0 : 0.1;
                double currentElapsed = sharedVideoElapsed.load(std::memory_order_acquire);
                while (!quitThread.load(std::memory_order_relaxed) &&
                       frameTime > currentElapsed + maxLookAhead) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(halfFrameMs));
                    currentElapsed = sharedVideoElapsed.load(std::memory_order_acquire);
                }
                if (quitThread.load(std::memory_order_relaxed)) break;
            }

            FrameSlot& slot = slots[tail];
            slot.pts = frameTime;

            uint8_t* dstY  = slot.data.data();
            uint8_t* dstUV = dstY + ySize;

            if (pFrame->linesize[0] == w) {
                memcpy(dstY, pFrame->data[0], ySize);
            } else {
                for (int r = 0; r < h; r++)
                    memcpy(dstY + r * w, pFrame->data[0] + r * pFrame->linesize[0], w);
            }

            if (pFrame->format == AV_PIX_FMT_NV12) {
                if (pFrame->linesize[1] == w) {
                    memcpy(dstUV, pFrame->data[1], uvSize);
                } else {
                    for (int r = 0; r < h / 2; r++)
                        memcpy(dstUV + r * w, pFrame->data[1] + r * pFrame->linesize[1], w);
                }
            } else {
                if (pFrame->linesize[1] == w / 2 && pFrame->linesize[2] == w / 2) {
                    const uint8_t* sU  = pFrame->data[1];
                    const uint8_t* sV  = pFrame->data[2];
                    uint16_t*      dUV = reinterpret_cast<uint16_t*>(dstUV);
                    const size_t   n   = uvSize / 2;
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

            qTail.store(nextTail, std::memory_order_release);

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
    if (isVideoMode && !isReady.load(std::memory_order_acquire)) return;

    int dynamicCenterX = cachedBgaCenterX;

    int renderH = 512, renderW = 512;
    if (isVideoMode && videoTexture) {
        if (videoTexH > 0) renderW = (int)(512.0f * (float)videoTexW / (float)videoTexH);
    } else if (lastDisplayedId != -1) {
        auto it = textures.find(lastDisplayedId);
        if (it != textures.end() && it->second.h > 0)
            renderW = (int)(512.0f * (float)it->second.w / (float)it->second.h);
    }

    SDL_Rect dst = { dynamicCenterX - renderW / 2,
                     cachedBgaCenterY - renderH / 2,
                     renderW, renderH };

    while (currentEventIndex < bgaEvents.size()
           && bgaEvents[currentEventIndex].y <= currentPulse)
        lastDisplayedId = bgaEvents[currentEventIndex++].id;
    while (currentLayerIndex < layerEvents.size()
           && layerEvents[currentLayerIndex].y <= currentPulse)
        lastLayerId = layerEvents[currentLayerIndex++].id;
    while (currentPoorIndex < poorEvents.size()
           && poorEvents[currentPoorIndex].y <= currentPulse)
        lastPoorId = poorEvents[currentPoorIndex++].id;

    if (isVideoMode && videoTexture) {
        double currentTime = sharedVideoElapsed.load(std::memory_order_acquire);

        int head    = qHead.load(std::memory_order_relaxed);
        int tail    = qTail.load(std::memory_order_acquire);
        int bestIdx = -1;
        int scanIdx = head;

        while (scanIdx != tail) {
            if (slots[scanIdx].pts <= currentTime + 0.001) {
                bestIdx = scanIdx;
                scanIdx = (scanIdx + 1) % NUM_SLOTS;
            } else {
                break;
            }
        }

        if (bestIdx >= 0) {
            const uint8_t* src     = slots[bestIdx].data.data();
            const size_t   yBytes  = (size_t)videoTexW * videoTexH;
            const size_t   uvBytes = yBytes / 2;

            void* pixels; int pitch;
            if (SDL_LockTexture(videoTexture, NULL, &pixels, &pitch) == 0) {
                uint8_t* yDst  = (uint8_t*)pixels;
                uint8_t* uvDst = yDst + (ptrdiff_t)pitch * videoTexH;

                if (pitch == videoTexW) {
                    memcpy(yDst,  src,          yBytes);
                    memcpy(uvDst, src + yBytes, uvBytes);
                } else {
                    for (int r = 0; r < videoTexH; r++)
                        memcpy(yDst + (ptrdiff_t)r * pitch, src + r * videoTexW, videoTexW);
                    const uint8_t* uvSrc = src + yBytes;
                    for (int r = 0; r < videoTexH / 2; r++)
                        memcpy(uvDst + (ptrdiff_t)r * pitch, uvSrc + r * videoTexW, videoTexW);
                }
                SDL_UnlockTexture(videoTexture);
            }
            qHead.store((bestIdx + 1) % NUM_SLOTS, std::memory_order_release);
        }

        SDL_RenderCopy(renderer, videoTexture, NULL, &dst);

    } else {
        if (lastDisplayedId != -1) {
            auto it = textures.find(lastDisplayedId);
            if (it != textures.end() && it->second.tex)
                SDL_RenderCopy(renderer, it->second.tex, NULL, &dst);
        }
    }

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

    // ③ FFmpeg コンテキスト解放 (この後に reset() を呼ぶこと)
    if (pFrame)     { av_frame_free(&pFrame);            pFrame     = nullptr; }
    if (pCodecCtx)  { avcodec_free_context(&pCodecCtx); pCodecCtx  = nullptr; }
    if (pFormatCtx) { avformat_close_input(&pFormatCtx); pFormatCtx = nullptr; }

    // ④ FFmpeg アリーナをリセット
    // ★ ③の後に呼ぶこと。フリーリストも含めて一括クリアする。
    FFmpegAllocator::reset();

    // ⑤ SPSC スロット: pts のみリセット。data バッファは解放しない。
    for (int i = 0; i < NUM_SLOTS; i++) {
        std::fill(slots[i].data.begin(), slots[i].data.end(), (uint8_t)0);
        slots[i].pts = -1.0;
    }
    qHead.store(0, std::memory_order_relaxed);
    qTail.store(0, std::memory_order_relaxed);

    // ⑥ 状態リセット
    isVideoMode = false;
    isReady.store(false, std::memory_order_release);
    quitThread.store(false, std::memory_order_relaxed);
    videoFps = 30.0;

    currentEventIndex = 0; currentLayerIndex = 0; currentPoorIndex = 0;
    lastDisplayedId   = -1; lastLayerId = -1; lastPoorId = -1;
}

void BgaManager::cleanup() { clear(); }


