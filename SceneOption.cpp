#include "SceneOption.hpp"

// 表示用ラベルの定義（21項目：プレイキー11種 + システムキー10種）
static const std::vector<std::string> keyLabels = {
    "START / EXIT",
    "EFFECT",
    "LANE 1",
    "LANE 2",
    "LANE 3",
    "LANE 4",
    "LANE 5",
    "LANE 6",
    "LANE 7",
    "SCRATCH A",
    "SCRATCH B",
    "SYS: DECIDE",
    "SYS: BACK",
    "SYS: UP",
    "SYS: DOWN",
    "SYS: LEFT",
    "SYS: RIGHT",
    "SYS: OPTION",
    "SYS: DIFF",
    "SYS: SORT",
    "SYS: RANDOM"
};

void SceneOption::init() {
    state = OptionState::SELECTING_ITEM;
    cursor = 0;
    lastConfigTime = 0;
    configStep = 0;
    repeatTimer = 0;
    updateItemList();
}

void SceneOption::updateItemList() {
    items.clear();
    // Index 0: Section
    items.emplace_back("--- [ KEY CONFIG ] ---", ""); 
    // Index 1-2
    items.emplace_back("[ PLAY KEY CONFIG ]", "11 Keys");
    items.emplace_back("[ SYSTEM KEY CONFIG ]", "10 Keys"); 
    
    auto getValStr = [&](std::string val, int idx) {
        return (state == OptionState::ADJUSTING_VALUE && cursor == idx) ? "< " + val + " >" : val;
    };

    // Index 3: Section
    items.emplace_back("--- [ PLAY SETTINGS ] ---", "");
    // Index 4: GREEN NUMBER
    items.emplace_back("[ GREEN NUMBER ]", getValStr(std::to_string(Config::GREEN_NUMBER), 4));
    // Index 5: LIFT
    items.emplace_back("[ LIFT ]", getValStr(std::to_string(Config::LIFT), 5));
    // Index 6-7: LANE / SCRATCH WIDTH
    items.emplace_back("[ LANE WIDTH ]", getValStr(std::to_string(Config::LANE_WIDTH), 6));
    items.emplace_back("[ SCRATCH WIDTH ]", getValStr(std::to_string(Config::SCRATCH_WIDTH), 7));
    // Index 8: GAUGE DISPLAY
    items.emplace_back("[ GAUGE DISPLAY ]", getValStr((Config::GAUGE_DISPLAY_TYPE == 0 ? "1% STEP" : "2% STEP (IIDX)"), 8));
    // Index 9: START UP SCREEN
    items.emplace_back("[ START UP SCREEN ]", getValStr((Config::START_UP_OPTION == 0 ? "TITLE" : "SELECT"), 9));
    // Index 10: JUDGE OFFSET
    items.emplace_back("[ JUDGE OFFSET (ms) ]", getValStr(std::to_string(Config::JUDGE_OFFSET), 10));
    // Index 11: VISUAL OFFSET
    items.emplace_back("[ VISUAL OFFSET (px) ]", getValStr(std::to_string(Config::VISUAL_OFFSET), 11));

    // Index 12: Section
    items.emplace_back("--- [ EFFECT SETTINGS ] ---", "");
    // Index 13: BOMB DURATION
    items.emplace_back("[ BOMB DURATION (ms) ]", getValStr(std::to_string(Config::BOMB_DURATION_MS), 13));
    // Index 14: BOMB SIZE
    items.emplace_back("[ BOMB SIZE ]", getValStr(std::to_string(Config::BOMB_SIZE_FACTOR), 14));

    // Index 15: Section
    items.emplace_back("--- [ FOLDER SETTINGS ] ---", "");
    // Index 16-21: 仮想フォルダ設定項目
    items.emplace_back("[ FOLDER: LEVEL ]", getValStr((Config::SHOW_LEVEL_FOLDER ? "ON" : "OFF"), 16));
    items.emplace_back("[ FOLDER: LAMP ]", getValStr((Config::SHOW_LAMP_FOLDER ? "ON" : "OFF"), 17));
    items.emplace_back("[ FOLDER: RANK ]", getValStr((Config::SHOW_RANK_FOLDER ? "ON" : "OFF"), 18)); 
    items.emplace_back("[ FOLDER: TYPE ]", getValStr((Config::SHOW_CHART_TYPE_FOLDER ? "ON" : "OFF"), 19));
    items.emplace_back("[ FOLDER: NOTES ]", getValStr((Config::SHOW_NOTES_RANGE_FOLDER ? "ON" : "OFF"), 20));
    items.emplace_back("[ FOLDER: ALPHA ]", getValStr((Config::SHOW_ALPHA_FOLDER ? "ON" : "OFF"), 21));
    
    // Index 22: BACK
    items.emplace_back("[ BACK TO MENU ]", "");
}

