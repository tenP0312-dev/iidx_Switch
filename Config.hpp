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
    // ============================================================
    //  ★ レイアウト定数 — ここを編集するだけで全座標が変わる ★
    //
    //  【レーン幅】  lane 1,3,5,7=白鍵  lane 2,4,6=黒鍵  lane 8=スクラッチ
    //    総レーン幅 = 白×4 + 黒×3 + scratch = 44×4+62×3+88 = 462px
    static constexpr int AC_KEY_WHITE  = 37;   // 白鍵幅 (px)
    static constexpr int AC_KEY_BLACK  = 27;   // 黒鍵幅 (px)
    static constexpr int AC_SCRATCH    = 60;   // スクラッチ幅 (px)

    //  【レーン全体の位置】
    //    1P: スクラッチ左端が LANE_BASE_X_1P
    //    2P: 自動で鏡像配置 (SCREEN_WIDTH - LANE_BASE_X_1P - totalWidth)
    static constexpr int LANE_BASE_X_1P = 50;  // 1Pレーン全体の左端X (px)

    //  【BGA表示中心位置】
    //    BGAは中心座標を指定。アスペクト比を保ったまま高さ512pxに自動スケール。
    //    1P: レーン右側の空白中央  2P: レーン左側の空白中央
    static constexpr int BGA_CENTER_X_1P = 820; // 1P BGA中心X (px)  ← (512+462+50+1280)/2 ≒ 871
    static constexpr int BGA_CENTER_X_2P = 460; // 2P BGA中心X (px)  ← 1280 - BGA_CENTER_X_1P = 460
    inline int BGA_CENTER_Y  = 340; // BGA中心Y (px) ← この値を変えるとBGAのY位置が変わる (config.hpp のみ)
    inline int BGA_HEIGHT    = 547; // BGA表示高さ (px) ← アスペクト比を保って横幅も自動スケール (config.hpp のみ)

    //  【プログレスバー】
    //    1P: レーン左端の外側  2P: レーン右端の外側
    static constexpr int PROGRESS_BAR_X_OFFSET = 16; // レーン端からの距離 (px)
    static constexpr int PROGRESS_BAR_Y        = 39;  // 上端Y (px)
    static constexpr int PROGRESS_BAR_H        = 418; // 高さ (px)

    //  【ゲージ】
    //    X: 1P/2P それぞれ指定。W: 幅指定
    //    Y: 画面下端から GAUGE_BOTTOM_MARGIN 上
    static constexpr int GAUGE_X_1P         = 30;  // 1P ゲージ左端X (px)  ← LANE_BASE_X_1P と合わせると自然
    static constexpr int GAUGE_X_2P         = 949; // 2P ゲージ左端X (px)  ← 1280-GAUGE_X_1P-GAUGE_W = 949
    static constexpr int GAUGE_W            = 301; // ゲージ幅 (px)        ← 総レーン幅+10 = 472
    static constexpr int GAUGE_BOTTOM_MARGIN = 101; // 画面下端からゲージ下端までの距離 (px)

    //  【判定表示・FAST/SLOW】
    //    X はレーン中央に自動計算 (ll.baseX + ll.totalWidth/2)
    //    Y は JUDGMENT_LINE_Y 基準のオフセットで指定
    static constexpr int JUDGE_Y_OFFSET    = 170; // 判定表示: 判定ラインより上 (px)
    static constexpr int FASTSLOW_Y_OFFSET = 195; // FAST/SLOW: 判定ラインより上 (px)
    // ============================================================

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
    inline int EX_OPTION = 0; // 0=OFF 1=ALL SCRATCH 2=MORE NOTES
    inline int MORE_NOTES_COUNT = 50; // MORE NOTES: 追加するノーツ数 (0以上、上限なし)
    // 5. LN TYPE
    // 0=CN (Charge Note): 終端でタイミング判定。早離しPOOR、押し続けBAD/POOR
    // 1=HCN (Hell Charge Note): 将来実装用プレースホルダー。現在はCNと同動作
    // 2=LN (Long Note): 終端超えでも押し続けてOK。終端以降に離してもP-GREAT
    inline int LN_OPTION = 0;

    // --- ゲージ表示設定 ---
    inline int GAUGE_DISPLAY_TYPE = 1;

    // ゲージ％数字表示設定
    // GAUGE_NUM_SHOW: 0=非表示, 1=整数(GAUGE_DISPLAY_TYPEに従う), 2=小数(0.1%刻み, gauge_number_detail使用)
    // GAUGE_NUM_X: 数字の基準X座標。1P/2Pで独立して設定（スクリーン絶対座標）
    // GAUGE_NUM_ALIGNにより左端/中心/右端として解釈される
    inline int GAUGE_NUM_SHOW = 1;
    inline int GAUGE_NUM_X_1P = 324;   // 1P 数字表示 左端X (px)
    inline int GAUGE_NUM_X_2P = 1024; // 2P 数字表示 右揃え基準X (px)  ← 2Pゲージ右端1250-7px
    inline int GAUGE_NUM_Y_OFFSET = -20; // ゲージ上端からのYオフセット (px、負=ゲージより上)
    // GAUGE_NUM_ALIGN: 0=左揃え, 1=中央揃え, 2=右揃え
    inline int GAUGE_NUM_ALIGN = 2;
    // GAUGE_NUM_SCALE: 数字の表示サイズ (%)。100=画像そのまま、50=半分
    inline int GAUGE_NUM_SCALE = 80;
    // 【追加】段位ゲージ開始％設定
    inline int DAN_GAUGE_START_PERCENT = 100;

    // --- 【追加】判定設定 ---
    inline int JUDGE_OFFSET = 0;  // 判定オフセット(ms): 正の値で判定が遅くなる
    inline int VISUAL_OFFSET = 0; // 表示オフセット(px): ノーツの見た目位置だけをずらす。JUDGE_OFFSETと独立。
    inline int SHOW_FAST_SLOW = 1; // FAST/SLOW表示: 0=OFF, 1=ON, 2=DETAIL(ms表示)

    // --- フォントサイズ設定 (config.hpp のみ / config.txt には保存されない) ---
    // 数値を直接変更してください (単位: px)
    inline int FONT_SIZE_SMALL = 24; // 小テキスト用フォントサイズ (px) — HUD情報・ヒント文字など
    inline int FONT_SIZE_BIG   = 48; // 大テキスト用フォントサイズ (px) — シーンタイトル・判定テキストなど

    // --- ゲージ上昇フラッシュ設定 (config.hpp のみ / config.txt には保存されない) ---
    // gauge_up.png を使用。2%回復ごとに0.5秒・2f周期で点滅表示。
    // GAUGE_UP_X/Y: 画像左上座標 (1P/2P独立)
    inline int GAUGE_UP_SHOW    = 1;
    inline int GAUGE_UP_X_1P   = 316;  // 1P: 左上X座標
    inline int GAUGE_UP_Y_1P   = 610;  // 1P: 左上Y座標
    inline int GAUGE_UP_SCALE_1P = 90; // 1P: 拡大率(%)
    inline int GAUGE_UP_X_2P   = 964;  // 2P: 左上X座標 (1280-GAUGE_UP_X_1P = 964)
    inline int GAUGE_UP_Y_2P   = 610;  // 2P: 左上Y座標
    inline int GAUGE_UP_SCALE_2P = 90; // 2P: 拡大率(%)

    // --- ボム演出設定 (config.hpp のみ / config.txt には保存されない) ---
    inline int BOMB_DURATION_MS = 300; // ボム1発の表示時間(ms) 50〜1000
    inline int BOMB_SIZE        = 140; // ボム表示サイズ (px) — 全レーン共通

    // --- ターンテーブル設定 (config.hpp のみ / config.txt には保存されない) ---
    // turn_center.png を使用。中心座標・拡大率指定。1P/2P独立。
    // SCALE: 100=元サイズ, 200=2倍
    // SPEED: 上限なし。10=360deg/sec(毎秒1回転順方向), 負=逆。0=停止 (1P/2P共通)
    inline int TURNTABLE_SHOW         = 1;
    inline int TURNTABLE_X_1P        = 59;   // 1P: 中心X座標 (スクラッチ上)
    inline int TURNTABLE_Y_1P        = 530;  // 1P: 中心Y座標
    inline int TURNTABLE_SCALE_1P    = 100;  // 1P: 拡大率(%)
    inline int TURNTABLE_X_2P        = 1221; // 2P: 中心X座標 (1280-59=1221, 2Pスクラッチ上)
    inline int TURNTABLE_Y_2P        = 530;  // 2P: 中心Y座標
    inline int TURNTABLE_SCALE_2P    = 100;  // 2P: 拡大率(%)
    inline int TURNTABLE_SPEED_NORMAL = 1;   // 通常時 (1P/2P共通)
    inline int TURNTABLE_SPEED_A      = 8;   // スクラッチA押下時
    inline int TURNTABLE_SPEED_B      = -8;  // スクラッチB押下時

    // --- ハイスピード表示設定 (config.hpp のみ / config.txt には保存されない) ---
    // hs_number.png を使用。HIGH_SPEED*100 を整数表示 (例: ×2.50 → "250")
    // ALIGN: 0=左揃え, 1=中央揃え, 2=右揃え
    inline int HS_DISP_SHOW     = 1;
    inline int HS_DISP_ALIGN_1P = 1;    // 1P: 中央揃え
    inline int HS_DISP_SCALE_1P = 100;  // 1P: 拡大率(%)
    inline int HS_DISP_X_1P    = 276;   // 1P: 基準X座標
    inline int HS_DISP_Y_1P    = 638;   // 1P: Y座標
    inline int HS_DISP_ALIGN_2P = 1;    // 2P: 中央揃え (中央揃えはそのまま)
    inline int HS_DISP_SCALE_2P = 100;  // 2P: 拡大率(%)
    inline int HS_DISP_X_2P    = 1004;  // 2P: 基準X座標 (1280-276=1004)
    inline int HS_DISP_Y_2P    = 638;   // 2P: Y座標

    // --- スコア表示設定 (config.hpp のみ / config.txt には保存されない) ---
    // SCORE_ALIGN: 0=左揃え, 1=中央揃え, 2=右揃え (SCORE_X を基準点として解釈)
    // 2P: 右揃え (数字は右寄せ)
    inline int SCORE_SHOW     = 1;
    inline int SCORE_ALIGN_1P = 2;    // 1P: 右揃え
    inline int SCORE_SCALE_1P = 100;  // 1P: 拡大率(%)
    inline int SCORE_X_1P    = 215;   // 1P: 基準X座標 (右端)
    inline int SCORE_Y_1P    = 637;   // 1P: Y座標
    inline int SCORE_ALIGN_2P = 0;    // 2P: 右揃え
    inline int SCORE_SCALE_2P = 100;  // 2P: 拡大率(%)
    inline int SCORE_X_2P    = 1066;  // 2P: 基準X座標 (1280-215=1065)
    inline int SCORE_Y_2P    = 637;   // 2P: Y座標

    // --- BPM表示設定 (config.hpp のみ / config.txt には保存されない) ---
    // BPM_ALIGN: 0=左揃え, 1=中央揃え, 2=右揃え
    // BPM_SHOW_MINMAX: 0=変化なし曲ではMIN/MAX非表示, 1=常に表示
    // 2P: CUR/MIN/MAXは中央揃えのまま左右反転
    inline int BPM_SHOW         = 1;
    inline int BPM_SHOW_MINMAX  = 0;  // 0=変化なし曲では非表示, 1=常に表示
    inline int BPM_ALIGN_1P        = 1;   // 1P: 中央揃え
    inline int BPM_SCALE_1P        = 100; // 1P: 現在BPM表示サイズ(%)
    inline int BPM_MINMAX_SCALE_1P = 70;  // 1P: MIN/MAX表示サイズ(%)
    inline int BPM_CUR_X_1P        = 819; // 1P: 現在BPM 基準X
    inline int BPM_CUR_Y_1P        = 648; // 1P: 現在BPM Y
    inline int BPM_MIN_X_1P        = 754; // 1P: MIN BPM 基準X
    inline int BPM_MIN_Y_1P        = 655; // 1P: MIN BPM Y
    inline int BPM_MAX_X_1P        = 885; // 1P: MAX BPM 基準X
    inline int BPM_MAX_Y_1P        = 655; // 1P: MAX BPM Y
    inline int BPM_ALIGN_2P        = 1;   // 2P: 中央揃え (中央揃えはそのまま)
    inline int BPM_SCALE_2P        = 100; // 2P: 現在BPM表示サイズ(%)
    inline int BPM_MINMAX_SCALE_2P = 70;  // 2P: MIN/MAX表示サイズ(%)
    inline int BPM_CUR_X_2P        = 461; // 2P: 現在BPM 基準X (1280-819=461)
    inline int BPM_CUR_Y_2P        = 648; // 2P: 現在BPM Y
    inline int BPM_MIN_X_2P        = 526; // 2P: MIN BPM 基準X (1280-754=526, CURの右)
    inline int BPM_MIN_Y_2P        = 655; // 2P: MIN BPM Y
    inline int BPM_MAX_X_2P        = 395; // 2P: MAX BPM 基準X (1280-885=395, CURの左)
    inline int BPM_MAX_Y_2P        = 655; // 2P: MAX BPM Y

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
    inline std::string ROOT_PATH = "./";   // Mac: initRootPath() で上書きされる
