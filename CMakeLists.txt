cmake_minimum_required(VERSION 3.16)
project(GeminiRhythm)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── ソースファイル ──────────────────────────────────────
set(SOURCES
    main.cpp
    BgaManager.cpp
    BmsonLoader.cpp
    ChartProjector.cpp
    JudgeManager.cpp
    NoteRenderer.cpp
    PlayEngine.cpp
    SceneDecision.cpp
    SceneModeSelect.cpp
    SceneOption.cpp
    ScenePlay.cpp
    SceneResult.cpp
    SceneSelect.cpp
    SceneSelectView.cpp
    SceneSideSelect.cpp
    SceneTitle.cpp
    ScoreManager.cpp
    SongManager.cpp
    SoundManager.cpp
    VirtualFolderManager.cpp
)

add_executable(${PROJECT_NAME} ${SOURCES})

# ── Homebrew パス (M1/M2 → /opt/homebrew, Intel → /usr/local) ──
if(APPLE)
    # arm64 (M1/M2)
    if(CMAKE_HOST_SYSTEM_PROCESSOR STREQUAL "arm64")
        set(BREW_PREFIX "/opt/homebrew")
    else()
        set(BREW_PREFIX "/usr/local")
    endif()
    message(STATUS "Homebrew prefix: ${BREW_PREFIX}")
endif()

# ── SDL2 ────────────────────────────────────────────────
find_package(SDL2 REQUIRED)
find_library(SDL2_MIXER_LIB SDL2_mixer HINTS "${BREW_PREFIX}/lib")
find_library(SDL2_TTF_LIB   SDL2_ttf   HINTS "${BREW_PREFIX}/lib")
find_library(SDL2_IMAGE_LIB SDL2_image HINTS "${BREW_PREFIX}/lib")

if(NOT SDL2_MIXER_LIB)
    message(FATAL_ERROR "SDL2_mixer not found. Run: brew install sdl2_mixer")
endif()
if(NOT SDL2_TTF_LIB)
    message(FATAL_ERROR "SDL2_ttf not found. Run: brew install sdl2_ttf")
endif()

# ── FFmpeg ──────────────────────────────────────────────
find_library(AVCODEC_LIB    avcodec    HINTS "${BREW_PREFIX}/lib")
find_library(AVFORMAT_LIB   avformat   HINTS "${BREW_PREFIX}/lib")
find_library(AVUTIL_LIB     avutil     HINTS "${BREW_PREFIX}/lib")
find_library(SWSCALE_LIB    swscale    HINTS "${BREW_PREFIX}/lib")

if(NOT AVCODEC_LIB)
    message(FATAL_ERROR "FFmpeg not found. Run: brew install ffmpeg")
endif()

# ── インクルードパス ─────────────────────────────────────
target_include_directories(${PROJECT_NAME} PRIVATE
    ${SDL2_INCLUDE_DIRS}
    "${BREW_PREFIX}/include"
)

# ── リンク ──────────────────────────────────────────────
target_link_libraries(${PROJECT_NAME}
    ${SDL2_LIBRARIES}
    ${SDL2_MIXER_LIB}
    ${SDL2_TTF_LIB}
    ${AVCODEC_LIB}
    ${AVFORMAT_LIB}
    ${AVUTIL_LIB}
    ${SWSCALE_LIB}
)

# SDL2_image は任意 (BGA画像に使う場合)
if(SDL2_IMAGE_LIB)
    target_link_libraries(${PROJECT_NAME} ${SDL2_IMAGE_LIB})
endif()

# ── macOS フレームワーク ─────────────────────────────────
if(APPLE)
    target_link_libraries(${PROJECT_NAME}
        "-framework Cocoa"
        "-framework AudioToolbox"
        "-framework CoreAudio"
    )
endif()

# ── コンパイルオプション ─────────────────────────────────
target_compile_options(${PROJECT_NAME} PRIVATE
    -O2
    -Wall
    -Wno-unused-variable
    -Wno-unused-parameter
)

message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "Sources: ${SOURCES}")
