#ifndef CONFIG_HPP
#define CONFIG_HPP

#include <string>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <thread>
#include <atomic>

namespace Config {
    // 画面・レイアウト設定
    inline int SCREEN_WIDTH = 1280;
    inline int SCREEN_HEIGHT = 720;
    inline int JUDGMENT_LINE_Y = 482;
    inline int LANE_WIDTH = 60;
    inline int SCRATCH_WIDTH = 90;
    inline int LANE_START_X = 385;

    // --- ゲームプレイ設定 (IIDX 仕様アップデート) ---
    inline constexpr int HS_BASE = 174728;
    inline int VISIBLE_PX = 482;
    inline int GREEN_NUMBER = 300; 
    
    inline double HIGH_SPEED = 1.0; 
    inline int PLAY_SIDE = 1; 
    inline int PLAY_MODE = 0;  // ★2P VS: 0=シングル, 1=2P VS

    // --- 【追加】LIFT / SUDDEN+ 設定 ---
    inline int SUDDEN_PLUS = 0; // 0 ~ 1000 (px) 画面上部を隠す
    inline int LIFT = 0;        // 0 ~ 1000 (px) 判定ラインを持ち上げる

    // 1. STYLE
    inline int PLAY_OPTION = 0;
    // 2. GAUGE
    inline int GAUGE_OPTION = 0;
    // 3. ASSIST
    inline int ASSIST_OPTION = 0;
    // 4. EX OPTION
    inline int EX_OPTION = 0; // 0=OFF 1=ALL SCRATCH 2=SCR ONLY 3=MORE NOTES 4=タピオカウメェス
    inline int MORE_NOTES_COUNT = 50; // MORE NOTES: 追加するノーツ数 (0以上、上限なし)

    // --- ゲージ表示設定 ---
    inline int GAUGE_DISPLAY_TYPE = 1;
    // 【追加】段位ゲージ開始％設定
    inline int DAN_GAUGE_START_PERCENT = 100;

    // --- 【追加】判定設定 ---
    inline int JUDGE_OFFSET = 0;  // 判定オフセット(ms): 正の値で判定が遅くなる
    inline int VISUAL_OFFSET = 0; // 表示オフセット(px): ノーツの見た目位置だけをずらす。JUDGE_OFFSETと独立。
    inline int SHOW_FAST_SLOW = 1; // FAST/SLOW表示: 0=OFF, 1=ON, 2=DETAIL(ms表示)

    // --- ボム演出設定 ---
    inline int BOMB_DURATION_MS = 300; // ボム1発の表示時間(ms) 50〜1000
    inline int BOMB_SIZE_FACTOR = 420; // ボムサイズ係数 (LANE_WIDTH * BOMB_SIZE_FACTOR / 100) * 3
                                       // デフォルト420 = LANE_WIDTH*1.4*3 と同等

    // --- 【追加】システム設定 ---
    inline int START_UP_OPTION = 1; // 0: Title, 1: Select (デフォルト選曲画面)
    inline std::string SORT_NAME = "DEFAULT"; // 【追加】現在のソート名を表示するための変数

    // --- 【追加】仮想フォルダ設定 ---
    inline int FOLDER_NOTES_MIN = 0;    // 最小ノーツ数
    inline int FOLDER_NOTES_MAX = 2000; // 最大ノーツ数
    // 各フォルダの表示フラグ
    inline bool SHOW_LEVEL_FOLDER = true;
    inline bool SHOW_LAMP_FOLDER = true;
    inline bool SHOW_RANK_FOLDER = true; // 【追加】DJランクフォルダ表示フラグ
    inline bool SHOW_CHART_TYPE_FOLDER = true;
    inline bool SHOW_NOTES_RANGE_FOLDER = true;
    inline bool SHOW_ALPHA_FOLDER = true;

