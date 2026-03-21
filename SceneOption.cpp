#include "SceneOption.hpp"

// 表示用ラベルの定義（21項目：プレイキー11種 + システムキー10種）
static const std::vector<std::string> keyLabels = {
    // 0-10: 1P KEY CONFIG
    "1P: START / EXIT",
    "1P: EFFECT",
    "1P: LANE 1",
    "1P: LANE 2",
    "1P: LANE 3",
    "1P: LANE 4",
    "1P: LANE 5",
    "1P: LANE 6",
    "1P: LANE 7",
    "1P: SCRATCH A",
    "1P: SCRATCH B",
    // 11-20: SYSTEM KEY CONFIG
    "SYS: DECIDE",
    "SYS: BACK",
    "SYS: UP",
    "SYS: DOWN",
    "SYS: LEFT",
    "SYS: RIGHT",
    "SYS: OPTION",
    "SYS: DIFF",
    "SYS: SORT",
    "SYS: RANDOM",
    // 21: padding (unused)
    "",
    // 22-31: 2P KEY CONFIG
    "2P: START / EXIT",
    "2P: LANE 1",
    "2P: LANE 2",
    "2P: LANE 3",
    "2P: LANE 4",
    "2P: LANE 5",
    "2P: LANE 6",
    "2P: LANE 7",
    "2P: SCRATCH A",
    "2P: SCRATCH B",
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
    // Index 1-3
    items.emplace_back("[ 1P KEY CONFIG ]", "11 Keys");
    items.emplace_back("[ 2P KEY CONFIG ]", "11 Keys");
    items.emplace_back("[ SYSTEM KEY CONFIG ]", "10 Keys"); 
    
    auto getValStr = [&](std::string val, int idx) {
        return (state == OptionState::ADJUSTING_VALUE && cursor == idx) ? "< " + val + " >" : val;
    };

    // Index 4: Section
    items.emplace_back("--- [ PLAY SETTINGS ] ---", "");
    // Index 5: GREEN NUMBER
    items.emplace_back("[ GREEN NUMBER ]", getValStr(std::to_string(Config::GREEN_NUMBER), 5));
    // Index 6: LIFT
    items.emplace_back("[ LIFT ]", getValStr(std::to_string(Config::LIFT), 6));
    // Index 7-8: LANE / SCRATCH WIDTH (AC固定 - 表示のみ)
    items.emplace_back("[ KEY WHITE WIDTH ]", std::to_string(Config::AC_KEY_WHITE));
    items.emplace_back("[ KEY BLACK WIDTH ]", std::to_string(Config::AC_KEY_BLACK));
    // Index 9: GAUGE DISPLAY
    items.emplace_back("[ GAUGE DISPLAY ]", getValStr((Config::GAUGE_DISPLAY_TYPE == 0 ? "1% STEP" : "2% STEP (IIDX)"), 9));
    // Index 10: START UP SCREEN
    items.emplace_back("[ START UP SCREEN ]", getValStr((Config::START_UP_OPTION == 0 ? "TITLE" : "SELECT"), 10));
    // Index 11: JUDGE OFFSET
    items.emplace_back("[ JUDGE OFFSET (ms) ]", getValStr(std::to_string(Config::JUDGE_OFFSET), 11));
    // Index 12: VISUAL OFFSET
    items.emplace_back("[ VISUAL OFFSET (px) ]", getValStr(std::to_string(Config::VISUAL_OFFSET), 12));
    // Index 13: MORE NOTES COUNT
    items.emplace_back("[ MORE NOTES COUNT ]", getValStr(std::to_string(Config::MORE_NOTES_COUNT), 13));

    // ★追加 Index 14: LN TYPE
    static const char* lnTypeNames[] = {"CN", "HCN", "LN"};
    items.emplace_back("[ LN TYPE ]", getValStr(lnTypeNames[Config::LN_OPTION], 14));

    // Index 15: Section  (以前の14から+1)
    items.emplace_back("--- [ EFFECT SETTINGS ] ---", "");
    // Index 16: BOMB DURATION  (以前の15から+1)
    items.emplace_back("[ BOMB DURATION (ms) ]", getValStr(std::to_string(Config::BOMB_DURATION_MS), 16));
    // Index 17: BOMB SIZE  (以前の16から+1)
    items.emplace_back("[ BOMB SIZE (px) ]", getValStr(std::to_string(Config::BOMB_SIZE), 17));

    // Index 18: Section  (以前の17から+1)
    items.emplace_back("--- [ FOLDER SETTINGS ] ---", "");
    // Index 19-24: 仮想フォルダ設定項目  (以前の18-23から+1)
    items.emplace_back("[ FOLDER: LEVEL ]", getValStr((Config::SHOW_LEVEL_FOLDER ? "ON" : "OFF"), 19));
    items.emplace_back("[ FOLDER: LAMP ]", getValStr((Config::SHOW_LAMP_FOLDER ? "ON" : "OFF"), 20));
    items.emplace_back("[ FOLDER: RANK ]", getValStr((Config::SHOW_RANK_FOLDER ? "ON" : "OFF"), 21));
    items.emplace_back("[ FOLDER: TYPE ]", getValStr((Config::SHOW_CHART_TYPE_FOLDER ? "ON" : "OFF"), 22));
    items.emplace_back("[ FOLDER: NOTES ]", getValStr((Config::SHOW_NOTES_RANGE_FOLDER ? "ON" : "OFF"), 23));
    items.emplace_back("[ FOLDER: ALPHA ]", getValStr((Config::SHOW_ALPHA_FOLDER ? "ON" : "OFF"), 24));
    
    // Index 25: BACK  (以前の24から+1)
    items.emplace_back("[ BACK TO MENU ]", "");
}

