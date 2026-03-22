#ifndef SOUNDMANAGER_HPP
#define SOUNDMANAGER_HPP

#include <SDL2/SDL_mixer.h>
#include <string>
#include <unordered_map>
#include <cstdint> 
#include <vector>
#include <functional>

class SoundManager {
public:
    static SoundManager& getInstance() {
        static SoundManager instance;
        return instance;
    }

    void init();
    
    void loadSingleSound(const std::string& filename, const std::string& rootPath, const std::string& bmsonName = "sounds");
    
    void loadSoundsInBulk(const std::vector<std::string>& filenames, 
                          const std::string& rootPath, 
                          const std::string& bmsonName,
                          std::function<void(int, const std::string&)> onProgress = nullptr);

    void preloadBoxIndex(const std::string& rootPath, const std::string& bmsonName);

    // --- 既存ロジック100%継承: 数値IDによる再生 ---
    void play(int soundId); 
    void playByName(const std::string& name);
    
    void clear();
    void stopAll();
    void cleanup();

    void playPreview(const std::string& fullPath);
    void stopPreview();

    uint64_t getCurrentMemory() const { return currentTotalMemory; }
    uint64_t getMaxMemory() const { return MAX_WAV_MEMORY; }

    // 再生中のチャンネルが1つでも存在するか
    bool anyPlaying() const { return Mix_Playing(-1) > 0; }

    // --- ヘルパー: 文字列からのID生成（一貫性維持用） ---
    // std::hash<std::string> はプラットフォーム・コンパイラ依存で値が変わるため使わない。
    // PlayEngine::init() の FNV-1a と同じアルゴリズムで統一する。
    inline uint32_t getHash(const std::string& name) const {
        uint32_t h = 2166136261u;
        for (unsigned char c : name) {
            h ^= c;
            h *= 16777619u;
        }
        return h;
    }

private:
    SoundManager() : currentPreviewChunk(nullptr), currentTotalMemory(0) {} 
    ~SoundManager() = default; // cleanup()はmain()から明示的に呼ぶ。SDL_Quit後のdouble-freeを防ぐ。
    SoundManager(const SoundManager&) = delete;
    SoundManager& operator=(const SoundManager&) = delete;

    struct BoxEntry {
        std::string pckPath;
        uint64_t offset; // uint32_t だと 4GB 超で overflow。uint64_t に統一。
        uint32_t size;
    };

    // --- 最適化: キーを std::string から uint32_t (ハッシュID) に変更 ---
    // これにより PlayableNote のコピーから std::string が消え、演奏中の検索が高速化されます
    std::unordered_map<uint32_t, Mix_Chunk*> sounds;
    // ★削除: activeChannels — 書き込みのみで一度も読まれていないデッドコード。
    //   毎フレーム10-20回の unordered_map 書き込みを根絶し、
    //   ハッシュ再配置・メモリアロケーションを削減する。
    
    // ロード時にファイル名で検索する必要があるため、ここは string を維持
    std::unordered_map<std::string, BoxEntry> boxIndex;

    Mix_Chunk* currentPreviewChunk = nullptr;
    std::string lastPreviewPath;   // playPreview()のstatic変数をメンバに昇格。clear()でリセット可能にする。

    uint64_t currentTotalMemory = 0;
    const uint64_t MAX_WAV_MEMORY = 512 * 1024 * 1024;

    // ★修正: SDL_malloc/SDL_free の繰り返しによるヒープフラグメンテーション対策。
    // 毎回 SDL_malloc(可変サイズ) + SDL_free を繰り返すと、Switch の libc malloc では
    // 30〜60分でヒープが断片化し、大きな連続領域の確保に失敗するようになる。
    // BGM ファイルはキー音より大きいため、SDL_malloc が NULL を返してサイレントにスキップされ
    // 「BGM だけ鳴らなくなる」症状として顕在化する。
    // vector は resize() で最大サイズに一度だけ拡大し、以降は同じメモリを使い回す。
    // アロケーション回数を「起動から終了まで数回」に激減させ、フラグメンテーションを根絶する。
    std::vector<uint8_t> loadBuffer;

    // チャンネル枯渇時のラウンドロビン強奪カーソル
    // static ローカル変数だと曲をまたいでもリセットされないため、メンバに昇格
    int nextVictim = 0;
};

#endif