    // --- パス設定 ---
#ifdef __SWITCH__
    inline std::string ROOT_PATH = "sdmc:/switch/bmsplayer/";
#elif defined(__APPLE__)
    inline std::string ROOT_PATH = "./";   // Mac: 実行ファイルと同じフォルダ
#else
    inline std::string ROOT_PATH = "./";   // Linux/Windows
#endif
    inline std::string BMS_PATH = ROOT_PATH + "BMS";
    inline std::string FONT_PATH = ROOT_PATH + "font.ttf";
    inline std::string SCORE_PATH = ROOT_PATH + "scores/";

    // --- 判定幅の設定 (ms) ---
    inline double JUDGE_PGREAT = 16.67; 
    inline double JUDGE_GREAT  = 33.33; 
    inline double JUDGE_GOOD   = 116.67;
    inline double JUDGE_BAD    = 250.00;
    inline double JUDGE_POOR   = 333.33;

    // キーコンフィグ (1P)
    inline int BTN_LANE1 = 12; 
    inline int BTN_LANE2 = 13; 
    inline int BTN_LANE3 = 14; 
    inline int BTN_LANE4 = 15; 
    inline int BTN_LANE5 = 3;  
    inline int BTN_LANE6 = 2;  
    inline int BTN_LANE7 = 0;  
    inline int BTN_LANE8_A = 6; 
    inline int BTN_LANE8_B = 7; 
    inline int BTN_EXIT  = 10;  
    inline int BTN_EFFECT = 11; 

    // キーコンフィグ (2P) - 2P VS モード用。デフォルトは 1P と同じ番号。
    // 2P 側のコントローラは別の SDL_Joystick インスタンスになるため
    // ボタン番号は 1P と独立して設定できる。
    inline int BTN_2P_LANE1   = 12;
    inline int BTN_2P_LANE2   = 13;
    inline int BTN_2P_LANE3   = 14;
    inline int BTN_2P_LANE4   = 15;
    inline int BTN_2P_LANE5   = 3;
    inline int BTN_2P_LANE6   = 2;
    inline int BTN_2P_LANE7   = 0;
    inline int BTN_2P_LANE8_A = 6;
    inline int BTN_2P_LANE8_B = 7;
    inline int BTN_2P_EXIT    = 10; // 2P の START ボタン (サドプラ/リフト操作に使用)

    // --- システム・選曲用キーコンフィグ ---
    inline int SYS_BTN_DECIDE = 15;
    inline int SYS_BTN_BACK   = 13;
    inline int SYS_BTN_UP      = 14;
    inline int SYS_BTN_DOWN    = 3; 
    inline int SYS_BTN_LEFT    = 12;
    inline int SYS_BTN_RIGHT  = 0; 
    inline int SYS_BTN_OPTION = 10;
    inline int SYS_BTN_DIFF   = 11;
    inline int SYS_BTN_SORT   = 4;  // 【追加】
    inline int SYS_BTN_RANDOM = 5;  // 【追加】
    inline int BTN_SYSTEM      = 10; 

    // ────────────────────────────────────────────────────────
    //  非同期セーブ管理
    //
    //  旧実装: save() が std::ofstream でメインスレッドをブロックしていた。
    //          SD カードへのランダムライトは最悪 100ms 以上かかるため、
    //          曲間遷移のたびにフリーズが発生していた。
    //
    //  新実装: markDirty() で変更フラグを立て、saveAsync() でバックグラウンド
    //          スレッドに書き込みをオフロードする。
    //          - markDirty(): 設定を変更した直後に呼ぶ（ゼロコスト）
    //          - saveAsync(): フェードアウト後など適切なタイミングで呼ぶ。
    //            前回の書き込みが完了していれば即 detach、完了待ちなら skip。
    //          - flushSave(): アプリ終了前に必ず呼ぶ。書き込み完了を待つ。
    //
    //  【修正】旧実装では writeToFile() がグローバル変数を直接参照していたため、
    //          バックグラウンドスレッドの読み取りとメインスレッドの書き込みが
    //          同時に発生し、データレース（未定義動作）によりクラッシュしていた。
    //          Snapshot 構造体にすべての設定値をコピーし、スレッドには
    //          Snapshot のコピーのみを渡すことでデータレースを完全に排除した。
    // ────────────────────────────────────────────────────────