OptionState SceneOption::update(SDL_Renderer* ren, NoteRenderer& renderer) {
    uint32_t currentTime = SDL_GetTicks();
    SDL_Event e;

    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT) {
            Config::save();
            renderer.notifyLayoutChanged();
            return OptionState::FINISHED;
        }

        // ★Mac/PC: キーボード → SYS_BTNに変換
        if (e.type == SDL_KEYDOWN && !e.key.repeat) {
            int btn = -1;
            switch (e.key.keysym.sym) {
                case SDLK_UP:     btn = Config::SYS_BTN_UP;     break;
                case SDLK_DOWN:   btn = Config::SYS_BTN_DOWN;   break;
                case SDLK_LEFT:   btn = Config::SYS_BTN_LEFT;   break;
                case SDLK_RIGHT:  btn = Config::SYS_BTN_RIGHT;  break;
                case SDLK_RETURN: btn = Config::SYS_BTN_DECIDE; break;
                case SDLK_ESCAPE: btn = Config::SYS_BTN_BACK;   break;
                default: break;
            }
            if (btn >= 0) {
                e.type = SDL_JOYBUTTONDOWN;
                e.jbutton.button = (Uint8)btn;
            }
        }

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
                    if (cursor == 1) { state = OptionState::WAITING_KEY; configStep = 0;  lastConfigTime = currentTime; }  // 1P KEY
                    else if (cursor == 2) { state = OptionState::WAITING_KEY; configStep = 22; lastConfigTime = currentTime; } // 2P KEY
                    else if (cursor == 3) { state = OptionState::WAITING_KEY; configStep = 11; lastConfigTime = currentTime; } // SYS KEY
                    // ★変更: 調整可能範囲に14(LN TYPE)を追加、以降+1
                    else if ((cursor >= 5 && cursor <= 17) || (cursor >= 19 && cursor <= 24)) { state = OptionState::ADJUSTING_VALUE; updateItemList(); }
                    else if (cursor == (int)items.size() - 1) { Config::save(); renderer.notifyLayoutChanged(); return OptionState::FINISHED; }
                }
                if (btn == Config::SYS_BTN_BACK) { Config::save(); renderer.notifyLayoutChanged(); return OptionState::FINISHED; }
            }
            else if (state == OptionState::ADJUSTING_VALUE) {
                bool changed = true;
                repeatTimer = currentTime + 400;

                if (cursor == 5) { // GREEN NUMBER
                    int& targetVar = Config::GREEN_NUMBER;
                    if (btn == Config::SYS_BTN_UP) targetVar -= 10;
                    else if (btn == Config::SYS_BTN_DOWN) targetVar += 10;
                    else if (btn == Config::SYS_BTN_LEFT) targetVar -= 1;
                    else if (btn == Config::SYS_BTN_RIGHT) targetVar += 1;
                    else if (btn == Config::SYS_BTN_DECIDE || btn == Config::SYS_BTN_BACK) state = OptionState::SELECTING_ITEM;
                    else changed = false;
                }
                else if (cursor == 6) { // LIFT
                    int& targetVar = Config::LIFT;
                    if (btn == Config::SYS_BTN_UP) targetVar += 10;
                    else if (btn == Config::SYS_BTN_DOWN) targetVar -= 10;
                    else if (btn == Config::SYS_BTN_LEFT) targetVar -= 1;
                    else if (btn == Config::SYS_BTN_RIGHT) targetVar += 1;
                    else if (btn == Config::SYS_BTN_DECIDE || btn == Config::SYS_BTN_BACK) state = OptionState::SELECTING_ITEM;
                    else changed = false;
                    if (targetVar < 0) targetVar = 0;
                }
                else if (cursor == 9) { // GAUGE DISPLAY
                    if (btn == Config::SYS_BTN_LEFT || btn == Config::SYS_BTN_RIGHT || btn == Config::SYS_BTN_UP || btn == Config::SYS_BTN_DOWN) Config::GAUGE_DISPLAY_TYPE = 1 - Config::GAUGE_DISPLAY_TYPE;
                    else if (btn == Config::SYS_BTN_DECIDE || btn == Config::SYS_BTN_BACK) state = OptionState::SELECTING_ITEM;
                    else changed = false;
                }
                else if (cursor == 10) { // START UP SCREEN
                    if (btn == Config::SYS_BTN_LEFT || btn == Config::SYS_BTN_RIGHT || btn == Config::SYS_BTN_UP || btn == Config::SYS_BTN_DOWN) Config::START_UP_OPTION = 1 - Config::START_UP_OPTION;
                    else if (btn == Config::SYS_BTN_DECIDE || btn == Config::SYS_BTN_BACK) state = OptionState::SELECTING_ITEM;
                    else changed = false;
                }
                else if (cursor == 11) { // JUDGE OFFSET
                    int& targetVar = Config::JUDGE_OFFSET;
                    if (btn == Config::SYS_BTN_UP) targetVar += 5;
                    else if (btn == Config::SYS_BTN_DOWN) targetVar -= 5;
                    else if (btn == Config::SYS_BTN_LEFT) targetVar -= 1;
                    else if (btn == Config::SYS_BTN_RIGHT) targetVar += 1;
                    else if (btn == Config::SYS_BTN_DECIDE || btn == Config::SYS_BTN_BACK) state = OptionState::SELECTING_ITEM;
                    else changed = false;
                    targetVar = std::clamp(targetVar, -200, 200);
                }
                else if (cursor == 12) { // VISUAL OFFSET
                    int& targetVar = Config::VISUAL_OFFSET;
                    if (btn == Config::SYS_BTN_UP) targetVar += 5;
                    else if (btn == Config::SYS_BTN_DOWN) targetVar -= 5;
                    else if (btn == Config::SYS_BTN_LEFT) targetVar -= 1;
                    else if (btn == Config::SYS_BTN_RIGHT) targetVar += 1;
                    else if (btn == Config::SYS_BTN_DECIDE || btn == Config::SYS_BTN_BACK) state = OptionState::SELECTING_ITEM;
                    else changed = false;
                    targetVar = std::clamp(targetVar, -200, 200);
                }
                else if (cursor == 13) { // MORE NOTES COUNT
                    int& targetVar = Config::MORE_NOTES_COUNT;
                    if (btn == Config::SYS_BTN_UP) targetVar += 10;
                    else if (btn == Config::SYS_BTN_DOWN) targetVar -= 10;
                    else if (btn == Config::SYS_BTN_DECIDE || btn == Config::SYS_BTN_BACK) state = OptionState::SELECTING_ITEM;
                    else changed = false;
                    if (targetVar < 0) targetVar = 0;
                }
                // ★追加: cursor == 14: LN TYPE
                else if (cursor == 14) { // LN TYPE: CN=0 / HCN=1 / LN=2
                    if (btn == Config::SYS_BTN_LEFT || btn == Config::SYS_BTN_UP)
                        Config::LN_OPTION = (Config::LN_OPTION + 2) % 3;
                    else if (btn == Config::SYS_BTN_RIGHT || btn == Config::SYS_BTN_DOWN)
                        Config::LN_OPTION = (Config::LN_OPTION + 1) % 3;
                    else if (btn == Config::SYS_BTN_DECIDE || btn == Config::SYS_BTN_BACK)
                        state = OptionState::SELECTING_ITEM;
                    else changed = false;
                }
                else if (cursor == 16) { // BOMB DURATION  (以前の15から+1)
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
                else if (cursor == 17) { // BOMB SIZE (px)
                    int& targetVar = Config::BOMB_SIZE;
                    if (btn == Config::SYS_BTN_UP) targetVar += 10;
                    else if (btn == Config::SYS_BTN_DOWN) targetVar -= 10;
                    else if (btn == Config::SYS_BTN_LEFT) targetVar -= 1;
                    else if (btn == Config::SYS_BTN_RIGHT) targetVar += 1;
                    else if (btn == Config::SYS_BTN_DECIDE || btn == Config::SYS_BTN_BACK) state = OptionState::SELECTING_ITEM;
                    else changed = false;
                    if (targetVar < 10) targetVar = 10;
                    if (targetVar > 500) targetVar = 500;
                }
                else if (cursor >= 19 && cursor <= 24) { // FOLDERS  (以前の18-23から+1)
                    if (btn == Config::SYS_BTN_LEFT || btn == Config::SYS_BTN_RIGHT || btn == Config::SYS_BTN_UP || btn == Config::SYS_BTN_DOWN) {
                        if (cursor == 19) Config::SHOW_LEVEL_FOLDER = !Config::SHOW_LEVEL_FOLDER;
                        else if (cursor == 20) Config::SHOW_LAMP_FOLDER = !Config::SHOW_LAMP_FOLDER;
                        else if (cursor == 21) Config::SHOW_RANK_FOLDER = !Config::SHOW_RANK_FOLDER;
                        else if (cursor == 22) Config::SHOW_CHART_TYPE_FOLDER = !Config::SHOW_CHART_TYPE_FOLDER;
                        else if (cursor == 23) Config::SHOW_NOTES_RANGE_FOLDER = !Config::SHOW_NOTES_RANGE_FOLDER;
                        else if (cursor == 24) Config::SHOW_ALPHA_FOLDER = !Config::SHOW_ALPHA_FOLDER;
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

    if (state == OptionState::SELECTING_ITEM || (state == OptionState::ADJUSTING_VALUE && cursor != 7 && cursor != 8)) {
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
            // ★変更: ヘルプテキストにcursor==14(LN TYPE)の説明を追加、cursor番号を+1修正
            std::string helpText;
            if (cursor == 5 || cursor == 6 || cursor == 11 || cursor == 12 || cursor == 16 || cursor == 17)
                helpText = "SYS-UP/DOWN: +-5  SYS-L/R: +-1";
            else if (cursor == 13)
                helpText = "SYS-UP/DOWN: +-10 (0+)";
            else if (cursor == 14)
                helpText = "SYS-L/R or UP/DOWN: CN / HCN / LN";
            else
                helpText = "SYS-UDLR TO SWITCH TYPE";
            renderer.drawText(ren, helpText, 640, 670, {120, 120, 120, 255}, true, true);
        } else if ((cursor >= 5 && cursor <= 17) || (cursor >= 19 && cursor <= 24)) {
            renderer.drawText(ren, "PRESS DECIDE TO ADJUST", 640, 670, {150, 150, 150, 255}, true, true);
        }
    } 
    else if (state == OptionState::ADJUSTING_VALUE && (cursor == 7 || cursor == 8)) {
        renderer.drawText(ren, items[cursor].label, 640, 60, { 255, 255, 0, 255 }, true, true);
        renderer.drawText(ren, items[cursor].current_value, 640, 110, { 255, 255, 255, 255 }, true, true);

        SDL_Rect bgaRect = { 320, 180, 640, 480 };
        SDL_SetRenderDrawColor(ren, 40, 40, 40, 255);
        SDL_RenderFillRect(ren, &bgaRect);
        renderer.drawText(ren, "BGA AREA", 640, 420, {60, 60, 60, 255}, true, true);

        int previewCenterY = 400;
        int previewWidth = (Config::AC_KEY_WHITE * 4 + Config::AC_KEY_BLACK * 3) + Config::AC_SCRATCH;
        int previewH = 400;

        for (int side = 0; side < 2; ++side) {
            int startX = (side == 0) ? (bgaRect.x - previewWidth - 20) : (bgaRect.x + bgaRect.w + 20);
            SDL_Rect laneBg = { startX, previewCenterY - 200, previewWidth, previewH };
            SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
            SDL_RenderFillRect(ren, &laneBg);

            SDL_SetRenderDrawColor(ren, 80, 80, 80, 255);
            for (int l = 0; l <= 8; ++l) {
                int x = startX;
                // ACレイアウト: 1P=scratch左端, 2P=scratch右端
                auto acLaneW = [](int lane) { return (lane == 8) ? Config::AC_SCRATCH : (lane % 2 == 0 ? Config::AC_KEY_BLACK : Config::AC_KEY_WHITE); };
                int w = 0;
                if (side == 0) { // 1P: scratch(8)→鍵1-7
                    if (l == 0) w = 0;
                    else if (l == 1) w = Config::AC_SCRATCH;
                    else { w = Config::AC_SCRATCH; for (int k=1; k<l; k++) w += acLaneW(k); }
                } else { // 2P: 鍵1-7→scratch(8)
                    for (int k=1; k<=std::min(l,7); k++) w += acLaneW(k);
                    if (l == 8) w += Config::AC_SCRATCH;
                }
                x += w;
                SDL_RenderDrawLine(ren, x, laneBg.y, x, laneBg.y + laneBg.h);

                if (l < 8) {
                    int noteX = x + 2;
                    int noteW = acLaneW(l == 0 ? 8 : l);
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
            int currentPos, totalPos;
            if (cursor == 1) { currentPos = configStep + 1;        totalPos = 11; } // 1P
            else if (cursor == 2) { currentPos = configStep - 22 + 1; totalPos = 10; } // 2P
            else { currentPos = configStep - 11 + 1;               totalPos = 10; } // SYS
            renderer.drawText(ren, std::to_string(currentPos) + " / " + std::to_string(totalPos), 640, 460, { 150, 150, 150, 255 }, true, true);
        }
    }

    SDL_RenderPresent(ren);
    return state;
}

void SceneOption::handleKeyConfig(int btn) {
    switch (configStep) {
        // 1P KEY CONFIG (cursor==1, step 0-10)
        case 0:  Config::BTN_EXIT    = btn; break;
        case 1:  Config::BTN_EFFECT  = btn; break;
        case 2:  Config::BTN_LANE1   = btn; break;
        case 3:  Config::BTN_LANE2   = btn; break;
        case 4:  Config::BTN_LANE3   = btn; break;
        case 5:  Config::BTN_LANE4   = btn; break;
        case 6:  Config::BTN_LANE5   = btn; break;
        case 7:  Config::BTN_LANE6   = btn; break;
        case 8:  Config::BTN_LANE7   = btn; break;
        case 9:  Config::BTN_LANE8_A = btn; break;
        case 10: Config::BTN_LANE8_B = btn; break;
        // SYSTEM KEY CONFIG (cursor==3, step 11-20)
        case 11: Config::SYS_BTN_DECIDE = btn; break;
        case 12: Config::SYS_BTN_BACK   = btn; break;
        case 13: Config::SYS_BTN_UP     = btn; break;
        case 14: Config::SYS_BTN_DOWN   = btn; break;
        case 15: Config::SYS_BTN_LEFT   = btn; break;
        case 16: Config::SYS_BTN_RIGHT  = btn; break;
        case 17: Config::SYS_BTN_OPTION = btn; break;
        case 18: Config::SYS_BTN_DIFF   = btn; break;
        case 19: Config::SYS_BTN_SORT   = btn; break;
        case 20: Config::SYS_BTN_RANDOM = btn; break;
        // 2P KEY CONFIG (cursor==2, step 22-32)
        case 22: Config::BTN_2P_EXIT    = btn; break;
        case 23: Config::BTN_2P_LANE1   = btn; break;
        case 24: Config::BTN_2P_LANE2   = btn; break;
        case 25: Config::BTN_2P_LANE3   = btn; break;
        case 26: Config::BTN_2P_LANE4   = btn; break;
        case 27: Config::BTN_2P_LANE5   = btn; break;
        case 28: Config::BTN_2P_LANE6   = btn; break;
        case 29: Config::BTN_2P_LANE7   = btn; break;
        case 30: Config::BTN_2P_LANE8_A = btn; break;
        case 31: Config::BTN_2P_LANE8_B = btn; break;
    }
    configStep++;
    bool finished = (cursor == 1 && configStep > 10)
                 || (cursor == 2 && configStep > 31)
                 || (cursor == 3 && configStep > 20);
    if (finished) {
        Config::save();
        state = OptionState::SELECTING_ITEM;
        configStep = 0;
        updateItemList();
    }
}


