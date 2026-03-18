#include "SceneSelectView.hpp"
#include "SceneSelect.hpp"
#include "Config.hpp"
#include <algorithm>
#include <vector>
#include <string>
#include <ctime>

void SceneSelectView::render(SDL_Renderer* ren, NoteRenderer& renderer, 
                             const std::vector<SongGroup>& songGroups, 
                             const std::vector<SongEntry>& songCache,
                             int selectedIndex, int currentState, 
                             bool isShowingExitDialog, int exitDialogSelection,
                             int categoryIndex,
                             int detailIndex,
                             int currentStage) { 
    
    // --- 現在時刻の取得ロジック ---
    time_t now = time(nullptr);
    struct tm* pnow = localtime(&now);
    char timeStr[16];
    bool blink = (SDL_GetTicks() / 500) % 2 == 0;
    snprintf(timeStr, sizeof(timeStr), blink ? "%02d:%02d" : "%02d %02d", pnow->tm_hour, pnow->tm_min);

    SDL_SetRenderDrawColor(ren, 10, 15, 30, 255);
    SDL_RenderClear(ren);

    // --- ソート状態の表示（左上に配置） ---
    std::string sortText = "SORT: " + Config::SORT_NAME; 
    renderer.drawText(ren, sortText, 20, 10, {200, 200, 200, 255}, false, false, false, "");

    // --- ステージ数の表示（フリープレイ用：無限加算表示） ★修正箇所 ---
    char stageStr[32];
    // FINAL判定を撤廃し、単純に現在のプレイ回数を STAGE X として表示します
    snprintf(stageStr, sizeof(stageStr), "STAGE %d", currentStage);
    renderer.drawText(ren, stageStr, 20, 45, {255, 255, 0, 255}, false, false, false, "");

    if (songGroups.empty()) {
        BMSHeader emptyH = {"No .bmson found", "Check BMS folder", "", "", 0.0, 480, 0.0, 0.0, 0};
        renderer.renderUI(ren, emptyH, 60, 0, 0);
    } else {
        int totalGroups = (int)songGroups.size();
        int safeSelectedIndex = std::max(0, std::min(selectedIndex, totalGroups - 1));
        const auto& group = songGroups[safeSelectedIndex];
        
        bool fastBlink4f = (SDL_GetTicks() / 64) % 2 == 0;

        int listX = 820, centerY = 360, itemH = 48;
        for (int i = -8; i <= 8; ++i) {
            int targetIdx = (safeSelectedIndex + i) % totalGroups;
            if (targetIdx < 0) targetIdx += totalGroups; 

            const auto& gItem = songGroups[targetIdx];
            int yPos = centerY + (i * itemH) - (itemH / 2);
            int xOffset = (i == 0) ? -40 : 0;

            if (gItem.isFolder || gItem.songIndices.empty()) {
                SDL_Rect r = { listX + xOffset, yPos, 460, itemH - 4 };
                SDL_SetRenderDrawColor(ren, (i == 0) ? 80 : 50, (i == 0) ? 70 : 40, (i == 0) ? 40 : 30, 255);
                SDL_RenderFillRect(ren, &r);
                SDL_Rect lRect = { listX + xOffset - 15, yPos + 8, 10, itemH - 20 };
                SDL_SetRenderDrawColor(ren, 255, 165, 0, 255);
                SDL_RenderFillRect(ren, &lRect);
                renderer.drawText(ren, gItem.title, listX + xOffset + 15, yPos + 10, {255, 255, 255, 255}, false, false, false, "");
            } else {
                int diffIdx = std::max(0, std::min((int)gItem.currentDiffIdx, (int)gItem.songIndices.size() - 1));
                size_t cacheIdx = gItem.songIndices[diffIdx];
                if (cacheIdx < songCache.size()) {
                    const SongEntry& item = songCache[cacheIdx];
                    SDL_Color lampCol = {60, 60, 60, 255};
                    switch (item.clearType) {
                        case ClearType::FAILED:         lampCol = {180, 20, 20, 255};   break;
                        case ClearType::DAN_CLEAR:      lampCol = fastBlink4f ? SDL_Color{255, 255, 255, 255} : SDL_Color{100, 100, 100, 255}; break;
                        case ClearType::ASSIST_CLEAR:   lampCol = {180, 100, 255, 255}; break;
                        case ClearType::EASY_CLEAR:     lampCol = {0, 255, 0, 255};     break;
                        case ClearType::NORMAL_CLEAR:   lampCol = {0, 200, 255, 255};   break;
                        case ClearType::HARD_CLEAR:     lampCol = {255, 255, 255, 255}; break;
                        case ClearType::EX_HARD_CLEAR:  lampCol = {255, 255, 0, 255};   break;
                        case ClearType::FULL_COMBO:     lampCol = fastBlink4f ? SDL_Color{255, 255, 0, 255} : SDL_Color{0, 255, 255, 255}; break;
                        default: break;
                    }
                    if (item.clearType != ClearType::NO_PLAY) {
                        SDL_Rect lRect = { listX + xOffset - 15, yPos + 8, 10, itemH - 20 };
                        SDL_SetRenderDrawColor(ren, lampCol.r, lampCol.g, lampCol.b, 255);
                        SDL_RenderFillRect(ren, &lRect);
                    }
                    SDL_Rect r = { listX + xOffset, yPos, 460, itemH - 4 };
                    SDL_SetRenderDrawColor(ren, (i == 0) ? 60 : 40, (i == 0) ? 100 : 45, (i == 0) ? 200 : 60, 255);
                    SDL_RenderFillRect(ren, &r);
                    renderer.drawText(ren, item.title, listX + xOffset + 15, yPos + 10, {255, 255, 255, 255}, false, false, false, "");
                }
            }
        }

        if (!group.customLogo.empty()) {
            renderer.drawImage(ren, group.customLogo, 70, 140, 158, 72, 255);
        }

        std::string fPath = group.customFont.empty() ? "" : group.customFont;
        if (group.isFolder || group.songIndices.empty()) {
            renderer.drawText(ren, group.title, 80, 220, {255, 255, 255, 255}, true, false, false, fPath);
            renderer.drawText(ren, "DIRECTORY", 90, 280, {200, 200, 200, 255}, false, false, false, "");
            renderer.drawText(ren, "PRESS KEY 1 TO ENTER", 90, 350, {255, 255, 0, 255}, false, false, false, "");
            renderer.drawText(ren, "PRESS KEY 2 TO BACK", 90, 385, {0, 255, 255, 255}, false, false, false, "");
        } else {
            int dIdx = std::max(0, std::min((int)group.currentDiffIdx, (int)group.songIndices.size() - 1));
            size_t sIdx = group.songIndices[dIdx];
            if (sIdx < songCache.size()) {
                const SongEntry& s = songCache[sIdx];
                renderer.drawText(ren, s.title, 80, 220, {255, 255, 255, 255}, true, false, false, fPath);
                renderer.drawText(ren, s.artist, 90, 280, {200, 200, 200, 255}, false, false, false, "");
                for (int i = 0; i < (int)group.songIndices.size(); ++i) {
                    size_t eIdx = group.songIndices[i];
                    if (eIdx >= songCache.size()) continue;
                    const auto& entry = songCache[eIdx];
                    int slot = SceneSelect::getDifficultyOrder(entry);
                    SDL_Color c = {100, 100, 100, 255};
                    if (i == group.currentDiffIdx) {
                        if (slot == 0) c = {0, 255, 0, 255};
                        else if (slot == 1) c = {100, 100, 255, 255};
                        else if (slot == 2) c = {255, 255, 0, 255};
                        else if (slot == 3) c = {255, 50, 50, 255};
                        else if (slot == 4) c = {255, 50, 255, 255};
                        else c = {255, 255, 255, 255};
                    }
                    char rowTxt[128];
                    snprintf(rowTxt, sizeof(rowTxt), "[%s] LV.%d", entry.chartName.c_str(), entry.level);
                    renderer.drawText(ren, rowTxt, 90, 350 + (i * 35), c, false, false, false, "");
                }
                char bpmStr[32]; snprintf(bpmStr, sizeof(bpmStr), "BPM %03.0f", s.bpm);
                renderer.drawText(ren, bpmStr, 400, 340, {255, 255, 255, 255}, false, false, false, "");
                char notesStr[32]; snprintf(notesStr, sizeof(notesStr), "NOTES: %d", s.totalNotes); 
                renderer.drawText(ren, notesStr, 400, 375, {255, 255, 255, 255}, false, false, false, "");
                int pX = 60, pY = 520;
                SDL_Color rCol = {255, 255, 255, 255};
                if (s.rank == "AAA") rCol = {255, 215, 0, 255};
                else if (s.rank == "AA") rCol = {192, 192, 192, 255};
                char sT[64], cT[64];
                snprintf(sT, sizeof(sT), "EX SCORE: %d", s.exScore);
                snprintf(cT, sizeof(cT), "MAX COMBO: %d", s.maxCombo);
                renderer.drawText(ren, sT, pX, pY, {255, 255, 255, 255}, false, false, false, "");
                renderer.drawText(ren, cT, pX, pY + 40, {255, 255, 255, 255}, false, false, false, "");
                renderer.drawText(ren, s.rank, pX + 350, pY - 10, rCol, true, false, false, "");
                std::string mStr = "NO PLAY"; SDL_Color mCol = {150, 150, 150, 255};
                switch (s.clearType) {
                    case ClearType::FAILED:        mStr = "FAILED";        mCol = {180, 20, 20, 255};   break;
                    case ClearType::DAN_CLEAR:      mStr = "DAN CLEAR";     mCol = fastBlink4f ? SDL_Color{255, 255, 255, 255} : SDL_Color{150, 150, 150, 255}; break;
                    case ClearType::ASSIST_CLEAR:   mStr = "ASSIST CLEAR";  mCol = {180, 100, 255, 255}; break;
                    case ClearType::EASY_CLEAR:     mStr = "EASY CLEAR";    mCol = {0, 255, 0, 255};     break;
                    case ClearType::NORMAL_CLEAR:   mStr = "NORMAL CLEAR";  mCol = {0, 200, 255, 255};   break;
                    case ClearType::HARD_CLEAR:     mStr = "HARD CLEAR";    mCol = {255, 255, 255, 255}; break;
                    case ClearType::EX_HARD_CLEAR:  mStr = "EX-HARD CLEAR"; mCol = {255, 255, 0, 255};   break;
                    case ClearType::FULL_COMBO:     mStr = "FULL COMBO";    mCol = fastBlink4f ? SDL_Color{255, 255, 0, 255} : SDL_Color{0, 255, 255, 255}; break;
                    default: break;
                }
                if (s.clearType != ClearType::NO_PLAY) renderer.drawText(ren, mStr, pX, pY + 80, mCol, false, false, false, "");
                SDL_Rect bgB = { pX, pY + 130, 450, 12 };
                SDL_SetRenderDrawColor(ren, 40, 40, 40, 255);
                SDL_RenderFillRect(ren, &bgB);
                double maxExScore = (double)s.totalNotes * 2.0;
                double prog = (maxExScore > 0) ? std::min(1.0, (double)s.exScore / maxExScore) : 0.0;
                SDL_Rect prgB = { pX, pY + 130, (int)(450 * prog), 12 };
                SDL_SetRenderDrawColor(ren, 0, 200, 255, 255);
                SDL_RenderFillRect(ren, &prgB);
                char perStr[16];
                snprintf(perStr, sizeof(perStr), "%.2f%%", prog * 100.0);
                renderer.drawText(ren, perStr, pX + 460, pY + 125, {255, 255, 255, 255}, false, false, false, "");
            }
        }
    }

    renderer.drawText(ren, timeStr, 1150, 10, {200, 200, 200, 255}, true, false, false, "");

    if (currentState == (int)SelectState::EDIT_OPTION) renderOptionOverlay(ren, renderer, categoryIndex);
    if (currentState == (int)SelectState::EDIT_DETAIL) renderDetailOptionOverlay(ren, renderer, detailIndex);
    if (isShowingExitDialog) renderExitDialog(ren, renderer, exitDialogSelection);
}

