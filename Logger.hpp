#ifndef LOGGER_HPP
#define LOGGER_HPP

#include <string>
#include <fstream>
#include <cstdio>
#include <cstdarg>
#include <SDL2/SDL.h>

// ─────────────────────────────────────────────────────────
//  Logger  ―  シンプルなファイルロガー
//
//  使い方:
//    Logger::init(Config::ROOT_PATH + "log.txt");  // 起動時に1回
//    LOG_INFO("SoundManager", "loaded %d sounds", count);
//    LOG_WARN("BgaManager",  "file not found: %s", path.c_str());
//    LOG_ERROR("SoundManager","Mix_LoadWAV_RW failed: %s", Mix_GetError());
//    LOG_TRACE("ScenePlay", "frame #%d cur_ms=%.1f", frame, cur_ms); // 高頻度ログ
//
//  出力フォーマット:
//    [  1234ms][INFO ][SoundManager] loaded 1185 sounds
//    [  1234ms][TRACE][ScenePlay   ] frame #1 cur_ms=-1998.0
//
//  TRACE ログ:
//    通常は何も出力しない空マクロ。クラッシュ再現調査時だけ
//    コンパイル時に -DENABLE_LOG_TRACE を追加して有効化する。
//    毎フレーム有効にすると sdmc:/ への fflush で数 ms/frame のスパイクが出るので注意。
// ─────────────────────────────────────────────────────────

#ifdef __SWITCH__
#include <switch.h>
#endif

class Logger {
public:
    static void init(const std::string& path) {
        // 起動毎にリセット（既存ファイルを上書き）
        instance().open(path);
    }

    // ── Switch ヒープ使用量 (MB) ──────────────────────────
    // svcGetInfo で取得。曲間の LOG_INFO に差し込むと
    // SoundManager の mem= だけでは見えない全体のリークが検出できる。
    //   例: LOG_INFO("main", "heap: %lluMB", Logger::heapUsedMB());
    static uint64_t heapUsedMB() {
#ifdef __SWITCH__
        uint64_t used = 0;
        svcGetInfo(&used, InfoType_UsedMemorySize, CUR_PROCESS_HANDLE, 0);
        return used / (1024ULL * 1024ULL);
#else
        return 0;
#endif
    }

    static void log(const char* level, const char* tag, const char* fmt, ...) {
        Logger& L = instance();
        if (!L.file_) return;

        uint32_t ms = SDL_GetTicks();
        char msg[1024];
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(msg, sizeof(msg), fmt, ap);
        va_end(ap);

        fprintf(L.file_, "[%7ums][%-5s][%s] %s\n", ms, level, tag, msg);
        fflush(L.file_);
    }

    static void close() {
        Logger& L = instance();
        if (L.file_) { fclose(L.file_); L.file_ = nullptr; }
    }

private:
    FILE* file_ = nullptr;

    static Logger& instance() {
        static Logger inst;
        return inst;
    }

    void open(const std::string& path) {
        if (file_) fclose(file_);
        file_ = fopen(path.c_str(), "w");
        if (file_) {
            fprintf(file_, "=== GeminiRhythm Log ===\n");
            // ビルド日時を残しておくと「ログがどのバイナリのものか」が即わかる
            fprintf(file_, "=== Build: %s %s ===\n", __DATE__, __TIME__);
            fflush(file_);
        }
    }

    Logger() {}
    ~Logger() { close(); }
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;
};

// ─── 呼び出しマクロ（タグ固定版） ───────────────────────
#define LOG_INFO(tag, fmt, ...)  Logger::log("INFO",  tag, fmt, ##__VA_ARGS__)
#define LOG_WARN(tag, fmt, ...)  Logger::log("WARN",  tag, fmt, ##__VA_ARGS__)
#define LOG_ERROR(tag, fmt, ...) Logger::log("ERROR", tag, fmt, ##__VA_ARGS__)

// LOG_TRACE: 毎フレーム呼ばれうる高頻度ログ。
// 通常は空マクロ（ゼロコスト）。クラッシュ調査時に -DENABLE_LOG_TRACE を追加して有効化。
// 有効時は sdmc:/ への fflush が毎フレーム走るため FPS が落ちる点に注意。
#ifdef ENABLE_LOG_TRACE
  #define LOG_TRACE(tag, fmt, ...) Logger::log("TRACE", tag, fmt, ##__VA_ARGS__)
#else
  #define LOG_TRACE(tag, fmt, ...) do {} while(0)
#endif

#endif // LOGGER_HPP