    // 内部実装用（直接触らないこと）
    namespace detail {
        inline std::atomic<bool> isDirty{false};
        inline std::atomic<bool> isSaving{false};
    }

    /**
     * @brief 書き込みスレッドに渡すスナップショット構造体。
     *        saveAsync() 呼び出し時点でメインスレッドが全変数をコピーする。
     *        スレッドはこの構造体のコピーのみを参照し、グローバル変数には
     *        一切アクセスしないため、データレースが発生しない。
     */
    struct Snapshot {
        int    PLAY_SIDE;
        int    PLAY_MODE;
        double HIGH_SPEED;
        int    GREEN_NUMBER;
        int    VISIBLE_PX;
        int    SUDDEN_PLUS;
        int    LIFT;
        int    LANE_WIDTH;
        int    SCRATCH_WIDTH;
        int    PLAY_OPTION;
        int    GAUGE_OPTION;
        int    ASSIST_OPTION;
        int    EX_OPTION;
        int    MORE_NOTES_COUNT;
        int    GAUGE_DISPLAY_TYPE;
        int    DAN_GAUGE_START_PERCENT;
        int    JUDGE_OFFSET;
        int    VISUAL_OFFSET;
        int    SHOW_FAST_SLOW;
        int    BOMB_DURATION_MS;
        int    BOMB_SIZE_FACTOR;
        int    START_UP_OPTION;
        int    FOLDER_NOTES_MIN;
        int    FOLDER_NOTES_MAX;
        bool   SHOW_LEVEL_FOLDER;
        bool   SHOW_LAMP_FOLDER;
        bool   SHOW_RANK_FOLDER;
        bool   SHOW_CHART_TYPE_FOLDER;
        bool   SHOW_NOTES_RANGE_FOLDER;
        bool   SHOW_ALPHA_FOLDER;
        int    BTN_LANE1, BTN_LANE2, BTN_LANE3, BTN_LANE4;
        int    BTN_LANE5, BTN_LANE6, BTN_LANE7;
        int    BTN_LANE8_A, BTN_LANE8_B;
        int    BTN_EXIT, BTN_EFFECT, BTN_SYSTEM;
        int    BTN_2P_LANE1, BTN_2P_LANE2, BTN_2P_LANE3, BTN_2P_LANE4;
        int    BTN_2P_LANE5, BTN_2P_LANE6, BTN_2P_LANE7;
        int    BTN_2P_LANE8_A, BTN_2P_LANE8_B, BTN_2P_EXIT;
        int    SYS_BTN_DECIDE, SYS_BTN_BACK;
        int    SYS_BTN_UP, SYS_BTN_DOWN, SYS_BTN_LEFT, SYS_BTN_RIGHT;
        int    SYS_BTN_OPTION, SYS_BTN_DIFF, SYS_BTN_SORT, SYS_BTN_RANDOM;
    };