void SceneSelectView::renderOptionOverlay(SDL_Renderer* ren, NoteRenderer& renderer, int categoryIndex) {
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 180);
    SDL_Rect screenBg = { 0, 0, 1280, 720 };
    SDL_RenderFillRect(ren, &screenBg);

    struct Category { std::string name; SDL_Color color; std::vector<std::string> items; int current; std::string keys; };
    std::vector<Category> cats = {
        {"STYLE", {255, 140, 0, 255}, {"OFF", "RANDOM", "R-RANDOM", "S-RANDOM", "MIRROR"}, Config::PLAY_OPTION, "LEFT / RIGHT"},
        {"GAUGE", {50, 205, 50, 255}, {"OFF", "A-EASY", "EASY", "HARD", "EX-HARD", "DAN", "HAZARD"}, Config::GAUGE_OPTION, "UP / DOWN"},
        {"ASSIST", {147, 112, 219, 255}, {"OFF", "AUTO SCR", "LEGACY", "5KEYS", "ASCR+LEG", "ASCR+5K", "FULL ASST", "AUTO PLAY", "SCR ONLY"}, Config::ASSIST_OPTION, "LEVEL BUTTON"},
        {"EX", {0, 191, 255, 255}, {"OFF", "ALL SCR", "MORE NOTES"}, Config::EX_OPTION, "TO DETAIL"}
    };

    int panelW = 260, panelH = 500, startX = 100, startY = 110;
    for (int i = 0; i < (int)cats.size(); i++) {
        int currentIdx = cats[i].current;
        if (currentIdx >= (int)cats[i].items.size()) currentIdx = (int)cats[i].items.size() - 1;
        int x = startX + (i * (panelW + 25));
        if (i == categoryIndex) {
            SDL_SetRenderDrawColor(ren, 255, 255, 0, 40); 
            SDL_Rect highlight = { x, startY, panelW, panelH };
            SDL_RenderFillRect(ren, &highlight);
            SDL_SetRenderDrawColor(ren, 255, 255, 0, 255);
        } else {
            SDL_SetRenderDrawColor(ren, cats[i].color.r, cats[i].color.g, cats[i].color.b, 255);
        }
        SDL_Rect border = { x, startY, panelW, panelH };
        SDL_RenderDrawRect(ren, &border);
        SDL_SetRenderDrawColor(ren, cats[i].color.r, cats[i].color.g, cats[i].color.b, 120);
        SDL_Rect header = { x, startY, panelW, 40 };
        SDL_RenderFillRect(ren, &header);
        renderer.drawText(ren, cats[i].name, x + (panelW/2 - 30), startY + 5, {255,255,255,255}, false, false, false, "");

        for (int j = 0; j < (int)cats[i].items.size(); j++) {
            int itemY = startY + 50 + (j * 42);
            SDL_Rect itemR = { x + 10, itemY, panelW - 20, 35 };
            if (j == currentIdx) {
                SDL_SetRenderDrawColor(ren, cats[i].color.r, cats[i].color.g, cats[i].color.b, 255);
                SDL_RenderFillRect(ren, &itemR);
                renderer.drawText(ren, cats[i].items[j], x + 20, itemY + 5, {0,0,0,255}, false, false, false, "");
            } else {
                SDL_SetRenderDrawColor(ren, 30, 30, 40, 255);
                SDL_RenderFillRect(ren, &itemR);
                renderer.drawText(ren, cats[i].items[j], x + 20, itemY + 5, {255,255,255,255}, false, false, false, "");
            }
        }
        SDL_SetRenderDrawColor(ren, (i == categoryIndex ? 100 : 60), (i == categoryIndex ? 100 : 60), (i == categoryIndex ? 0 : 70), 255);
        SDL_Rect guideR = { x + 10, startY + panelH - 45, panelW - 20, 35 };
        SDL_RenderFillRect(ren, &guideR);
        renderer.drawText(ren, cats[i].keys, x + (panelW/2 - 45), startY + panelH - 40, {255,255,255,255}, false, false, false, "");
    }
    char hsText[64]; 
    snprintf(hsText, sizeof(hsText), "HS: %.2f (GN: %d)   LEFT/RIGHT: CATEGORY / UP/DOWN: CHANGE", Config::HIGH_SPEED, Config::GREEN_NUMBER);
    renderer.drawText(ren, hsText, 640, 60, {0, 255, 255, 255}, false, true, false, ""); 
    renderer.drawText(ren, "DECIDE+BACK to RE-SCAN", 640, 90, {255, 255, 0, 255}, false, true, false, "");
}

