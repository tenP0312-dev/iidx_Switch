#ifndef SCENE2PDIFFSELECT_HPP
#define SCENE2PDIFFSELECT_HPP

#include <string>
#include <vector>
#include <SDL2/SDL.h>
#include "NoteRenderer.hpp"
#include "SceneSelect.hpp"  // SongEntry, SongGroup

// ============================================================
//  Scene2PDiffSelect
//  1Pが選曲確定後、2Pが同じ曲グループの難易度を選ぶ画面。
//  ・左半分: 1Pの選択難易度（固定表示）
//  ・右半分: 2Pのカーソル（SYS_BTN_LEFT/RIGHT または BTN_LANE8_A/B で移動）
//  ・SYS_BTN_DECIDE: 確定 → 2Pパスを返す
//  ・SYS_BTN_BACK:   キャンセル → 空文字を返す
//  ・3秒カウントダウン（入力あるたびリセット）: タイムアウトで確定
// ============================================================
class Scene2PDiffSelect {
public:
    // 戻り値: 2Pが選んだ譜面のフルパス。キャンセル時は空文字。
    std::string run(SDL_Renderer* ren, NoteRenderer& renderer,
                    const std::vector<SongEntry>& songCache,
                    const SongGroup& group,
                    int p1DiffIdx);
};

#endif // SCENE2PDIFFSELECT_HPP