    /**
     * @brief 現在の全設定値を Snapshot にコピーして返す。
     *        必ずメインスレッドから呼ぶこと。
     */
    inline Snapshot takeSnapshot() {
        Snapshot s;
        s.PLAY_SIDE              = PLAY_SIDE;
        s.PLAY_MODE              = PLAY_MODE;
        s.HIGH_SPEED             = HIGH_SPEED;
        s.GREEN_NUMBER           = GREEN_NUMBER;
        s.VISIBLE_PX             = VISIBLE_PX;
        s.SUDDEN_PLUS            = SUDDEN_PLUS;
        s.LIFT                   = LIFT;
        s.LANE_WIDTH             = LANE_WIDTH;
        s.SCRATCH_WIDTH          = SCRATCH_WIDTH;
        s.PLAY_OPTION            = PLAY_OPTION;
        s.GAUGE_OPTION           = GAUGE_OPTION;
        s.ASSIST_OPTION          = ASSIST_OPTION;
        s.EX_OPTION              = EX_OPTION;
        s.MORE_NOTES_COUNT       = MORE_NOTES_COUNT;
        s.GAUGE_DISPLAY_TYPE     = GAUGE_DISPLAY_TYPE;
        s.DAN_GAUGE_START_PERCENT = DAN_GAUGE_START_PERCENT;
        s.JUDGE_OFFSET           = JUDGE_OFFSET;
        s.VISUAL_OFFSET          = VISUAL_OFFSET;
        s.SHOW_FAST_SLOW         = SHOW_FAST_SLOW;
        s.BOMB_DURATION_MS       = BOMB_DURATION_MS;
        s.BOMB_SIZE_FACTOR       = BOMB_SIZE_FACTOR;
        s.START_UP_OPTION        = START_UP_OPTION;
        s.FOLDER_NOTES_MIN       = FOLDER_NOTES_MIN;
        s.FOLDER_NOTES_MAX       = FOLDER_NOTES_MAX;
        s.SHOW_LEVEL_FOLDER      = SHOW_LEVEL_FOLDER;
        s.SHOW_LAMP_FOLDER       = SHOW_LAMP_FOLDER;
        s.SHOW_RANK_FOLDER       = SHOW_RANK_FOLDER;
        s.SHOW_CHART_TYPE_FOLDER  = SHOW_CHART_TYPE_FOLDER;
        s.SHOW_NOTES_RANGE_FOLDER = SHOW_NOTES_RANGE_FOLDER;
        s.SHOW_ALPHA_FOLDER      = SHOW_ALPHA_FOLDER;
        s.BTN_LANE1              = BTN_LANE1;
        s.BTN_LANE2              = BTN_LANE2;
        s.BTN_LANE3              = BTN_LANE3;
        s.BTN_LANE4              = BTN_LANE4;
        s.BTN_LANE5              = BTN_LANE5;
        s.BTN_LANE6              = BTN_LANE6;
        s.BTN_LANE7              = BTN_LANE7;
        s.BTN_LANE8_A            = BTN_LANE8_A;
        s.BTN_LANE8_B            = BTN_LANE8_B;
        s.BTN_EXIT               = BTN_EXIT;
        s.BTN_EFFECT             = BTN_EFFECT;
        s.BTN_SYSTEM             = BTN_SYSTEM;
        s.BTN_2P_LANE1           = BTN_2P_LANE1;
        s.BTN_2P_LANE2           = BTN_2P_LANE2;
        s.BTN_2P_LANE3           = BTN_2P_LANE3;
        s.BTN_2P_LANE4           = BTN_2P_LANE4;
        s.BTN_2P_LANE5           = BTN_2P_LANE5;
        s.BTN_2P_LANE6           = BTN_2P_LANE6;
        s.BTN_2P_LANE7           = BTN_2P_LANE7;
        s.BTN_2P_LANE8_A         = BTN_2P_LANE8_A;
        s.BTN_2P_LANE8_B         = BTN_2P_LANE8_B;
        s.BTN_2P_EXIT            = BTN_2P_EXIT;
        s.SYS_BTN_DECIDE         = SYS_BTN_DECIDE;
        s.SYS_BTN_BACK           = SYS_BTN_BACK;
        s.SYS_BTN_UP             = SYS_BTN_UP;
        s.SYS_BTN_DOWN           = SYS_BTN_DOWN;
        s.SYS_BTN_LEFT           = SYS_BTN_LEFT;
        s.SYS_BTN_RIGHT          = SYS_BTN_RIGHT;
        s.SYS_BTN_OPTION         = SYS_BTN_OPTION;
        s.SYS_BTN_DIFF           = SYS_BTN_DIFF;
        s.SYS_BTN_SORT           = SYS_BTN_SORT;
        s.SYS_BTN_RANDOM         = SYS_BTN_RANDOM;
        return s;
    }

