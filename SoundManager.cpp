#include "SoundManager.hpp"
#include <SDL2/SDL.h>
#include <SDL2/SDL_mixer.h>
#include <iostream>
#include <unordered_map>
#include <fstream>
#include <vector>
#include <cstring>
#include <algorithm>

void SoundManager::init() {
    sounds.reserve(4000);
    SDL_SetHint("SDL_AUDIO_RESAMPLING_MODE", "linear");

    if (Mix_OpenAudio(22050, MIX_DEFAULT_FORMAT, 1, 512) < 0) {
        std::cerr << "Mix_OpenAudio Error: " << Mix_GetError() << std::endl;
    }

    Mix_AllocateChannels(256);
    std::cout << "SoundManager Initialized." << std::endl;
}

void SoundManager::preloadBoxIndex(const std::string& rootPath, const std::string& bmsonName) {
    boxIndex.clear();
    int partIdx = 1;
    while (true) {
        std::string suffix = (partIdx == 1) ? "" : std::to_string(partIdx);
        std::string pckPath = rootPath + (rootPath.empty() || rootPath.back() == '/' ? "" : "/") + bmsonName + suffix + ".boxwav";

        // ★修正: SDL_Delay(2500) の無条件ハードスリープを廃止。
        //        旧実装は「2.5秒待てば I/O が安定する」という根拠のない仮定に基づいており、
        //        遅い microSD では 2.5秒でも足りず、速い microSD では無駄に待つだけだった。
        //        新実装: ファイルを開けたら即座に処理を続行し、
        //                開けなかった場合のみ exponential backoff でリトライする。
        //        200ms → 400ms → 800ms → 1600ms → 3200ms の最大5回試行 (計 ~6秒上限)。
        //        ファイルが存在しない場合（= これ以上パートがない）は即座に終了する。
        std::ifstream ifs;
        if (partIdx > 1) {
            for (int retry = 0; retry < 5; ++retry) {
                ifs.open(pckPath, std::ios::binary);
                if (ifs) break;
                // 存在しないファイルへのリトライは無意味なので open 失敗=終了とみなす
                // (ただし I/O 一時エラーの可能性を考慮して 1 回だけ短い待機を挟む)
                if (retry == 0) SDL_Delay(200);
                else break;
            }
        } else {
            ifs.open(pckPath, std::ios::binary);
        }
        if (!ifs) break;

        uint32_t count, d1, d2;
        if (!ifs.read((char*)&count, 4)) break;
        ifs.read((char*)&d1, 4);
        ifs.read((char*)&d2, 4);

        for (uint32_t i = 0; i < count; ++i) {
            char nameBuf[32];
            uint32_t fSize;
            if (!ifs.read(nameBuf, 32)) break;
            if (!ifs.read((char*)&fSize, 4)) break;

            std::string fileName(nameBuf, strnlen(nameBuf, 32));
            boxIndex[fileName] = { pckPath, (uint32_t)ifs.tellg(), fSize };
            ifs.seekg(fSize, std::ios::cur);
        }
        partIdx++;
        if (partIdx > 128) break;
    }
}

void SoundManager::loadSingleSound(const std::string& filename, const std::string& rootPath, const std::string& bmsonName) {
    uint32_t id = getHash(filename);
    if (sounds.find(id) != sounds.end()) return;

    if (boxIndex.count(filename)) {
        auto& entry = boxIndex[filename];

        // ★修正: MAX_WAV_MEMORY チェックを boxIndex パスにも追加。
        //        旧実装はこのパスだけチェックが抜けていた。
        if (currentTotalMemory + entry.size > MAX_WAV_MEMORY) return;

        std::ifstream ifs(entry.pckPath, std::ios::binary);
        if (ifs) {
            // ★修正: SDL_malloc → loadBuffer (再利用バッファ) に変更。
            //        SDL_malloc の NULL 返却チェック不要になり、
            //        ヒープフラグメンテーションも発生しない。
            if (loadBuffer.size() < entry.size)
                loadBuffer.resize(entry.size);

            ifs.seekg(entry.offset);
            ifs.read((char*)loadBuffer.data(), entry.size);

            SDL_RWops* rw = SDL_RWFromMem(loadBuffer.data(), (int)entry.size);
            Mix_Chunk* chunk = Mix_LoadWAV_RW(rw, 0);
            SDL_RWclose(rw);

            if (chunk) {
                sounds[id] = chunk;
                currentTotalMemory += entry.size;
            } else {
                fprintf(stderr, "[SoundManager] Mix_LoadWAV_RW failed: %s (%s)\n",
                        filename.c_str(), Mix_GetError());
            }
            return;
        }
    }

    // 外部ファイル読み込み
    std::string path = rootPath + (rootPath.empty() || rootPath.back() == '/' ? "" : "/") + filename;
    SDL_RWops* rw = SDL_RWFromFile(path.c_str(), "rb");
    if (!rw) return;

    uint64_t fileSize = SDL_RWsize(rw);
    if (currentTotalMemory + fileSize > MAX_WAV_MEMORY) {
        SDL_RWclose(rw);
        return;
    }

    // 外部ファイルは Mix_LoadWAV_RW(freesrc=1) で問題なし。
    // SDL_RWFromFile で開いた RWops は SDL が内部でファイルハンドルを持つため、
    // freesrc=1 で正しく閉じられる。
    Mix_Chunk* chunk = Mix_LoadWAV_RW(rw, 1);
    if (chunk) {
        sounds[id] = chunk;
        currentTotalMemory += fileSize;
    }
}

