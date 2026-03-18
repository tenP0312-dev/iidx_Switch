#include "ScoreManager.hpp"
#include "Config.hpp"
#include <fstream>
#include <sys/stat.h>
#include <algorithm>
#include <map>
#include <iomanip>
#include <sstream>

#ifdef __SWITCH__
#include <switch.h>
#endif

// 静的メンバ変数の実体定義
std::map<std::string, BestScore> ScoreManager::scoreCache;

int ScoreManager::calculateExScore(int pg, int gr) {
    return (pg * 2) + (gr * 1);
}

std::string ScoreManager::calculateRank(int exScore, int totalNotes) {
    if (totalNotes <= 0) return "F";
    double rate = (double)exScore / (totalNotes * 2);

    if (rate >= 8.0 / 9.0) return "AAA";
    if (rate >= 7.0 / 9.0) return "AA";
    if (rate >= 6.0 / 9.0) return "A";
    if (rate >= 5.0 / 9.0) return "B";
    if (rate >= 4.0 / 9.0) return "C";
    if (rate >= 3.0 / 9.0) return "D";
    if (rate >= 2.0 / 9.0) return "E";
    return "F";
}

std::string ScoreManager::generateUniqueId(const std::string& title, const std::string& chartName, int totalNotes) {
    std::string diff = chartName.empty() ? "NoName" : chartName;
    return title + "_" + diff + "_" + std::to_string(totalNotes);
}

// --- 【追加】Switch/OS互換性のためのファイル名ハッシュ化関数 ---
std::string ScoreManager::convertToHash(const std::string& input) {
    std::hash<std::string> hasher;
    size_t hashValue = hasher(input);
    std::stringstream ss;
    // 16進数文字列に変換することで、ファイル名を常に英数字のみにする
    ss << std::hex << std::setw(16) << std::setfill('0') << hashValue;
    return ss.str();
}

std::string ScoreManager::getSavePath(const std::string& uniqueId) {
    std::string dir = Config::SCORE_PATH;
    
#ifdef __SWITCH__
    mkdir(Config::ROOT_PATH.c_str(), 0777);
#endif
    mkdir(dir.c_str(), 0777);

    // 【修正箇所】日本語を避け、ハッシュ値をファイル名にする
    std::string safeFileName = convertToHash(uniqueId);

    // プレフィックス 's_' を付けて拡張子を指定
    return dir + "s_" + safeFileName + ".dat";
}

BestScore ScoreManager::loadScore(const std::string& title, const std::string& chartName, int totalNotes) {
    // --- 【継承】キャッシュチェックロジック ---
    std::string uniqueId = generateUniqueId(title, chartName, totalNotes);
    if (scoreCache.count(uniqueId)) {
        return scoreCache[uniqueId];
    }
    // ----------------------------------------

    BestScore best;
    best.pGreat = best.great = best.good = best.bad = best.poor = 0;
    best.fastCount = best.slowCount = 0; 
    best.maxCombo = 0;
    best.exScore = 0;
    best.rank = "F";
    best.clearType = ClearType::NO_PLAY;
    best.isClear = false;
    best.gaugeHistory.clear(); // 明示的にクリア

    // getSavePath 内部でハッシュ化されたパスが返される
    std::string path = getSavePath(uniqueId);
    std::ifstream ifs(path);
    if (!ifs) {
        scoreCache[uniqueId] = best; 
        return best;
    }

    int clearTypeInt = 0;
    if (!(ifs >> best.pGreat >> best.great >> best.good >> best.bad 
             >> best.poor >> best.maxCombo >> best.exScore >> best.rank >> clearTypeInt)) {
        scoreCache[uniqueId] = best;
        return best;
    }
    
    ifs >> best.fastCount >> best.slowCount;

    best.clearType = static_cast<ClearType>(clearTypeInt);
    best.isClear = (best.clearType >= ClearType::DAN_CLEAR);

    // --- 【追加】ゲージ推移データの読み込み ---
    size_t historySize = 0;
    if (ifs >> historySize) {
        best.gaugeHistory.resize(historySize);
        for (size_t i = 0; i < historySize; ++i) {
            ifs >> best.gaugeHistory[i];
        }
    }

    // 【継承】読み込んだ結果をキャッシュに保存
    scoreCache[uniqueId] = best;
    return best;
}