#else
    inline std::string ROOT_PATH = "./";   // Linux/Windows
#endif
    inline std::string BMS_PATH = ROOT_PATH + "BMS";
    inline std::string FONT_PATH = ROOT_PATH + "font.ttf";
    inline std::string SCORE_PATH = ROOT_PATH + "scores/";

    // 実行ファイルの場所から ROOT_PATH を動的に設定する。
    // main() の先頭、Config::load() より前に呼ぶこと。
    // Switch は sdmc: 固定なので何もしない。
    inline void initRootPath(const char* argv0) {
#ifndef __SWITCH__
        if (!argv0) return;
        std::string exe(argv0);
        // 最後の / または \ までをディレクトリとして取得
        size_t slash = exe.find_last_of("/\\");
        std::string dir = (slash != std::string::npos) ? exe.substr(0, slash + 1) : "./";
        ROOT_PATH  = dir;
        BMS_PATH   = ROOT_PATH + "BMS";
        FONT_PATH  = ROOT_PATH + "font.ttf";
        SCORE_PATH = ROOT_PATH + "scores/";
#endif
    }

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
    inline int SYS_BTN_SORT   = 4;
    inline int SYS_BTN_RANDOM = 5;
    inline int BTN_SYSTEM      = 10; 

    // ────────────────────────────────────────────────────────
    //  非同期セーブ管理
    // ────────────────────────────────────────────────────────

    // 内部実装用（直接触らないこと）
    namespace detail {
        inline std::atomic<bool> isDirty{false};
        inline std::atomic<bool> isSaving{false};
    }

    struct Snapshot {
        int    PLAY_SIDE;
        int    PLAY_MODE;
        double HIGH_SPEED;
        int    GREEN_NUMBER;
        int    VISIBLE_PX;
        int    SUDDEN_PLUS;
        int    LIFT;
        int    PLAY_OPTION;
        int    GAUGE_OPTION;
        int    ASSIST_OPTION;
        int    EX_OPTION;
        int    MORE_NOTES_COUNT;
        int    LN_OPTION;
        int    GAUGE_DISPLAY_TYPE;
        int    DAN_GAUGE_START_PERCENT;
        int    JUDGE_OFFSET;
        int    VISUAL_OFFSET;
        int    SHOW_FAST_SLOW;
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

    inline Snapshot takeSnapshot() {
        Snapshot s;
        s.PLAY_SIDE              = PLAY_SIDE;
        s.PLAY_MODE              = PLAY_MODE;
        s.HIGH_SPEED             = HIGH_SPEED;
        s.GREEN_NUMBER           = GREEN_NUMBER;
        s.VISIBLE_PX             = VISIBLE_PX;
        s.SUDDEN_PLUS            = SUDDEN_PLUS;
        s.LIFT                   = LIFT;
        s.PLAY_OPTION            = PLAY_OPTION;
        s.GAUGE_OPTION           = GAUGE_OPTION;
        s.ASSIST_OPTION          = ASSIST_OPTION;
        s.EX_OPTION              = EX_OPTION;
        s.MORE_NOTES_COUNT       = MORE_NOTES_COUNT;
        s.LN_OPTION              = LN_OPTION;
        s.GAUGE_DISPLAY_TYPE     = GAUGE_DISPLAY_TYPE;
        s.DAN_GAUGE_START_PERCENT = DAN_GAUGE_START_PERCENT;
        s.JUDGE_OFFSET           = JUDGE_OFFSET;
        s.VISUAL_OFFSET          = VISUAL_OFFSET;
        s.SHOW_FAST_SLOW         = SHOW_FAST_SLOW;
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

    inline void markDirty() {
        detail::isDirty.store(true, std::memory_order_relaxed);
    }

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
                else if (key == "PLAY_MODE") PLAY_MODE = std::stoi(val);
                else if (key == "HIGH_SPEED") HIGH_SPEED = std::stod(val);
                else if (key == "GREEN_NUMBER") GREEN_NUMBER = std::stoi(val);
                else if (key == "VISIBLE_PX") VISIBLE_PX = std::stoi(val);
                else if (key == "SUDDEN_PLUS") SUDDEN_PLUS = std::stoi(val);
                else if (key == "LIFT") LIFT = std::stoi(val);
                else if (key == "PLAY_OPTION") PLAY_OPTION = std::stoi(val);
                else if (key == "GAUGE_OPTION") GAUGE_OPTION = std::stoi(val);
                else if (key == "ASSIST_OPTION") ASSIST_OPTION = std::stoi(val);
                else if (key == "EX_OPTION") EX_OPTION = std::stoi(val);
                else if (key == "MORE_NOTES_COUNT") MORE_NOTES_COUNT = std::stoi(val);
                else if (key == "LN_OPTION") LN_OPTION = std::stoi(val);
                else if (key == "GAUGE_DISPLAY_TYPE") GAUGE_DISPLAY_TYPE = std::stoi(val);
                else if (key == "DAN_GAUGE_START_PERCENT") DAN_GAUGE_START_PERCENT = std::stoi(val); 
                else if (key == "JUDGE_OFFSET") JUDGE_OFFSET = std::stoi(val);
                else if (key == "VISUAL_OFFSET") VISUAL_OFFSET = std::stoi(val);
                else if (key == "SHOW_FAST_SLOW") SHOW_FAST_SLOW = std::stoi(val);
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

    inline void writeSnapshot(const std::string& path, const Snapshot& s) {
        std::ofstream file(path);
        if (!file.is_open()) {
            std::cerr << "Config Error: Could not save config.txt" << std::endl;
            return;
        }

        file << "PLAY_SIDE="              << s.PLAY_SIDE              << "\n";
        file << "PLAY_MODE="              << s.PLAY_MODE              << "\n";
        file << "HIGH_SPEED="             << std::fixed << std::setprecision(1) << s.HIGH_SPEED << "\n";
        file << "GREEN_NUMBER="           << s.GREEN_NUMBER           << "\n";
        file << "VISIBLE_PX="             << s.VISIBLE_PX             << "\n";
        file << "SUDDEN_PLUS="            << s.SUDDEN_PLUS            << "\n";
        file << "LIFT="                   << s.LIFT                   << "\n";
        file << "PLAY_OPTION="            << s.PLAY_OPTION            << "\n";
        file << "GAUGE_OPTION="           << s.GAUGE_OPTION           << "\n";
        file << "ASSIST_OPTION="          << s.ASSIST_OPTION          << "\n";
        file << "EX_OPTION="              << s.EX_OPTION              << "\n";
        file << "MORE_NOTES_COUNT="       << s.MORE_NOTES_COUNT       << "\n";
        file << "LN_OPTION="              << s.LN_OPTION              << "\n";
        file << "GAUGE_DISPLAY_TYPE="     << s.GAUGE_DISPLAY_TYPE     << "\n";
        file << "DAN_GAUGE_START_PERCENT=" << s.DAN_GAUGE_START_PERCENT << "\n";
        file << "JUDGE_OFFSET="           << s.JUDGE_OFFSET           << "\n";
        file << "VISUAL_OFFSET="          << s.VISUAL_OFFSET          << "\n";
        file << "SHOW_FAST_SLOW="         << s.SHOW_FAST_SLOW         << "\n";
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

    inline void saveAsync() {
        if (!detail::isDirty.load(std::memory_order_relaxed)) return;
        detail::isDirty.store(false, std::memory_order_relaxed);
        writeSnapshot(ROOT_PATH + "config.txt", takeSnapshot());
    }

    inline void flushSave() {
        if (detail::isDirty.load(std::memory_order_relaxed)) {
            writeSnapshot(ROOT_PATH + "config.txt", takeSnapshot());
            detail::isDirty.store(false, std::memory_order_relaxed);
        }
    }

    inline void save() {
        markDirty();
        saveAsync();
    }
}
#endif