void SceneSelectView::renderDetailOptionOverlay(SDL_Renderer* ren, NoteRenderer& renderer, int detailIndex) {
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 180);
    SDL_Rect screenBg = { 0, 0, 1280, 720 };
    SDL_RenderFillRect(ren, &screenBg);

    int panelW = 190, panelH = 500, startX = 135, startY = 110; 
    SDL_Color themeCol = {0, 191, 255, 255};

    struct DetailPanel { std::string title; int type; };
    std::vector<DetailPanel> panels = {
        {"JUDGE OFFSET", 0}, {"FAST / SLOW", 1}, {"VF NOTES MIN", 2}, {"VF NOTES MAX", 3}, {"DAN START %", 4}
    };

    for (int i = 0; i < (int)panels.size(); ++i) {
        int x = startX + (i * (panelW + 15));
        bool isActive = (detailIndex == i);
        if (isActive) {
            SDL_SetRenderDrawColor(ren, 255, 255, 0, 40); 
            SDL_Rect highlight = { x, startY, panelW, panelH };
            SDL_RenderFillRect(ren, &highlight);
            SDL_SetRenderDrawColor(ren, 255, 255, 0, 255);
        } else {
            SDL_SetRenderDrawColor(ren, themeCol.r, themeCol.g, themeCol.b, 255);
        }
        SDL_Rect border = { x, startY, panelW, panelH };
        SDL_RenderDrawRect(ren, &border);
        SDL_SetRenderDrawColor(ren, themeCol.r, themeCol.g, themeCol.b, 120);
        SDL_Rect header = { x, startY, panelW, 40 };
        SDL_RenderFillRect(ren, &header);
        renderer.drawText(ren, panels[i].title, x + (panelW/2 - 50), startY + 5, {255,255,255,255}, false, false, false, "");

        if (panels[i].type == 0 || panels[i].type == 2 || panels[i].type == 3 || panels[i].type == 4) {
            int sliderCenterX = x + panelW / 2;
            int sliderCenterY = startY + 220;
            SDL_SetRenderDrawColor(ren, 60, 60, 60, 255);
            SDL_Rect track = { sliderCenterX - 2, sliderCenterY - 100, 4, 200 };
            SDL_RenderFillRect(ren, &track);

            int currentVal = 0;
            char valStr[32];
            if (panels[i].type == 0) {
                currentVal = (int)Config::JUDGE_OFFSET;
                snprintf(valStr, sizeof(valStr), "%+d ms", currentVal);
            } else if (panels[i].type == 2) {
                currentVal = Config::FOLDER_NOTES_MIN / 10;
                snprintf(valStr, sizeof(valStr), "%d", Config::FOLDER_NOTES_MIN);
            } else if (panels[i].type == 3) {
                currentVal = Config::FOLDER_NOTES_MAX / 10;
                snprintf(valStr, sizeof(valStr), "%d", Config::FOLDER_NOTES_MAX);
            } else if (panels[i].type == 4) {
                currentVal = Config::DAN_GAUGE_START_PERCENT - 50; 
                snprintf(valStr, sizeof(valStr), "%d%%", Config::DAN_GAUGE_START_PERCENT);
            }
            int knobY = sliderCenterY - (currentVal * 2); 
            knobY = std::max(sliderCenterY - 100, std::min(sliderCenterY + 100, knobY));
            SDL_SetRenderDrawColor(ren, 255, 255, 255, 255);
            SDL_Rect knob = { sliderCenterX - 20, knobY - 5, 40, 10 };
            SDL_RenderFillRect(ren, &knob);
            renderer.drawText(ren, valStr, sliderCenterX, startY + 360, {255, 255, 255, 255}, true, true, false, "");
        } else {
            const char* labels[] = {"OFF", "ON", "DETAIL"};
            int selection = std::clamp(Config::SHOW_FAST_SLOW, 0, 2);
            for (int j = 0; j < 3; j++) {
                int itemY = startY + 50 + (j * 42); 
                SDL_Rect itemR = { x + 10, itemY, panelW - 20, 35 };
                if (j == selection) {
                    SDL_SetRenderDrawColor(ren, themeCol.r, themeCol.g, themeCol.b, 255);
                    SDL_RenderFillRect(ren, &itemR);
                    renderer.drawText(ren, labels[j], x + 20, itemY + 5, {0,0,0,255}, false, false, false, "");
                } else {
                    SDL_SetRenderDrawColor(ren, 30, 30, 40, 255);
                    SDL_RenderFillRect(ren, &itemR);
                    renderer.drawText(ren, labels[j], x + 20, itemY + 5, {255,255,255,255}, false, false, false, "");
                }
            }
        }
        SDL_SetRenderDrawColor(ren, (isActive ? 100 : 60), (isActive ? 100 : 60), (isActive ? 0 : 70), 255);
        SDL_Rect guideR = { x + 10, startY + panelH - 45, panelW - 20, 35 };
        SDL_RenderFillRect(ren, &guideR);
        std::string guideTxt = (panels[i].type == 1) ? "DECIDE" : "UP / DOWN";
        renderer.drawText(ren, guideTxt, x + (panelW/2 - 40), startY + panelH - 40, {255,255,255,255}, false, false, false, "");
    }
    renderer.drawText(ren, "DETAIL OPTION", 640, 60, {0, 255, 255, 255}, false, true, false, ""); 
    renderer.drawText(ren, "LEFT / RIGHT: SELECT CATEGORY | LEVEL BUTTON: BACK", 640, 650, {200, 200, 200, 255}, false, true, false, "");
}