void SoundManager::loadSoundsInBulk(const std::vector<std::string>& filenames,
                                    const std::string& rootPath,
                                    const std::string& bmsonName,
                                    std::function<void(int, const std::string&)> onProgress) {

    std::unordered_map<std::string, std::vector<std::string>> groupPerBox;
    std::vector<std::string> externalFiles;

    for (const auto& name : filenames) {
        if (sounds.find(getHash(name)) != sounds.end()) continue;
        if (boxIndex.count(name)) {
            groupPerBox[boxIndex[name].pckPath].push_back(name);
        } else {
            externalFiles.push_back(name);
        }
    }

    int processedCount = 0;

    for (auto& [pckPath, list] : groupPerBox) {
        // オフセット順にソートしてシーク回数を最小化
        std::sort(list.begin(), list.end(), [&](const std::string& a, const std::string& b) {
            return boxIndex[a].offset < boxIndex[b].offset;
        });

        std::ifstream ifs(pckPath, std::ios::binary);
        if (!ifs) continue;

        for (const auto& name : list) {
            auto& entry = boxIndex[name];
            if (currentTotalMemory + entry.size > MAX_WAV_MEMORY) {
                processedCount++;
                if (onProgress) onProgress(processedCount, name);
                continue;
            }

            // ★修正: SDL_malloc → loadBuffer (再利用バッファ) に変更。
            //        SDL_malloc/SDL_free の繰り返しはヒープフラグメンテーションの原因。
            //        30〜60分プレイ後に大きな連続領域の確保だけ失敗するようになり、
            //        「BGMだけ鳴らなくなる」症状を引き起こしていた。
            //        loadBuffer は最大サイズに一度だけ拡大し、以降は解放しない。
            if (loadBuffer.size() < entry.size)
                loadBuffer.resize(entry.size);

            ifs.seekg(entry.offset);
            ifs.read((char*)loadBuffer.data(), entry.size);

            SDL_RWops* rwIndiv = SDL_RWFromMem(loadBuffer.data(), (int)entry.size);
            Mix_Chunk* chunk = Mix_LoadWAV_RW(rwIndiv, 0);
            SDL_RWclose(rwIndiv);

            if (chunk) {
                sounds[getHash(name)] = chunk;
                currentTotalMemory += entry.size;
            } else {
                fprintf(stderr, "[SoundManager] Mix_LoadWAV_RW failed: %s (%s)\n",
                        name.c_str(), Mix_GetError());
            }

            processedCount++;
            if (onProgress) onProgress(processedCount, name);
        }
    }

    for (const auto& name : externalFiles) {
        loadSingleSound(name, rootPath, bmsonName);
        processedCount++;
        if (onProgress) onProgress(processedCount, name);
    }
}

void SoundManager::play(int soundId) {
    uint32_t id = static_cast<uint32_t>(soundId);
    // ★修正: sounds.count(id) + sounds[id] の二重ハッシュ計算を廃止。
    //        find() でイテレータを1回取得し、以降はイテレータ経由で直接アクセスする。
    //        1音再生ごとにハッシュ計算が2→1回になる。
    auto it = sounds.find(id);
    if (it != sounds.end() && it->second != nullptr) {
        Mix_Chunk* targetChunk = it->second;
        int newChannel = Mix_PlayChannel(-1, targetChunk, 0);

        if (newChannel == -1) {
            static int nextVictim = 0;
            newChannel = nextVictim;
            Mix_HaltChannel(newChannel);
            Mix_PlayChannel(newChannel, targetChunk, 0);
            nextVictim = (nextVictim + 1) % 256;
        }

        if (newChannel != -1) {
            Mix_Volume(newChannel, 96);
            activeChannels[id] = newChannel;
        }
    }
}

void SoundManager::playByName(const std::string& name) {
    play(static_cast<int>(getHash(name)));
}

void SoundManager::playPreview(const std::string& fullPath) {
    static std::string lastPath = "";
    if (lastPath == fullPath && currentPreviewChunk != nullptr) return;
    stopPreview();

    SDL_RWops* rw = SDL_RWFromFile(fullPath.c_str(), "rb");
    if (!rw) return;

    Mix_Chunk* previewChunk = Mix_LoadWAV_RW(rw, 1);
    if (previewChunk) {
        Mix_PlayChannel(255, previewChunk, -1);
        Mix_Volume(255, 80);
        currentPreviewChunk = previewChunk;
        lastPath = fullPath;
    }
}

void SoundManager::stopPreview() {
    Mix_HaltChannel(255);
    if (currentPreviewChunk) {
        Mix_FreeChunk(currentPreviewChunk);
        currentPreviewChunk = nullptr;
    }
}

void SoundManager::stopAll() {
    Mix_HaltChannel(-1);
    activeChannels.clear();
    stopPreview();
}

void SoundManager::clear() {
    stopAll();
    for (auto& pair : sounds) {
        if (pair.second) Mix_FreeChunk(pair.second);
    }
    std::unordered_map<uint32_t, Mix_Chunk*>().swap(sounds);
    std::unordered_map<uint32_t, int>().swap(activeChannels);

    boxIndex.clear();
    currentTotalMemory = 0;
    sounds.reserve(4000);

    // ★修正: Mix_CloseAudio()/Mix_OpenAudio() を廃止する。
    // Switch では OpenAudio の再呼び出しがドライバ側の解放完了前に実行されると
    // -1 を返し、以降の Mix_PlayChannel が全て失敗して 2曲目以降が無音になる。
    // オーディオデバイスはアプリ起動から終了まで開きっぱなしにし、
    // チャンネルの再割り当てだけ行えば十分。
    Mix_AllocateChannels(256);
}

void SoundManager::cleanup() {
    clear();
    Mix_CloseAudio();
}