void ScoreManager::saveIfBest(const std::string& title, const std::string& chartName, int totalNotes, const PlayStatus& status) {
    if (Config::ASSIST_OPTION == 7) {
        return;
    }

    // MORE NOTES (EX_OPTION==2) はノーツ数が変わるため、スコア・コンボは参考値にならない。
    // クリアランプのみ保存し、スコア・コンボ更新は行わない。
    const bool isMoreNotes = (Config::EX_OPTION == 2);

    BestScore currentBest = loadScore(title, chartName, totalNotes);
    int currentExScore = calculateExScore(status.pGreatCount, status.greatCount);

    ClearType currentType = status.clearType;

    bool scoreUpdated = !isMoreNotes && (currentExScore > currentBest.exScore);
    // AUTO PLAY (ASSIST_OPTION==7) はsaveIfBest冒頭でreturn済み。
    // クリアランプ更新: AUTO PLAYを除く全ASSISTはASSIST_CLEARでのみ更新可能。
    bool lampUpdated  = (static_cast<int>(currentType) > static_cast<int>(currentBest.clearType));
    bool comboUpdated = !isMoreNotes && (status.maxCombo > currentBest.maxCombo);

    // スコア、ランプ、コンボのいずれかが更新されたら保存
    if (scoreUpdated || lampUpdated || comboUpdated) {
        std::string uniqueId = generateUniqueId(title, chartName, totalNotes);
        std::string path = getSavePath(uniqueId);
        std::ofstream ofs(path);
        if (!ofs) return;

        std::string rank = calculateRank(currentExScore, status.totalNotes);

        // 新しいスコアデータを作成
        BestScore newBest;
        newBest.pGreat = (scoreUpdated ? status.pGreatCount : currentBest.pGreat);
        newBest.great  = (scoreUpdated ? status.greatCount  : currentBest.great);
        newBest.good   = (scoreUpdated ? status.goodCount   : currentBest.good);
        newBest.bad    = (scoreUpdated ? status.badCount    : currentBest.bad);
        newBest.poor   = (scoreUpdated ? status.poorCount   : currentBest.poor);
        newBest.fastCount = (scoreUpdated ? status.fastCount : currentBest.fastCount); 
        newBest.slowCount = (scoreUpdated ? status.slowCount : currentBest.slowCount); 
        newBest.maxCombo = (comboUpdated ? status.maxCombo  : currentBest.maxCombo);
        newBest.exScore  = (scoreUpdated ? currentExScore   : currentBest.exScore);
        newBest.rank     = (scoreUpdated ? rank             : currentBest.rank);
        newBest.clearType = (lampUpdated ? currentType      : currentBest.clearType);
        newBest.isClear   = (newBest.clearType >= ClearType::DAN_CLEAR);

        // ゲージ推移: スコア更新時のみ保存（MORE NOTES時はスコア非更新なので継承）
        newBest.gaugeHistory = (scoreUpdated ? status.gaugeHistory : currentBest.gaugeHistory);

        // ファイル書き込み (継承ロジック)
        ofs << newBest.pGreat << " " << newBest.great << " " << newBest.good << " "
            << newBest.bad << " " << newBest.poor << " " << newBest.maxCombo << " "
            << newBest.exScore << " " << newBest.rank << " " << static_cast<int>(newBest.clearType) << " "
            << newBest.fastCount << " " << newBest.slowCount;

        // --- 【追加】ゲージ推移データの書き込み (要素数 -> 中身) ---
        ofs << " " << newBest.gaugeHistory.size();
        for (float val : newBest.gaugeHistory) {
            ofs << " " << val;
        }

        // 【継承】保存と同時にキャッシュも更新
        scoreCache[uniqueId] = newBest;
    }
}





