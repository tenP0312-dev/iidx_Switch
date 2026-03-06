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
//    → render()とvideoWorker()が毎フレームmutexを奪い合い、
//      OSスケジューラが両スレッドを止めてジッターが発生する。
//  修正: SPSC (Single-Producer Single-Consumer) ロックフリーキュー に置き換え。
//       mutex を完全に廃止。atomic<int> の head/tail のみで同期する。
//
//  問題②: pCodecCtx->thread_count = 2
//    → FFmpeg が内部に2つの pthread を立てる。
//      外側の videoWorker + 内部2スレッド = 計3スレッドが
//      Switch の4コア(コア0=システム予約)を侵食し、
//      ゲームメインスレッドとコアを奪い合う。
//  修正: thread_count = 1 にしてFFmpeg内部スレッドを無効化。
//       videoWorker スレッドをコア2に固定する (#ifdef __SWITCH__)。
//
//  問題③: MAX_FRAME_QUEUE = 60
//    → 60フレーム分デコードしようとCPUを使い続ける。
//      256×H の小さい動画なら NUM_SLOTS = 6 で十分。
//  修正: NUM_SLOTS = 6 (約0.2秒分のバッファ @ 30fps)
//
//  問題④: render() での行ごと memcpy
//    → ストライドが width と一致する場合に2回の単一 memcpy で済む。
//  修正: pitch == videoTexW の場合は Y/UV を各1回の memcpy で処理。
//
//  【動画制約】
//    height <= 256px, fps <= 30fps の動画のみ受け付ける。
//    これを超える動画は loadBgaFile() が false を返して拒否する。
// ============================================================
class BgaManager {
public:
    // 動画制約 (Switch の処理能力上限)
    static constexpr int MAX_VIDEO_HEIGHT = 256;
    static constexpr int MAX_VIDEO_FPS    = 30;

    BgaManager() = default;
    ~BgaManager() { cleanup(); }

    void init(size_t expectedSize = 512);
    void setBgaDirectory(const std::string& dir) { baseDir = dir; }
    void registerPath(int id, const std::string& filename) { idToFilename[id] = filename; }
    void loadBmp(int id, const std::string& fullPath, SDL_Renderer* renderer);

    // 動画を開く。height>256 または fps>30 の動画は拒否して false を返す。
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

    // NoteRenderer がレイアウトを確定した後に呼ぶ。
    // BgaManager はレーン幅を自分で計算せず、ここで受け取った値のみを使う。
    void setLayout(int bgaCenterX) {
        cachedBgaCenterX = bgaCenterX;
    }

private:
    void videoWorker();

    // NoteRenderer から受け取ったレイアウトキャッシュ
    int cachedBgaCenterX = 640;

    // BMP/PNG テクスチャエントリ
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

    // 動画状態
    bool               isVideoMode = false;
    std::atomic<bool>  isReady{false};   // worker が最初の1フレームを書いた後 true になる
    SDL_Texture*       videoTexture = nullptr;
    int                videoTexW    = 0;
    int                videoTexH    = 0;
    double             videoFps     = 30.0;

    // FFmpeg コンテキスト
    AVFormatContext* pFormatCtx     = nullptr;
    AVCodecContext*  pCodecCtx      = nullptr;
    AVFrame*         pFrame         = nullptr;
    int              videoStreamIdx = -1;

    // デコードスレッド
    std::thread         decodeThread;
    std::atomic<bool>   quitThread{false};
    std::atomic<double> sharedVideoElapsed{0.0};

    // ============================================================
    //  SPSC ロックフリーリングバッファ
    //
    //  Producer (videoWorker): qTail のみ書く、qHead のみ読む
    //  Consumer (render):      qHead のみ書く、qTail のみ読む
    //  → mutex 不要、キャッシュライン競合も最小
    //
    //  NUM_SLOTS = 6: 30fps なら約200ms 分のバッファ
    //  各スロット: NV12 タイトパッキング (stride = width)
    //             サイズ = width × height × 3/2
    // ============================================================
    static constexpr int NUM_SLOTS = 6;

    struct FrameSlot {
        std::vector<uint8_t> data; // NV12 (Y plane + UV plane 連続)
        double               pts = -1.0;
    };

    FrameSlot          slots[NUM_SLOTS];
    std::atomic<int>   qHead{0};  // Consumer (render)  が次に読む位置
    std::atomic<int>   qTail{0};  // Producer (worker)  が次に書く位置
};

#endif // BGAMANAGER_HPP

