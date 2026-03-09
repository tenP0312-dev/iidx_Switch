#ifndef SCENESELECT_HPP
#define SCENESELECT_HPP

#include <vector>
#include <string>
#include <SDL2/SDL.h>
#include "NoteRenderer.hpp"
#include "BMSData.hpp"
#include "CommonTypes.hpp"

struct SongEntry {
    std::string filename;
    std::string title;
    std::string subtitle;
    std::string artist;
    std::string chartName;
    double bpm;
    int level;
    double total;
    int totalNotes;
    ClearType clearType;
    int exScore;
    int maxCombo;
    std::string rank;
    std::string modeHint;
    std::string previewPath;
    std::string rootDir;
};

struct SongGroup {
    std::string title;
    std::string sortName;
    std::vector<int> songIndices;
    int currentDiffIdx = 0;
    bool isFolder = false;
    std::string folderPath = "";
    std::string customFont = "";
    std::string customLogo = "";
    std::string customSE   = "";
};

enum class SelectState {
    SELECT_SONG,
    EDIT_OPTION,
    EDIT_DETAIL
};

enum class SortMode {
    ALPHABET,
    LEVEL,
    CLEAR_LAMP,
    SCORE,
    BPM,
    ARTIST,   // アーティスト名昇順
    NOTES,    // ノーツ数昇順
    MAX_COUNT
};

class SceneSelect {
public:
    static int getDifficultyOrder(const SongEntry& entry);
    void init(bool forceScan, SDL_Renderer* ren, NoteRenderer& renderer, int currentStage);
    std::string update(SDL_Renderer* ren, NoteRenderer& renderer, int currentStage, bool& quit);
    bool shouldBackToModeSelect() const { return backToModeSelectRequested; }

public:
    SelectState currentState = SelectState::SELECT_SONG;
    SortMode    currentSort  = SortMode::ALPHABET;
    std::vector<SongEntry> songCache;
    std::vector<SongGroup> songGroups;
    int selectedIndex        = 0;
    int preferredDifficultySlot = 1;
    int lastSelectedIndex    = -1;
    int lastDifficultyIdx    = -1;
    BMSHeader  cachedHeader;
    BestScore  cachedBest;
    bool isShowingExitDialog = false;
    int  exitDialogSelection = 0;
    bool startButtonPressed  = false;
    bool effectButtonPressed = false;

    bool isExtraFolderSelected()   const;
    bool isOneMoreFolderSelected() const;

    bool   scrUpPressed   = false;
    bool   scrDownPressed = false;
    Uint32 lastScrollTime = 0;

    int optionCategoryIndex = 0;
    int detailOptionIndex   = 0;

private:
    void prepareSongList(bool forceScan, SDL_Renderer* ren, NoteRenderer& renderer, int currentStage);
    void syncDifficultyWithPreferredSlot();
    void applySort();
    void renderOptionOverlay(SDL_Renderer* ren, NoteRenderer& renderer);
    void renderExitDialog(SDL_Renderer* ren, NoteRenderer& renderer);

    bool backToModeSelectRequested  = false;
    bool lastSelectedWasExtra       = false;
    bool lastSelectedWasOneMore     = false;

    // ★修正：フレームカウンタ (previewTimer) を廃止し、ミリ秒ベースのタイマーに変更。
    // フレームカウンタは 60fps では約 0.5 秒、30fps では約 1 秒と動作が変わり、
    // Switch のフレームレートが揺れた際に再生タイミングが不定になる問題があった。
    uint32_t previewPendingStartTime = 0; // 選曲変化を検知した時刻 (SDL_GetTicks)
    bool     previewPending          = false;
    static constexpr uint32_t PREVIEW_DELAY_MS = 500; // 0.5秒後にプレビュー再生
};

#endif // SCENESELECT_HPP