OptionState SceneOption::update(SDL_Renderer* ren, NoteRenderer& renderer) {
    uint32_t currentTime = SDL_GetTicks();
    SDL_Event e;

    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT) return OptionState::FINISHED;

        if (e.type == SDL_JOYBUTTONDOWN) {
            int btn = e.jbutton.button;

            if (state == OptionState::SELECTING_ITEM) {
                if (btn == Config::SYS_BTN_UP) {
                    cursor = (cursor - 1 + items.size()) % items.size();
                    if (items[cursor].label.find("---") != std::string::npos) cursor = (cursor - 1 + items.size()) % items.size();
                }
                if (btn == Config::SYS_BTN_DOWN) {
                    cursor = (cursor + 1) % items.size();
                    if (items[cursor].label.find("---") != std::string::npos) cursor = (cursor + 1) % items.size();
                }

                if (btn == Config::SYS_BTN_DECIDE) {
                    if (cursor == 1) { state = OptionState::WAITING_KEY; configStep = 0; lastConfigTime = currentTime; }
                    else if (cursor == 2) { state = OptionState::WAITING_KEY; configStep = 11; lastConfigTime = currentTime; }
                    else if ((cursor >= 4 && cursor <= 14) || (cursor >= 16 && cursor <= 21)) { state = OptionState::ADJUSTING_VALUE; updateItemList(); }
                    else if (cursor == (int)items.size() - 1) { Config::save(); return OptionState::FINISHED; }
                }
                if (btn == Config::SYS_BTN_BACK) { Config::save(); return OptionState::FINISHED; }
            }
            else if (state == OptionState::ADJUSTING_VALUE) {
                bool changed = true;
                repeatTimer = currentTime + 400;

                if (cursor == 4) { // GREEN NUMBER
                    int& targetVar = Config::GREEN_NUMBER;
                    if (btn == Config::SYS_BTN_UP) targetVar -= 10;
                    else if (btn == Config::SYS_BTN_DOWN) targetVar += 10;
                    else if (btn == Config::SYS_BTN_LEFT) targetVar -= 1;
                    else if (btn == Config::SYS_BTN_RIGHT) targetVar += 1;
                    else if (btn == Config::SYS_BTN_DECIDE || btn == Config::SYS_BTN_BACK) state = OptionState::SELECTING_ITEM;
                    else changed = false;
                }
                else if (cursor == 5) { // LIFT
                    int& targetVar = Config::LIFT;
                    if (btn == Config::SYS_BTN_UP) targetVar += 10;
                    else if (btn == Config::SYS_BTN_DOWN) targetVar -= 10;
                    else if (btn == Config::SYS_BTN_LEFT) targetVar -= 1;
                    else if (btn == Config::SYS_BTN_RIGHT) targetVar += 1;
                    else if (btn == Config::SYS_BTN_DECIDE || btn == Config::SYS_BTN_BACK) state = OptionState::SELECTING_ITEM;
                    else changed = false;
                    if (targetVar < 0) targetVar = 0;
                }
                else if (cursor == 6) { // LANE WIDTH
                    int& targetVar = Config::LANE_WIDTH;
                    if (btn == Config::SYS_BTN_UP) targetVar += 5;
                    else if (btn == Config::SYS_BTN_DOWN) targetVar -= 5;
                    else if (btn == Config::SYS_BTN_LEFT) targetVar -= 1;
                    else if (btn == Config::SYS_BTN_RIGHT) targetVar += 1;
                    else if (btn == Config::SYS_BTN_DECIDE || btn == Config::SYS_BTN_BACK) state = OptionState::SELECTING_ITEM;
                    else changed = false;
                    if (targetVar < 10) targetVar = 10;
                }
                else if (cursor == 7) { // SCRATCH WIDTH
                    int& targetVar = Config::SCRATCH_WIDTH;
                    if (btn == Config::SYS_BTN_UP) targetVar += 5;
                    else if (btn == Config::SYS_BTN_DOWN) targetVar -= 5;
                    else if (btn == Config::SYS_BTN_LEFT) targetVar -= 1;
                    else if (btn == Config::SYS_BTN_RIGHT) targetVar += 1;
                    else if (btn == Config::SYS_BTN_DECIDE || btn == Config::SYS_BTN_BACK) state = OptionState::SELECTING_ITEM;
                    else changed = false;
                    if (targetVar < 10) targetVar = 10;
                }
                else if (cursor == 8) { // GAUGE DISPLAY
                    if (btn == Config::SYS_BTN_LEFT || btn == Config::SYS_BTN_RIGHT || btn == Config::SYS_BTN_UP || btn == Config::SYS_BTN_DOWN) Config::GAUGE_DISPLAY_TYPE = 1 - Config::GAUGE_DISPLAY_TYPE;
                    else if (btn == Config::SYS_BTN_DECIDE || btn == Config::SYS_BTN_BACK) state = OptionState::SELECTING_ITEM;
                    else changed = false;
                }
                else if (cursor == 9) { // START UP SCREEN
                    if (btn == Config::SYS_BTN_LEFT || btn == Config::SYS_BTN_RIGHT || btn == Config::SYS_BTN_UP || btn == Config::SYS_BTN_DOWN) Config::START_UP_OPTION = 1 - Config::START_UP_OPTION;
                    else if (btn == Config::SYS_BTN_DECIDE || btn == Config::SYS_BTN_BACK) state = OptionState::SELECTING_ITEM;
                    else changed = false;
                }
                else if (cursor == 10) { // JUDGE OFFSET
                    int& targetVar = Config::JUDGE_OFFSET;
                    if (btn == Config::SYS_BTN_UP) targetVar += 5;
                    else if (btn == Config::SYS_BTN_DOWN) targetVar -= 5;
                    else if (btn == Config::SYS_BTN_LEFT) targetVar -= 1;
                    else if (btn == Config::SYS_BTN_RIGHT) targetVar += 1;
                    else if (btn == Config::SYS_BTN_DECIDE || btn == Config::SYS_BTN_BACK) state = OptionState::SELECTING_ITEM;
                    else changed = false;
                    targetVar = std::clamp(targetVar, -200, 200);
                }
                else if (cursor == 11) { // VISUAL OFFSET
                    int& targetVar = Config::VISUAL_OFFSET;
                    if (btn == Config::SYS_BTN_UP) targetVar += 5;
                    else if (btn == Config::SYS_BTN_DOWN) targetVar -= 5;
                    else if (btn == Config::SYS_BTN_LEFT) targetVar -= 1;
                    else if (btn == Config::SYS_BTN_RIGHT) targetVar += 1;
                    else if (btn == Config::SYS_BTN_DECIDE || btn == Config::SYS_BTN_BACK) state = OptionState::SELECTING_ITEM;
                    else changed = false;
                    targetVar = std::clamp(targetVar, -200, 200);
                }
                else if (cursor == 13) { // BOMB DURATION
                    int& targetVar = Config::BOMB_DURATION_MS;
                    if (btn == Config::SYS_BTN_UP) targetVar += 50;
                    else if (btn == Config::SYS_BTN_DOWN) targetVar -= 50;
                    else if (btn == Config::SYS_BTN_LEFT) targetVar -= 10;
                    else if (btn == Config::SYS_BTN_RIGHT) targetVar += 10;
                    else if (btn == Config::SYS_BTN_DECIDE || btn == Config::SYS_BTN_BACK) state = OptionState::SELECTING_ITEM;
                    else changed = false;
                    if (targetVar < 50) targetVar = 50;
                    if (targetVar > 1000) targetVar = 1000;
                }
                else if (cursor == 14) { // BOMB SIZE
                    int& targetVar = Config::BOMB_SIZE_FACTOR;
                    if (btn == Config::SYS_BTN_UP) targetVar += 50;
                    else if (btn == Config::SYS_BTN_DOWN) targetVar -= 50;
                    else if (btn == Config::SYS_BTN_LEFT) targetVar -= 10;
                    else if (btn == Config::SYS_BTN_RIGHT) targetVar += 10;
                    else if (btn == Config::SYS_BTN_DECIDE || btn == Config::SYS_BTN_BACK) state = OptionState::SELECTING_ITEM;
                    else changed = false;
                    if (targetVar < 100) targetVar = 100;
                    if (targetVar > 2000) targetVar = 2000;
                }
                else if (cursor >= 16 && cursor <= 21) { // FOLDERS
                    if (btn == Config::SYS_BTN_LEFT || btn == Config::SYS_BTN_RIGHT || btn == Config::SYS_BTN_UP || btn == Config::SYS_BTN_DOWN) {
                        if (cursor == 16) Config::SHOW_LEVEL_FOLDER = !Config::SHOW_LEVEL_FOLDER;
                        else if (cursor == 17) Config::SHOW_LAMP_FOLDER = !Config::SHOW_LAMP_FOLDER;
                        else if (cursor == 18) Config::SHOW_RANK_FOLDER = !Config::SHOW_RANK_FOLDER;
                        else if (cursor == 19) Config::SHOW_CHART_TYPE_FOLDER = !Config::SHOW_CHART_TYPE_FOLDER;
                        else if (cursor == 20) Config::SHOW_NOTES_RANGE_FOLDER = !Config::SHOW_NOTES_RANGE_FOLDER;
                        else if (cursor == 21) Config::SHOW_ALPHA_FOLDER = !Config::SHOW_ALPHA_FOLDER;
                    }
                    else if (btn == Config::SYS_BTN_DECIDE || btn == Config::SYS_BTN_BACK) state = OptionState::SELECTING_ITEM;
                    else changed = false;
                }
                else changed = false;
                if (changed) updateItemList();
            }
            else if (state == OptionState::WAITING_KEY) {
                if (currentTime - lastConfigTime > 1000) { handleKeyConfig(btn); lastConfigTime = currentTime; }
            }
        }
    }

    // --- 描画処理 ---
    SDL_SetRenderDrawColor(ren, 20, 20, 30, 255);
    SDL_RenderClear(ren);

    if (state == OptionState::SELECTING_ITEM || (state == OptionState::ADJUSTING_VALUE && cursor != 6 && cursor != 7)) {
        renderer.drawText(ren, "SETTINGS", 640, 80, { 255, 255, 255, 255 }, true, true);
        int maxVisible = 10;
        int scrollOffset = (cursor >= maxVisible) ? cursor - (maxVisible - 1) : 0;
        for (int i = 0; i < maxVisible && (i + scrollOffset) < (int)items.size(); ++i) {
            int idx = i + scrollOffset;
            SDL_Color color = (idx == (int)cursor) ? SDL_Color{ 0, 255, 255, 255 } : SDL_Color{ 150, 150, 150, 255 };
            if (items[idx].label.find("---") != std::string::npos) color = { 80, 80, 120, 255 };
            else if (state == OptionState::ADJUSTING_VALUE && idx == cursor) color = { 255, 255, 0, 255 };
            renderer.drawText(ren, items[idx].label + "  " + items[idx].current_value, 640, 160 + i * 45, color, true, true);
        }
        if (state == OptionState::ADJUSTING_VALUE) {
            renderer.drawText(ren, "ADJUSTING: PRESS DECIDE TO CONFIRM", 640, 620, {255, 255, 0, 255}, true, true);
            renderer.drawText(ren, (cursor == 4 || cursor == 5 || cursor == 10 || cursor == 11 || cursor == 13 || cursor == 14 ? "SYS-UP/DOWN: +-5  SYS-L/R: +-1" : "SYS-UDLR TO SWITCH TYPE"), 640, 670, {120, 120, 120, 255}, true, true);
        } else if ((cursor >= 4 && cursor <= 14) || (cursor >= 16 && cursor <= 21)) {
            renderer.drawText(ren, "PRESS DECIDE TO ADJUST", 640, 670, {150, 150, 150, 255}, true, true);
        }
    } 
    else if (state == OptionState::ADJUSTING_VALUE && (cursor == 6 || cursor == 7)) {
        renderer.drawText(ren, items[cursor].label, 640, 60, { 255, 255, 0, 255 }, true, true);
        renderer.drawText(ren, items[cursor].current_value, 640, 110, { 255, 255, 255, 255 }, true, true);

        SDL_Rect bgaRect = { 320, 180, 640, 480 };
        SDL_SetRenderDrawColor(ren, 40, 40, 40, 255);
        SDL_RenderFillRect(ren, &bgaRect);
        renderer.drawText(ren, "BGA AREA", 640, 420, {60, 60, 60, 255}, true, true);

        int previewCenterY = 400;
        int previewWidth = (Config::LANE_WIDTH * 7) + Config::SCRATCH_WIDTH;
        int previewH = 400;

        for (int side = 0; side < 2; ++side) {
            int startX = (side == 0) ? (bgaRect.x - previewWidth - 20) : (bgaRect.x + bgaRect.w + 20);
            SDL_Rect laneBg = { startX, previewCenterY - 200, previewWidth, previewH };
            SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
            SDL_RenderFillRect(ren, &laneBg);

            SDL_SetRenderDrawColor(ren, 80, 80, 80, 255);
            for (int l = 0; l <= 8; ++l) {
                int x = startX;
                int w = (side == 0) ? (l == 0 ? 0 : (l == 1 ? Config::SCRATCH_WIDTH : Config::SCRATCH_WIDTH + (l-1)*Config::LANE_WIDTH)) 
                                    : (l < 8 ? l*Config::LANE_WIDTH : 7*Config::LANE_WIDTH + Config::SCRATCH_WIDTH);
                x += w;
                SDL_RenderDrawLine(ren, x, laneBg.y, x, laneBg.y + laneBg.h);

                if (l < 8) {
                    int noteX = x + 2;
                    int noteW = (side == 0) ? (l == 0 ? Config::SCRATCH_WIDTH : Config::LANE_WIDTH) 
                                            : (l == 7 ? Config::SCRATCH_WIDTH : Config::LANE_WIDTH);
                    noteW -= 4;
                    SDL_Rect noteRect = { noteX, laneBg.y + 50 + (l*30), noteW, 8 };
                    SDL_SetRenderDrawColor(ren, (l==0||l==7 ? 255 : (l%2==0 ? 255 : 100)), (l%2==0 ? 255 : 100), 255, 255);
                    SDL_RenderFillRect(ren, &noteRect);
                    SDL_SetRenderDrawColor(ren, 80, 80, 80, 255);
                }
            }
            SDL_SetRenderDrawColor(ren, 255, 0, 0, 255);
            SDL_RenderDrawLine(ren, startX, laneBg.y + 360, startX + laneBg.w, laneBg.y + 360);
            renderer.drawText(ren, (side == 0 ? "1P (LEFT-S)" : "2P (RIGHT-S)"), startX + laneBg.w / 2, laneBg.y + laneBg.h + 25, {150, 150, 150, 255}, true, true);
        }
        renderer.drawText(ren, "CHECK LANE COVERAGE OVER BGA", 640, 680, {100, 100, 100, 255}, true, true);
    }
    else {
        renderer.drawText(ren, "PRESS BUTTON FOR:", 640, 300, { 255, 255, 0, 255 }, true, true);
        if (currentTime - lastConfigTime < 1000) {
            renderer.drawText(ren, "- PLEASE WAIT -", 640, 400, { 100, 100, 100, 255 }, true, true);
        } else if (configStep < (int)keyLabels.size()) {
            renderer.drawText(ren, keyLabels[configStep], 640, 400, { 255, 255, 255, 255 }, true, true);
            int currentPos = (cursor == 1) ? (configStep + 1) : (configStep - 11 + 1);
            int totalPos = (cursor == 1) ? 11 : 10; 
            renderer.drawText(ren, std::to_string(currentPos) + " / " + std::to_string(totalPos), 640, 460, { 150, 150, 150, 255 }, true, true);
        }
    }

    SDL_RenderPresent(ren);
    return state;
}

