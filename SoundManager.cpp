#include "SoundManager.hpp"
#include "Logger.hpp"
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
        LOG_ERROR("SoundManager", "Mix_OpenAudio failed: %s", Mix_GetError());
    } else {
        LOG_INFO("SoundManager", "Mix_OpenAudio OK (22050Hz, mono, buf=512)");
    }

    Mix_AllocateChannels(256);
    LOG_INFO("SoundManager", "init complete. channels=256");
}

void SoundManager::preloadBoxIndex(const std::string& rootPath, const std::string& bmsonName) {
    boxIndex.clear();
    LOG_INFO("SoundManager", "preloadBoxIndex start: rootPath='%s' bmsonName='%s'",
             rootPath.c_str(), bmsonName.c_str());

    // ── ヘルパー: 通常boxwavのエントリをboxIndexに登録 ──────────────────
    // pckPath のファイルを開き、エントリを boxIndex に追加する。
    // REDIRECT/REDIRECT+EXTRA の場合は参照先を解決してから登録する。
    // マルチパート (boxwav, boxwav2, ...) も処理する。
    auto loadBoxFile = [&](const std::string& pckPath) {
        int partIdx = 1;
        std::string curPath = pckPath;
        while (true) {
            std::ifstream ifs;
            if (partIdx > 1) {
                // パート2以降: exponential backoff で最大2回試行
                for (int retry = 0; retry < 2; ++retry) {
                    ifs.open(curPath, std::ios::binary);
                    if (ifs) break;
                    if (retry == 0) SDL_Delay(200);
                    else break;
                }
            } else {
                ifs.open(curPath, std::ios::binary);
            }
            if (!ifs) break;

            uint32_t magic;
            if (!ifs.read((char*)&magic, 4)) break;

            // ── REDIRECT (0xFFFFFFFF) ──────────────────────────────────
            if (magic == 0xFFFFFFFF) {
                char targetBuf[32];
                if (!ifs.read(targetBuf, 32)) break;
                std::string targetName(targetBuf, strnlen(targetBuf, 32));
                LOG_INFO("SoundManager", "REDIRECT: '%s' -> '%s'", curPath.c_str(), targetName.c_str());
                // 同フォルダの実体boxwavを再帰的にロード
                std::string dir = curPath.substr(0, curPath.find_last_of('/') + 1);
                std::string realPath = dir + targetName;
                // 実体ファイルをそのままロード (パート分割あり)
                int rPartIdx = 1;
                while (true) {
                    std::string suffix = (rPartIdx == 1) ? "" : std::to_string(rPartIdx);
                    // 拡張子の直前にsuffixを挿入: "33086.boxwav" → "33086.boxwav", "330862.boxwav"
                    std::string rPath;
                    auto dotPos = realPath.rfind('.');
                    if (dotPos != std::string::npos && rPartIdx > 1)
                        rPath = realPath.substr(0, dotPos) + suffix + realPath.substr(dotPos);
                    else
                        rPath = realPath;
                    std::ifstream rifs(rPath, std::ios::binary);
                    if (!rifs) break;
                    uint32_t rCount, rd1, rd2;
                    if (!rifs.read((char*)&rCount, 4)) break;
                    rifs.read((char*)&rd1, 4); rifs.read((char*)&rd2, 4);
                    LOG_INFO("SoundManager", "REDIRECT real boxwav: '%s' entries=%u (part%d)", rPath.c_str(), rCount, rPartIdx);
                    for (uint32_t i = 0; i < rCount; ++i) {
                        char nameBuf[32]; uint32_t fSize;
                        if (!rifs.read(nameBuf, 32)) break;
                        if (!rifs.read((char*)&fSize, 4)) break;
                        std::string fileName(nameBuf, strnlen(nameBuf, 32));
                        boxIndex[fileName] = { rPath, (uint32_t)rifs.tellg(), fSize };
                        rifs.seekg(fSize, std::ios::cur);
                    }
                    rPartIdx++;
                    if (rPartIdx > 128) break;
                }
                break; // REDIRECT ファイル自体はパート分割なし
            }

            // ── REDIRECT+EXTRA (0xFFFFFFFE) ───────────────────────────
            if (magic == 0xFFFFFFFE) {
                char targetBuf[32];
                if (!ifs.read(targetBuf, 32)) break;
                std::string targetName(targetBuf, strnlen(targetBuf, 32));
                // 共通boxwavをロード (上記REDIRECTと同じロジック)
                std::string dir = curPath.substr(0, curPath.find_last_of('/') + 1);
                std::string realPath = dir + targetName;
                int rPartIdx = 1;
                while (true) {
                    std::string rPath;
                    auto dotPos = realPath.rfind('.');
                    if (dotPos != std::string::npos && rPartIdx > 1)
                        rPath = realPath.substr(0, dotPos) + std::to_string(rPartIdx) + realPath.substr(dotPos);
                    else
                        rPath = realPath;
                    std::ifstream rifs(rPath, std::ios::binary);
                    if (!rifs) break;
                    uint32_t rCount, rd1, rd2;
                    if (!rifs.read((char*)&rCount, 4)) break;
                    rifs.read((char*)&rd1, 4); rifs.read((char*)&rd2, 4);
                    LOG_INFO("SoundManager", "REDIRECT+EXTRA real boxwav: '%s' entries=%u (part%d)", rPath.c_str(), rCount, rPartIdx);
                    for (uint32_t i = 0; i < rCount; ++i) {
                        char nameBuf[32]; uint32_t fSize;
                        if (!rifs.read(nameBuf, 32)) break;
                        if (!rifs.read((char*)&fSize, 4)) break;
                        std::string fileName(nameBuf, strnlen(nameBuf, 32));
                        boxIndex[fileName] = { rPath, (uint32_t)rifs.tellg(), fSize };
                        rifs.seekg(fSize, std::ios::cur);
                    }
                    rPartIdx++;
                    if (rPartIdx > 128) break;
                }
                // 固有WAVをインラインから登録 (curPath 内に直接格納)
                uint32_t extraCount;
                if (!ifs.read((char*)&extraCount, 4)) break;
                for (uint32_t i = 0; i < extraCount; ++i) {
                    char nameBuf[32]; uint32_t fSize;
                    if (!ifs.read(nameBuf, 32)) break;
                    if (!ifs.read((char*)&fSize, 4)) break;
                    std::string fileName(nameBuf, strnlen(nameBuf, 32));
                    boxIndex[fileName] = { curPath, (uint32_t)ifs.tellg(), fSize };
                    ifs.seekg(fSize, std::ios::cur);
                }
                break; // REDIRECT+EXTRA もパート分割なし
            }

            // ── 通常boxwav ────────────────────────────────────────────
            uint32_t count = magic; // magic == entry_count
            uint32_t d1, d2;
            ifs.read((char*)&d1, 4); ifs.read((char*)&d2, 4);
            LOG_INFO("SoundManager", "normal boxwav: '%s' entries=%u (part%d)",
                     curPath.c_str(), count, partIdx);
            for (uint32_t i = 0; i < count; ++i) {
                char nameBuf[32]; uint32_t fSize;
                if (!ifs.read(nameBuf, 32)) break;
                if (!ifs.read((char*)&fSize, 4)) break;
                std::string fileName(nameBuf, strnlen(nameBuf, 32));
                boxIndex[fileName] = { curPath, (uint32_t)ifs.tellg(), fSize };
                ifs.seekg(fSize, std::ios::cur);
            }

            // 次パートへ
            partIdx++;
            if (partIdx > 128) break;
            auto dotPos = pckPath.rfind('.');
            if (dotPos == std::string::npos) break;
            curPath = pckPath.substr(0, dotPos) + std::to_string(partIdx) + pckPath.substr(dotPos);
        }
    };

    // bmsonName に対応する boxwav をロード (パート1から開始)
    std::string sep    = (rootPath.empty() || rootPath.back() == '/') ? "" : "/";
    std::string base   = rootPath + sep + bmsonName;
    std::string part1  = base + ".boxwav";

    // ★修正: パート1のみ exponential backoff でリトライ
    {
        std::ifstream probe;
        for (int retry = 0; retry < 5; ++retry) {
            probe.open(part1, std::ios::binary);
            if (probe) break;
            if (retry == 0) SDL_Delay(200);
            else { SDL_Delay(200 << retry); }
        }
        if (!probe) return; // boxwav が存在しない
    }

    loadBoxFile(part1);
    LOG_INFO("SoundManager", "preloadBoxIndex done: boxIndex.size=%zu, memory=%lluMB / %lluMB",
             boxIndex.size(),
             (unsigned long long)(currentTotalMemory / 1024 / 1024),
             (unsigned long long)(MAX_WAV_MEMORY   / 1024 / 1024));
}

