#ifndef BGAMANAGER_HPP
#define BGAMANAGER_HPP

#include <SDL2/SDL.h>
#include <string>
#include <unordered_map>
#include <vector>
#include <thread>
#include <atomic>
#include "CommonTypes.hpp"

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
}

#ifdef __SWITCH__
#include <switch.h>
#endif

struct BgaEvent {
    long long y;
    int id;
};

// ============================================================
//  BgaManager — Switch 最適化版
//
//  【旧実装の問題点と修正内容】
//
//  問題①: mutex + std::deque がホットパス上にある
//    修正: SPSC ロックフリーキューに置き換え。
//
//  問題②: pCodecCtx->thread_count = 2
//    修正: thread_count = 1、videoWorker をコア2に固定。
//
//  問題③: MAX_FRAME_QUEUE = 60
//    修正: NUM_SLOTS = 6 (約0.2秒分のバッファ @ 30fps)
//
//  問題④: render() での行ごと memcpy
//    修正: pitch == videoTexW の場合は Y/UV を各1回の memcpy で処理。
//
//  問題⑤: FFmpeg が libc malloc を使うことによるヒープ断片化
//    → avformat_open_input / avcodec_open2 が曲をまたぐたびに
//      libc malloc/free を繰り返し、Switch の malloc アリーナが断片化。
//      最終的に SDL_Mixer の Mix_LoadWAV_RW が "Out of memory" で失敗する。
//    修正: FFmpegAllocator (フリーリスト付き固定アリーナ) に
//          av_malloc シンボルを差し替えて FFmpeg をメインヒープから切り離す。
//
//  【動画制約】
//    height <= 256px, fps <= 30fps の動画のみ受け付ける。
// ============================================================

// ============================================================
//  FFmpegAllocator — フリーリスト付き固定アリーナアロケータ
//
//  【設計方針】
//  FFmpeg の確保パターンは2種類ある:
//    (A) 初期化バッファ: avformat_open_input / avcodec_open2 時に確保し
//        avformat_close_input / avcodec_free_context で解放。長寿命。
//    (B) フレーム処理バッファ: AVPacket / 内部作業バッファ。
//        毎フレーム確保→解放を繰り返す。短寿命・小サイズ。
//
//  バンプアロケータは (A) には有効だが (B) で枯渇する。
//  → フリーリストを追加して解放済みブロックを再利用する。
//
//  【フリーリスト戦略】
//  サイズクラスを N_BINS 段に分けてそれぞれ単方向リストで管理する。
//  確保要求はサイズを切り上げて対応クラスのリストから取り出す。
//  リストが空なら新規バンプ確保する。
//  アリーナ全体のリセットは reset() で O(1)。
//
//  MAX_BIN_SIZE を超えるサイズはそのまま新規バンプ確保し、
//  free されても再利用せずに「穴」として残す
//  (大きなブロックは頻繁に確保・解放されないため問題ない)。
//
//  【アライメント】
//  FFmpeg の av_malloc は AV_INPUT_BUFFER_PADDING_SIZE (32バイト) の
//  アライメントを保証する。これを満たさないと AArch64 NEON が
//  スカラーフォールバックに落ちて fps が激減する。
//  ブロックヘッダも 32 バイト固定にすることで
//  ペイロード先頭が常に 32 バイト境界に来る。
// ============================================================
class FFmpegAllocator {
public:
    // アリーナサイズ。
    // 初期化バッファ (~3MB) + フレームバッファ循環分 + 余裕
    static constexpr size_t FFMPEG_HEAP_SIZE = 16 * 1024 * 1024; // 16MB

    static void   install();
    static void   uninstall();
    static size_t usedBytes();

    // FFmpeg コンテキストを全て解放した後に呼ぶ。
    // アリーナポインタを先頭に戻し、フリーリストもクリアする。
    static void reset();

    // av_malloc / av_free シンボル上書きから呼ばれる (public 必須)
    static void* ffmpegMalloc(size_t size, void* opaque);
    static void* ffmpegRealloc(void* ptr, size_t size, void* opaque);
    static void  ffmpegFree(void* ptr, void* opaque);

private:
    // ---- アライメント定数 ----
    static constexpr size_t ALIGN = 32; // FFmpeg NEON 要件
    static size_t alignUp(size_t n) { return (n + ALIGN - 1) & ~(ALIGN - 1); }