void SceneOption::handleKeyConfig(int btn) {
    switch (configStep) {
        case 0: Config::BTN_EXIT = btn;    break;
        case 1: Config::BTN_EFFECT = btn;  break;
        case 2: Config::BTN_LANE1 = btn;   break;
        case 3: Config::BTN_LANE2 = btn;   break;
        case 4: Config::BTN_LANE3 = btn;   break;
        case 5: Config::BTN_LANE4 = btn;   break;
        case 6: Config::BTN_LANE5 = btn;   break;
        case 7: Config::BTN_LANE6 = btn;   break;
        case 8: Config::BTN_LANE7 = btn;   break;
        case 9: Config::BTN_LANE8_A = btn; break;
        case 10: Config::BTN_LANE8_B = btn; break;
        case 11: Config::SYS_BTN_DECIDE = btn; break;
        case 12: Config::SYS_BTN_BACK   = btn; break;
        case 13: Config::SYS_BTN_UP      = btn; break;
        case 14: Config::SYS_BTN_DOWN    = btn; break;
        case 15: Config::SYS_BTN_LEFT    = btn; break;
        case 16: Config::SYS_BTN_RIGHT  = btn; break;
        case 17: Config::SYS_BTN_OPTION = btn; break;
        case 18: Config::SYS_BTN_DIFF   = btn; break;
        case 19: Config::SYS_BTN_SORT   = btn; break; 
        case 20: Config::SYS_BTN_RANDOM = btn; break; 
    }
    configStep++;
    bool finished = (cursor == 1 && configStep > 10) || (cursor == 2 && configStep > 20); 
    if (finished) {
        Config::save();
        state = OptionState::SELECTING_ITEM;
        configStep = 0;
        updateItemList();
    }
}