void SceneSelectView::renderExitDialog(SDL_Renderer* ren, NoteRenderer& renderer, int selection) {
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 220); 
    SDL_Rect bg = { 0, 0, 1280, 720 }; 
    SDL_RenderFillRect(ren, &bg);
    renderer.drawText(ren, "EXIT GAME?", 640, 260, {255,255,255,255}, true, true, false, "");
    renderer.drawText(ren, "YES", 540, 360, (selection == 1 ? SDL_Color{255,255,0,255} : SDL_Color{120,120,120,255}), true, true, false, "");
    renderer.drawText(ren, "NO", 740, 360, (selection == 0 ? SDL_Color{255,255,0,255} : SDL_Color{120,120,120,255}), true, true, false, "");
    SDL_Color backCol = (selection == 2 ? SDL_Color{255,255,0,255} : SDL_Color{120,120,120,255});
    renderer.drawText(ren, "BACK TO MODE SELECT", 640, 440, backCol, true, true, false, "");
    renderer.drawText(ren, "SCRATCH: SELECT YES/NO | LEVEL BUTTON: BACK TO MODE SELECT", 640, 540, {200,200,200,255}, false, true, false, "");
    renderer.drawText(ren, "PRESS DECIDE KEY TO CONFIRM", 640, 580, {255,255,255,255}, false, true, false, "");
}