    /**
     * @brief 設定が変更されたことをマークする（ゼロコスト）。
     * 設定値を変更した箇所（SceneOption, ScenePlay等）の直後に呼ぶ。
     */
    inline void markDirty() {
        detail::isDirty.store(true, std::memory_order_relaxed);
    }

    /**
     * @brief config.txt から設定を読み込みます。
     */
    inline void load() {
        std::ifstream file(ROOT_PATH + "config.txt");
        if (!file.is_open()) return;
        std::string line;
        while (std::getline(file, line)) {
            size_t sep = line.find('=');
            if (sep == std::string::npos) continue;
            std::string key = line.substr(0, sep);
            std::string val = line.substr(sep + 1);

            try {
                if (key == "PLAY_SIDE") PLAY_SIDE = std::stoi(val);
                else if (key == "PLAY_MODE") PLAY_MODE = std::stoi(val);  // ★2P VS
                else if (key == "HIGH_SPEED") HIGH_SPEED = std::stod(val);
                else if (key == "GREEN_NUMBER") GREEN_NUMBER = std::stoi(val);
                else if (key == "VISIBLE_PX") VISIBLE_PX = std::stoi(val);
                // 【追加】LIFT/SUD+
                else if (key == "SUDDEN_PLUS") SUDDEN_PLUS = std::stoi(val);
                else if (key == "LIFT") LIFT = std::stoi(val);
                
                else if (key == "LANE_WIDTH") LANE_WIDTH = std::stoi(val);
                else if (key == "SCRATCH_WIDTH") SCRATCH_WIDTH = std::stoi(val);
                else if (key == "PLAY_OPTION") PLAY_OPTION = std::stoi(val);
                else if (key == "GAUGE_OPTION") GAUGE_OPTION = std::stoi(val);
                else if (key == "ASSIST_OPTION") ASSIST_OPTION = std::stoi(val);
                else if (key == "EX_OPTION") EX_OPTION = std::stoi(val);
                else if (key == "MORE_NOTES_COUNT") MORE_NOTES_COUNT = std::stoi(val);
                else if (key == "GAUGE_DISPLAY_TYPE") GAUGE_DISPLAY_TYPE = std::stoi(val);
                else if (key == "DAN_GAUGE_START_PERCENT") DAN_GAUGE_START_PERCENT = std::stoi(val); 
                else if (key == "JUDGE_OFFSET") JUDGE_OFFSET = std::stoi(val);
                else if (key == "VISUAL_OFFSET") VISUAL_OFFSET = std::stoi(val);
                else if (key == "SHOW_FAST_SLOW") SHOW_FAST_SLOW = std::stoi(val);
                else if (key == "BOMB_DURATION_MS") BOMB_DURATION_MS = std::stoi(val);
                else if (key == "BOMB_SIZE_FACTOR") BOMB_SIZE_FACTOR = std::stoi(val);
                else if (key == "START_UP_OPTION") START_UP_OPTION = std::stoi(val);
                else if (key == "FOLDER_NOTES_MIN") FOLDER_NOTES_MIN = std::stoi(val);
                else if (key == "FOLDER_NOTES_MAX") FOLDER_NOTES_MAX = std::stoi(val);
                else if (key == "SHOW_LEVEL_FOLDER") SHOW_LEVEL_FOLDER = (std::stoi(val) != 0);
                else if (key == "SHOW_LAMP_FOLDER") SHOW_LAMP_FOLDER = (std::stoi(val) != 0);
                else if (key == "SHOW_RANK_FOLDER") SHOW_RANK_FOLDER = (std::stoi(val) != 0); 
                else if (key == "SHOW_CHART_TYPE_FOLDER") SHOW_CHART_TYPE_FOLDER = (std::stoi(val) != 0);
                else if (key == "SHOW_NOTES_RANGE_FOLDER") SHOW_NOTES_RANGE_FOLDER = (std::stoi(val) != 0);
                else if (key == "SHOW_ALPHA_FOLDER") SHOW_ALPHA_FOLDER = (std::stoi(val) != 0);
                else if (key == "BTN_LANE1") BTN_LANE1 = std::stoi(val);
                else if (key == "BTN_LANE2") BTN_LANE2 = std::stoi(val);
                else if (key == "BTN_LANE3") BTN_LANE3 = std::stoi(val);
                else if (key == "BTN_LANE4") BTN_LANE4 = std::stoi(val);
                else if (key == "BTN_LANE5") BTN_LANE5 = std::stoi(val);
                else if (key == "BTN_LANE6") BTN_LANE6 = std::stoi(val);
                else if (key == "BTN_LANE7") BTN_LANE7 = std::stoi(val);
                else if (key == "BTN_LANE8_A") BTN_LANE8_A = std::stoi(val);
                else if (key == "BTN_LANE8_B") BTN_LANE8_B = std::stoi(val);
                else if (key == "BTN_EXIT") BTN_EXIT = std::stoi(val);
                else if (key == "BTN_EFFECT") BTN_EFFECT = std::stoi(val);
                else if (key == "BTN_SYSTEM") BTN_SYSTEM = std::stoi(val); 
                // 2P ボタン
                else if (key == "BTN_2P_LANE1")   BTN_2P_LANE1   = std::stoi(val);
                else if (key == "BTN_2P_LANE2")   BTN_2P_LANE2   = std::stoi(val);
                else if (key == "BTN_2P_LANE3")   BTN_2P_LANE3   = std::stoi(val);
                else if (key == "BTN_2P_LANE4")   BTN_2P_LANE4   = std::stoi(val);
                else if (key == "BTN_2P_LANE5")   BTN_2P_LANE5   = std::stoi(val);
                else if (key == "BTN_2P_LANE6")   BTN_2P_LANE6   = std::stoi(val);
                else if (key == "BTN_2P_LANE7")   BTN_2P_LANE7   = std::stoi(val);
                else if (key == "BTN_2P_LANE8_A") BTN_2P_LANE8_A = std::stoi(val);
                else if (key == "BTN_2P_LANE8_B") BTN_2P_LANE8_B = std::stoi(val);
                else if (key == "BTN_2P_EXIT")    BTN_2P_EXIT    = std::stoi(val);
                else if (key == "SYS_BTN_DECIDE") SYS_BTN_DECIDE = std::stoi(val);
                else if (key == "SYS_BTN_BACK")   SYS_BTN_BACK   = std::stoi(val);
                else if (key == "SYS_BTN_UP")      SYS_BTN_UP      = std::stoi(val);
                else if (key == "SYS_BTN_DOWN")   SYS_BTN_DOWN   = std::stoi(val);
                else if (key == "SYS_BTN_LEFT")   SYS_BTN_LEFT   = std::stoi(val);
                else if (key == "SYS_BTN_RIGHT")  SYS_BTN_RIGHT  = std::stoi(val);
                else if (key == "SYS_BTN_OPTION") SYS_BTN_OPTION = std::stoi(val);
                else if (key == "SYS_BTN_DIFF")   SYS_BTN_DIFF   = std::stoi(val);
                else if (key == "SYS_BTN_SORT")   SYS_BTN_SORT   = std::stoi(val);
                else if (key == "SYS_BTN_RANDOM") SYS_BTN_RANDOM = std::stoi(val);
            } catch (...) {
                continue;
            }
        }
    }