void SoundManager::loadSingleSound(const std::string& filename, const std::string& rootPath, const std::string& bmsonName) {
    uint32_t id = getHash(filename);
    if (sounds.find(id) != sounds.end()) {
        LOG_INFO("SoundManager", "loadSingleSound SKIP (already loaded): '%s'", filename.c_str());
        return;
    }

    if (boxIndex.count(filename)) {
        auto& entry = boxIndex[filename];

        if (currentTotalMemory + entry.size > MAX_WAV_MEMORY) {
            LOG_WARN("SoundManager", "loadSingleSound SKIP (memory full): '%s' size=%u mem=%lluMB",
                     filename.c_str(), entry.size,
                     (unsigned long long)(currentTotalMemory / 1024 / 1024));
            return;
        }

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
                LOG_INFO("SoundManager", "loaded (boxwav): '%s' size=%u mem=%lluMB",
                         filename.c_str(), entry.size,
                         (unsigned long long)(currentTotalMemory / 1024 / 1024));
            } else {
                fprintf(stderr, "[SoundManager] Mix_LoadWAV_RW failed: %s (%s)\n",
                        filename.c_str(), Mix_GetError());
                LOG_ERROR("SoundManager", "Mix_LoadWAV_RW failed (boxwav): '%s' err=%s",
                          filename.c_str(), Mix_GetError());
            }
            return;
        }
    }

    // 外部ファイル読み込み
    std::string path = rootPath + (rootPath.empty() || rootPath.back() == '/' ? "" : "/") + filename;
    SDL_RWops* rw = SDL_RWFromFile(path.c_str(), "rb");
    if (!rw) {
        LOG_WARN("SoundManager", "external file not found: '%s'", path.c_str());
        return;
    }

    uint64_t fileSize = SDL_RWsize(rw);
    if (currentTotalMemory + fileSize > MAX_WAV_MEMORY) {
        SDL_RWclose(rw);
        LOG_WARN("SoundManager", "loadSingleSound SKIP (memory full, external): '%s' size=%llu mem=%lluMB",
                 filename.c_str(),
                 (unsigned long long)fileSize,
                 (unsigned long long)(currentTotalMemory / 1024 / 1024));
        return;
    }

    // 外部ファイルは Mix_LoadWAV_RW(freesrc=1) で問題なし。
    // SDL_RWFromFile で開いた RWops は SDL が内部でファイルハンドルを持つため、
    // freesrc=1 で正しく閉じられる。
    Mix_Chunk* chunk = Mix_LoadWAV_RW(rw, 1);
    if (chunk) {
        sounds[id] = chunk;
        currentTotalMemory += fileSize;
        LOG_INFO("SoundManager", "loaded (external): '%s' size=%llu mem=%lluMB",
                 filename.c_str(),
                 (unsigned long long)fileSize,
                 (unsigned long long)(currentTotalMemory / 1024 / 1024));
    } else {
        LOG_ERROR("SoundManager", "Mix_LoadWAV_RW failed (external): '%s' err=%s",
                  filename.c_str(), Mix_GetError());
    }
}

