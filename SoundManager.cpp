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
                        boxIndex[fileName] = { rPath, (uint64_t)rifs.tellg(), fSize };
                        rifs.seekg((std::streamoff)fSize, std::ios::cur);
                        if (!rifs) { rifs.clear(); break; }
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
                        boxIndex[fileName] = { rPath, (uint64_t)rifs.tellg(), fSize };
                        rifs.seekg((std::streamoff)fSize, std::ios::cur);
                        if (!rifs) { rifs.clear(); break; }
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
                    boxIndex[fileName] = { curPath, (uint64_t)ifs.tellg(), fSize };
                    ifs.seekg((std::streamoff)fSize, std::ios::cur);
                    if (!ifs) { ifs.clear(); break; }
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
                boxIndex[fileName] = { curPath, (uint64_t)ifs.tellg(), fSize };
                ifs.seekg((std::streamoff)fSize, std::ios::cur);
                if (!ifs) { ifs.clear(); break; }
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
            if (loadBuffer.size() < entry.size)
                loadBuffer.resize(entry.size);

            ifs.seekg((std::streamoff)entry.offset);
            if (!ifs) {
                LOG_ERROR("SoundManager", "loadSingleSound seekg failed: '%s' offset=%llu",
                          filename.c_str(), (unsigned long long)entry.offset);
                return;
            }
            ifs.read((char*)loadBuffer.data(), entry.size);
            std::streamsize readBytes = ifs.gcount();
            if (readBytes != (std::streamsize)entry.size) {
                LOG_ERROR("SoundManager", "loadSingleSound read SHORT: '%s' expected=%u got=%lld",
                          filename.c_str(), entry.size, (long long)readBytes);
                return;
            }

            // loadBuffer.data() を使い終わるまで resize しないよう、
            // ローカルポインタとサイズを確定してから RWops を作る
            uint8_t* ptr  = loadBuffer.data();
            int       sz  = (int)entry.size; // entry.size は uint32_t なので int で安全
            SDL_RWops* rw = SDL_RWFromMem(ptr, sz);
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
    // 候補パスを順に試す:
    // ① rootPath + 元のファイル名
    // ② rootPath + 同ステム + 別音声拡張子（SDL_mixerが対応するもの）
    static const std::vector<std::string> AUDIO_EXTS = {
        ".wav", ".WAV", ".ogg", ".OGG", ".mp3", ".MP3",
        ".flac", ".FLAC", ".opus", ".OPUS"
    };

    // ステム取り出しヘルパー
    auto getStem = [](const std::string& fname) -> std::string {
        size_t dot = fname.find_last_of('.');
        return (dot != std::string::npos) ? fname.substr(0, dot) : fname;
    };

    std::string dir = rootPath.empty() ? "" : (rootPath.back() == '/' ? rootPath : rootPath + "/");
    std::string stem = getStem(filename);

    // 候補リスト構築
    std::vector<std::string> candidates;
    candidates.push_back(dir + filename);           // ① 元のファイル名
    for (const auto& ext : AUDIO_EXTS) {
        std::string alt = dir + stem + ext;
        if (alt != dir + filename) candidates.push_back(alt);  // ② 別拡張子
    }

    SDL_RWops* rw = nullptr;
    std::string resolvedPath;
    for (const auto& cand : candidates) {
        rw = SDL_RWFromFile(cand.c_str(), "rb");
        if (rw) {
            resolvedPath = cand;
            if (cand != dir + filename)
                LOG_INFO("SoundManager", "loadSingleSound: resolved '%s' -> '%s'",
                         filename.c_str(), cand.c_str());
            break;
        }
    }
    if (!rw) {
        LOG_WARN("SoundManager", "external file not found: '%s'", (dir + filename).c_str());
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

    Mix_Chunk* chunk = Mix_LoadWAV_RW(rw, 1);
    if (chunk) {
        sounds[id] = chunk;
        currentTotalMemory += fileSize;
        LOG_INFO("SoundManager", "loaded (external): '%s' size=%llu mem=%lluMB",
                 resolvedPath.c_str(),
                 (unsigned long long)fileSize,
                 (unsigned long long)(currentTotalMemory / 1024 / 1024));
    } else {
        LOG_ERROR("SoundManager", "Mix_LoadWAV_RW failed (external): '%s' err=%s",
                  resolvedPath.c_str(), Mix_GetError());
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

            ifs.seekg((std::streamoff)entry.offset);
            if (!ifs) {
                LOG_ERROR("SoundManager", "bulk seekg failed: '%s' offset=%llu",
                          name.c_str(), (unsigned long long)entry.offset);
                ifs.clear();
                processedCount++;
                if (onProgress) onProgress(processedCount, name);
                continue;
            }
            ifs.read((char*)loadBuffer.data(), entry.size);

            // read の実際の読み取りバイト数を確認（部分読み取り検出）
            std::streamsize readBytes = ifs.gcount();
            if (readBytes != (std::streamsize)entry.size) {
                LOG_ERROR("SoundManager", "bulk read SHORT: '%s' expected=%u got=%lld offset=%llu",
                          name.c_str(), entry.size, (long long)readBytes,
                          (unsigned long long)entry.offset);
                ifs.clear(); // eofbit/failbitをリセットして次のseekgを有効にする
                processedCount++;
                if (onProgress) onProgress(processedCount, name);
                continue;
            }

            // loadBuffer.data() を使い終わるまで resize しないよう、
            // ローカルポインタとサイズを確定してから RWops を作る
            uint8_t* ptr    = loadBuffer.data();
            int       sz    = (int)entry.size; // entry.size は uint32_t なので int で安全
            SDL_RWops* rwIndiv = SDL_RWFromMem(ptr, sz);
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
        }
    } else {
        LOG_WARN("SoundManager", "play: soundId=%u not in sounds (not loaded?)", id);
    }
}

void SoundManager::playByName(const std::string& name) {
    play(static_cast<int>(getHash(name)));
}

void SoundManager::playPreview(const std::string& fullPath) {
    if (lastPreviewPath == fullPath && currentPreviewChunk != nullptr) return;
    stopPreview();

    SDL_RWops* rw = SDL_RWFromFile(fullPath.c_str(), "rb");
    if (!rw) return;

    Mix_Chunk* previewChunk = Mix_LoadWAV_RW(rw, 1);
    if (previewChunk) {
        Mix_PlayChannel(255, previewChunk, -1);
        Mix_Volume(255, 80);
        currentPreviewChunk = previewChunk;
        lastPreviewPath = fullPath;
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

    boxIndex.clear();
    currentTotalMemory = 0;
    nextVictim = 0;
    lastPreviewPath.clear(); // 次曲でplayPreview()が確実に再ロードされるようにリセット
    // loadBuffer を解放して曲間にメモリを戻す（次の曲でまた必要なら再確保される）
    std::vector<uint8_t>().swap(loadBuffer);
    sounds.reserve(4000);
    // ★ Mix_AllocateChannels(256) は init() の1回だけ呼べば十分。
    //    clear() のたびに呼ぶと SDL_mixer の内部チャンネル配列が realloc され、
    //    オーディオコールバックスレッドとのロック競合で 50〜70ms のスパイクが発生する。
    //    さらに長時間稼働で realloc を繰り返すとヒープが断片化し、
    //    最終的に SDL_mixer 内部状態が壊れてクラッシュする原因になる。
    LOG_INFO("SoundManager", "clear() done");
}

void SoundManager::cleanup() {
    clear();
    Mix_CloseAudio();
}





