    // ---- ブロックヘッダ (32バイト固定) ----
    // ペイロード直前に埋め込む。32バイト固定にすることで
    // ペイロード先頭が常に ALIGN 境界に来る。
    struct BlockHeader {
        size_t       size;      // ペイロードバイト数
        BlockHeader* nextFree;  // フリーリストの次ノード (使用中は nullptr)
        uint8_t      _pad[32 - sizeof(size_t) - sizeof(BlockHeader*)];
    };
    static_assert(sizeof(BlockHeader) == 32, "BlockHeader must be 32 bytes");
    static constexpr size_t HDR = sizeof(BlockHeader); // = 32

    // ---- フリーリスト ----
    // サイズクラス: 32バイト刻み、MAX_BIN_SIZE まで
    // binIdx(size): ペイロードサイズ size に対応するビンインデックス
    //   size=1..32 → 0, 33..64 → 1, 65..96 → 2, ...
    static constexpr size_t BIN_STEP     = 32;
    static constexpr size_t MAX_BIN_SIZE = 32768; // これ以下のサイズをフリーリスト管理 (H.264作業バッファ最大~17KB)
    static constexpr size_t N_BINS       = MAX_BIN_SIZE / BIN_STEP; // = 1024

    static size_t binIdx(size_t size) {
        size_t s = alignUp(size == 0 ? 1 : size);
        return s / BIN_STEP - 1;
    }

    static BlockHeader* freeLists[N_BINS]; // 各ビンの先頭 (nullptr = 空)

    // ---- アリーナ本体 ----
    alignas(32) static uint8_t heap[FFMPEG_HEAP_SIZE];
    static size_t              heapPos;
    static bool                installed;
};

class BgaManager {
public:
    static constexpr int MAX_VIDEO_HEIGHT = 256;
    static constexpr int MAX_VIDEO_FPS    = 30;

    BgaManager() = default;
    ~BgaManager() { cleanup(); }

    void init(size_t expectedSize = 512);
    void setBgaDirectory(const std::string& dir) { baseDir = dir; }
    void registerPath(int id, const std::string& filename) { idToFilename[id] = filename; }
    void loadBmp(int id, const std::string& fullPath, SDL_Renderer* renderer);

    bool loadBgaFile(const std::string& path, SDL_Renderer* renderer);

    void preLoad(long long startPulse, SDL_Renderer* renderer);
    void setEvents(const std::vector<BgaEvent>& events)      { bgaEvents   = events; currentEventIndex = 0; }
    void setLayerEvents(const std::vector<BgaEvent>& events)  { layerEvents = events; currentLayerIndex = 0; }
    void setPoorEvents(const std::vector<BgaEvent>& events)   { poorEvents  = events; currentPoorIndex  = 0; }
    void syncTime(double ms);
    void render(long long currentPulse, SDL_Renderer* renderer, int x, int y, double cur_ms = 0.0);
    void setMissTrigger(bool active) { showPoor = active; }
    void clear();
    void cleanup();

    void setLayout(int bgaCenterX, int bgaCenterY = 360) {
        cachedBgaCenterX = bgaCenterX;
        cachedBgaCenterY = bgaCenterY;
    }

private:
    void videoWorker();

    int cachedBgaCenterX = 640;
    int cachedBgaCenterY = 360;   // BGA中心Y座標

    struct BgaTextureEntry {
        SDL_Texture* tex = nullptr;
        int w = 0;
        int h = 0;
    };

    std::unordered_map<int, BgaTextureEntry> textures;
    std::unordered_map<int, std::string>     idToFilename;
    std::string baseDir;

    std::vector<BgaEvent> bgaEvents, layerEvents, poorEvents;
    size_t currentEventIndex = 0, currentLayerIndex = 0, currentPoorIndex = 0;
    int    lastDisplayedId   = -1, lastLayerId = -1, lastPoorId = -1;
    bool   showPoor          = false;

    bool               isVideoMode = false;
    std::atomic<bool>  isReady{false};
    SDL_Texture*       videoTexture = nullptr;
    int                videoTexW    = 0;
    int                videoTexH    = 0;
    double             videoFps     = 30.0;

    AVFormatContext* pFormatCtx     = nullptr;
    AVCodecContext*  pCodecCtx      = nullptr;
    AVFrame*         pFrame         = nullptr;
    int              videoStreamIdx = -1;

    std::thread         decodeThread;
    std::atomic<bool>   quitThread{false};
    std::atomic<double> sharedVideoElapsed{0.0};

    static constexpr int NUM_SLOTS = 6;

    struct FrameSlot {
        std::vector<uint8_t> data;
        double               pts = -1.0;
    };

    FrameSlot          slots[NUM_SLOTS];
    std::atomic<int>   qHead{0};
    std::atomic<int>   qTail{0};
};

#endif // BGAMANAGER_HPP