void SoundManager::loadSoundsInBulk(const std::vector<std::string>& filenames,
                                    const std::string& rootPath,
                                    const std::string& bmsonName,
                                    std::function<void(int, const std::string&)> onProgress) {

    std::unordered_map<std::string, std::vector<std::string>> groupPerBox;
    std::vector<std::string> externalFiles;

    for (const auto& name : filenames) {
        if (sounds.find(getHash(name)) != sounds.end()) {
            LOG_INFO("SoundManager", "bulk SKIP (already loaded): '%s'", name.c_str());
            continue;
        }
        if (boxIndex.count(name)) {
            groupPerBox[boxIndex[name].pckPath].push_back(name);
        } else {
            LOG_WARN("SoundManager", "bulk not in boxIndex (->external): '%s'", name.c_str());
            externalFiles.push_back(name);
        }
    }

    LOG_INFO("SoundManager", "loadSoundsInBulk: total=%zu boxGroups=%zu external=%zu",
             filenames.size(), groupPerBox.size(), externalFiles.size());

    int processedCount = 0;

    for (auto& [pckPath, list] : groupPerBox) {
        // オフセット順にソートしてシーク回数を最小化
        std::sort(list.begin(), list.end(), [&](const std::string& a, const std::string& b) {
            return boxIndex[a].offset < boxIndex[b].offset;
        });

        std::ifstream ifs(pckPath, std::ios::binary);
        if (!ifs) {
            LOG_ERROR("SoundManager", "bulk: failed to open boxwav '%s'", pckPath.c_str());
            continue;
        }
        LOG_INFO("SoundManager", "bulk: processing boxwav '%s' (%zu entries)", pckPath.c_str(), list.size());

        for (const auto& name : list) {
            auto& entry = boxIndex[name];
            if (currentTotalMemory + entry.size > MAX_WAV_MEMORY) {
                LOG_WARN("SoundManager", "bulk SKIP (memory full): '%s' size=%u mem=%lluMB",
                         name.c_str(), entry.size,
                         (unsigned long long)(currentTotalMemory / 1024 / 1024));
                processedCount++;
                if (onProgress) onProgress(processedCount, name);
                continue;
            }

            if (loadBuffer.size() < entry.size)
                loadBuffer.resize(entry.size);

            ifs.seekg(entry.offset);
            ifs.read((char*)loadBuffer.data(), entry.size);

            // read の実際の読み取りバイト数を確認（部分読み取り検出）
            std::streamsize readBytes = ifs.gcount();
            if (readBytes != (std::streamsize)entry.size) {
                LOG_ERROR("SoundManager", "bulk read SHORT: '%s' expected=%u got=%lld offset=%u",
                          name.c_str(), entry.size, (long long)readBytes, entry.offset);
                ifs.clear(); // eofbit/failbitをリセットして次のseekgを有効にする
                processedCount++;
                if (onProgress) onProgress(processedCount, name);
                continue;
            }

            SDL_RWops* rwIndiv = SDL_RWFromMem(loadBuffer.data(), (int)entry.size);
            Mix_Chunk* chunk = Mix_LoadWAV_RW(rwIndiv, 0);
            SDL_RWclose(rwIndiv);

            if (chunk) {
                sounds[getHash(name)] = chunk;
                currentTotalMemory += entry.size;
            } else {
                fprintf(stderr, "[SoundManager] Mix_LoadWAV_RW failed: %s (%s)\n",
                        name.c_str(), Mix_GetError());
                LOG_ERROR("SoundManager", "Mix_LoadWAV_RW failed (bulk): '%s' err=%s",
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

    LOG_INFO("SoundManager", "loadSoundsInBulk done: sounds.size=%zu mem=%lluMB/%lluMB",
             sounds.size(),
             (unsigned long long)(currentTotalMemory / 1024 / 1024),
             (unsigned long long)(MAX_WAV_MEMORY   / 1024 / 1024));
}

void SoundManager::play(int soundId) {
    uint32_t id = static_cast<uint32_t>(soundId);
    auto it = sounds.find(id);
    if (it != sounds.end() && it->second != nullptr) {
        Mix_Chunk* targetChunk = it->second;
        int newChannel = Mix_PlayChannel(-1, targetChunk, 0);

        if (newChannel == -1) {
            // 全チャンネル使用中 → 強奪
            LOG_WARN("SoundManager", "all channels busy, stealing ch=%d (soundId=%u)", nextVictim, id);
            newChannel = nextVictim;
            Mix_HaltChannel(newChannel);
            Mix_PlayChannel(newChannel, targetChunk, 0);
            nextVictim = (nextVictim + 1) % 256;
        }

        if (newChannel != -1) {
            Mix_Volume(newChannel, 96);
            activeChannels[id] = newChannel;
        }
    } else {
        LOG_WARN("SoundManager", "play: soundId=%u not in sounds (not loaded?)", id);
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
    LOG_INFO("SoundManager", "clear() start: sounds=%zu mem=%lluMB",
             sounds.size(),
             (unsigned long long)(currentTotalMemory / 1024 / 1024));
    stopAll();
    for (auto& pair : sounds) {
        if (pair.second) Mix_FreeChunk(pair.second);
    }
    std::unordered_map<uint32_t, Mix_Chunk*>().swap(sounds);
    std::unordered_map<uint32_t, int>().swap(activeChannels);

    boxIndex.clear();
    currentTotalMemory = 0;
    nextVictim = 0;
    sounds.reserve(4000);

    Mix_AllocateChannels(256);
    LOG_INFO("SoundManager", "clear() done");
}

void SoundManager::cleanup() {
    clear();
    Mix_CloseAudio();
}
