    /**
     * @brief Snapshot の内容をファイルに書き出す。
     *        グローバル変数には一切アクセスしないため、バックグラウンド
     *        スレッドから安全に呼び出せる。
     */
    inline void writeSnapshot(const std::string& path, const Snapshot& s) {
        std::ofstream file(path);
        if (!file.is_open()) {
            std::cerr << "Config Error: Could not save config.txt" << std::endl;
            return;
        }

        file << "PLAY_SIDE="              << s.PLAY_SIDE              << "\n";
        file << "PLAY_MODE="              << s.PLAY_MODE              << "\n";  // ★2P VS
        file << "HIGH_SPEED="             << std::fixed << std::setprecision(1) << s.HIGH_SPEED << "\n";
        file << "GREEN_NUMBER="           << s.GREEN_NUMBER           << "\n";
        file << "VISIBLE_PX="             << s.VISIBLE_PX             << "\n";
        // 【追加】LIFT/SUD+
        file << "SUDDEN_PLUS="            << s.SUDDEN_PLUS            << "\n";
        file << "LIFT="                   << s.LIFT                   << "\n";

        file << "LANE_WIDTH="             << s.LANE_WIDTH             << "\n";
        file << "SCRATCH_WIDTH="          << s.SCRATCH_WIDTH          << "\n";
        file << "PLAY_OPTION="            << s.PLAY_OPTION            << "\n";
        file << "GAUGE_OPTION="           << s.GAUGE_OPTION           << "\n";
        file << "ASSIST_OPTION="          << s.ASSIST_OPTION          << "\n";
        file << "EX_OPTION="              << s.EX_OPTION              << "\n";
        file << "MORE_NOTES_COUNT="       << s.MORE_NOTES_COUNT       << "\n";
        file << "GAUGE_DISPLAY_TYPE="     << s.GAUGE_DISPLAY_TYPE     << "\n";
        file << "DAN_GAUGE_START_PERCENT=" << s.DAN_GAUGE_START_PERCENT << "\n";
        file << "JUDGE_OFFSET="           << s.JUDGE_OFFSET           << "\n";
        file << "VISUAL_OFFSET="          << s.VISUAL_OFFSET          << "\n";
        file << "SHOW_FAST_SLOW="         << s.SHOW_FAST_SLOW         << "\n";
        file << "BOMB_DURATION_MS="       << s.BOMB_DURATION_MS       << "\n";
        file << "BOMB_SIZE_FACTOR="       << s.BOMB_SIZE_FACTOR       << "\n";
        file << "START_UP_OPTION="        << s.START_UP_OPTION        << "\n";
        file << "FOLDER_NOTES_MIN="       << s.FOLDER_NOTES_MIN       << "\n";
        file << "FOLDER_NOTES_MAX="       << s.FOLDER_NOTES_MAX       << "\n";
        file << "SHOW_LEVEL_FOLDER="      << (s.SHOW_LEVEL_FOLDER      ? 1 : 0) << "\n";
        file << "SHOW_LAMP_FOLDER="       << (s.SHOW_LAMP_FOLDER       ? 1 : 0) << "\n";
        file << "SHOW_RANK_FOLDER="       << (s.SHOW_RANK_FOLDER       ? 1 : 0) << "\n";
        file << "SHOW_CHART_TYPE_FOLDER=" << (s.SHOW_CHART_TYPE_FOLDER  ? 1 : 0) << "\n";
        file << "SHOW_NOTES_RANGE_FOLDER=" << (s.SHOW_NOTES_RANGE_FOLDER ? 1 : 0) << "\n";
        file << "SHOW_ALPHA_FOLDER="      << (s.SHOW_ALPHA_FOLDER      ? 1 : 0) << "\n";
        file << "BTN_LANE1="              << s.BTN_LANE1              << "\n";
        file << "BTN_LANE2="              << s.BTN_LANE2              << "\n";
        file << "BTN_LANE3="              << s.BTN_LANE3              << "\n";
        file << "BTN_LANE4="              << s.BTN_LANE4              << "\n";
        file << "BTN_LANE5="              << s.BTN_LANE5              << "\n";
        file << "BTN_LANE6="              << s.BTN_LANE6              << "\n";
        file << "BTN_LANE7="              << s.BTN_LANE7              << "\n";
        file << "BTN_LANE8_A="            << s.BTN_LANE8_A            << "\n";
        file << "BTN_LANE8_B="            << s.BTN_LANE8_B            << "\n";
        file << "BTN_EXIT="               << s.BTN_EXIT               << "\n";
        file << "BTN_EFFECT="             << s.BTN_EFFECT             << "\n";
        file << "BTN_SYSTEM="             << s.BTN_SYSTEM             << "\n";
        // 2P ボタン
        file << "BTN_2P_LANE1="           << s.BTN_2P_LANE1           << "\n";
        file << "BTN_2P_LANE2="           << s.BTN_2P_LANE2           << "\n";
        file << "BTN_2P_LANE3="           << s.BTN_2P_LANE3           << "\n";
        file << "BTN_2P_LANE4="           << s.BTN_2P_LANE4           << "\n";
        file << "BTN_2P_LANE5="           << s.BTN_2P_LANE5           << "\n";
        file << "BTN_2P_LANE6="           << s.BTN_2P_LANE6           << "\n";
        file << "BTN_2P_LANE7="           << s.BTN_2P_LANE7           << "\n";
        file << "BTN_2P_LANE8_A="         << s.BTN_2P_LANE8_A         << "\n";
        file << "BTN_2P_LANE8_B="         << s.BTN_2P_LANE8_B         << "\n";
        file << "BTN_2P_EXIT="            << s.BTN_2P_EXIT            << "\n";
        file << "SYS_BTN_DECIDE="         << s.SYS_BTN_DECIDE         << "\n";
        file << "SYS_BTN_BACK="           << s.SYS_BTN_BACK           << "\n";
        file << "SYS_BTN_UP="             << s.SYS_BTN_UP             << "\n";
        file << "SYS_BTN_DOWN="           << s.SYS_BTN_DOWN           << "\n";
        file << "SYS_BTN_LEFT="           << s.SYS_BTN_LEFT           << "\n";
        file << "SYS_BTN_RIGHT="          << s.SYS_BTN_RIGHT          << "\n";
        file << "SYS_BTN_OPTION="         << s.SYS_BTN_OPTION         << "\n";
        file << "SYS_BTN_DIFF="           << s.SYS_BTN_DIFF           << "\n";
        file << "SYS_BTN_SORT="           << s.SYS_BTN_SORT           << "\n";
        file << "SYS_BTN_RANDOM="         << s.SYS_BTN_RANDOM         << "\n";

        file.close();
    }

    /**
     * @brief 設定を非同期でバックグラウンドスレッドに書き込む。
     *
     * isDirty が false（変更なし）なら何もしない。
     * isSaving が true（前の書き込みが完了していない）なら skip。
     * それ以外は std::thread を detach して即座に返る（メインスレッドをブロックしない）。
     *
     * 呼び出しタイミング: fadeOut後、曲間遷移時など。
     * 旧 save() を呼んでいた箇所をすべてこれに置き換える。
     */
    inline void saveAsync() {
        if (!detail::isDirty.load(std::memory_order_relaxed)) return;
        detail::isDirty.store(false, std::memory_order_relaxed);
        writeSnapshot(ROOT_PATH + "config.txt", takeSnapshot());
    }
    /**
     * @brief アプリ終了前に必ず呼ぶ。書き込み完了を待つ（ブロッキング）。
     * 終了時のみ呼ばれるため、ここでのブロックは許容できる。
     */
    inline void flushSave() {
        if (detail::isDirty.load(std::memory_order_relaxed)) {
            writeSnapshot(ROOT_PATH + "config.txt", takeSnapshot());
            detail::isDirty.store(false, std::memory_order_relaxed);
        }
    }

    /**
     * @brief 【後方互換】既存の save() 呼び出し箇所はそのまま動く。
     * 内部では saveAsync() に委譲する。
     * 旧コードを一括検索置換する前の移行期間用。
     */
    inline void save() {
        markDirty();
        saveAsync();
    }
}
#endif
