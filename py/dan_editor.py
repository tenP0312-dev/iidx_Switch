#!/usr/bin/env python3
"""
dan_editor.py  —  段位認定コース JSON エディタ
songlist.dat を読み込んで danCourses.json を対話的に編集する

操作:
  Tab        : 左右ペイン切替
  ↑↓ / j k  : カーソル移動
  Enter      : 曲を右ペイン(コース)に追加（左ペインフォーカス時）
  D          : 曲を削除（右ペインフォーカス時）
  U / M      : 曲を上/下に移動（右ペインフォーカス時）
  1〜5       : 難易度フィルタ ANOTHER/HYPER/NORMAL/BEGINNER/INSANE トグル
  入力文字   : 検索クエリ（左ペインフォーカス時）
  BackSpace  : 検索クエリを1文字削除
  P / N      : 前/次のコースへ
  C          : コース設定メニュー
  S          : danCourses.json に保存
  ESC        : 終了
"""

import curses
import json
import os
import struct
import sys
from pathlib import Path


# ─────────────────────────────────────────────────────────────────
#  songlist.dat バイナリパーサ
# ─────────────────────────────────────────────────────────────────

def _read_str(f):
    raw = f.read(8)
    if len(raw) < 8:
        raise EOFError
    (length,) = struct.unpack('<Q', raw)
    data = f.read(length)
    if len(data) < length:
        raise EOFError
    return data.decode('utf-8', errors='replace')


def parse_songlist(dat_path):
    """songlist.dat を読み込み、曲情報のリストを返す"""
    songs = []
    try:
        with open(dat_path, 'rb') as f:
            raw = f.read(8)
            if len(raw) < 8:
                return songs
            (count,) = struct.unpack('<Q', raw)
            for _ in range(count):
                try:
                    filename  = _read_str(f)
                    title     = _read_str(f)
                    subtitle  = _read_str(f)
                    artist    = _read_str(f)
                    chartName = _read_str(f)
                    (bpm,)        = struct.unpack('<d', f.read(8))
                    (level,)      = struct.unpack('<i', f.read(4))
                    (total,)      = struct.unpack('<d', f.read(8))   # noqa
                    (totalNotes,) = struct.unpack('<i', f.read(4))
                    f.read(4)   # clearType
                    f.read(4)   # exScore
                    f.read(4)   # maxCombo
                    _read_str(f)   # rank
                    _read_str(f)   # modeHint
                    songs.append({
                        'filename':   filename,
                        'title':      title,
                        'subtitle':   subtitle,
                        'artist':     artist,
                        'chartName':  chartName,
                        'bpm':        bpm,
                        'level':      level,
                        'totalNotes': totalNotes,
                    })
                except (EOFError, struct.error):
                    break
    except OSError:
        pass
    return songs


# ─────────────────────────────────────────────────────────────────
#  danCourses.json I/O
# ─────────────────────────────────────────────────────────────────

GAUGE_TYPES = ['EASY', 'NORMAL', 'HARD', 'EX_HARD', 'DAN']


def load_courses(json_path):
    try:
        with open(json_path, encoding='utf-8') as f:
            data = json.load(f)
            return data.get('courses', [])
    except Exception:
        return []


def save_courses(json_path, courses):
    with open(json_path, 'w', encoding='utf-8') as f:
        json.dump({'courses': courses}, f, ensure_ascii=False, indent=2)


def make_course(idx):
    return {
        'id':         f'course_{idx}',
        'name':       f'コース{idx}',
        'visible':    True,
        'gaugeType':  'NORMAL',
        'startGauge': 100.0,
        'songs':      [],
    }


# ─────────────────────────────────────────────────────────────────
#  エディタ本体
# ─────────────────────────────────────────────────────────────────

DIFF_KEYS  = ['ANOTHER', 'HYPER', 'NORMAL', 'BEGINNER', 'INSANE']
DIFF_SHORT = {'ANOTHER': 'A', 'HYPER': 'H', 'NORMAL': 'N',
               'BEGINNER': 'B', 'INSANE': 'I'}

# カラーペア番号
CP_HEADER  = 1   # ヘッダ（反転シアン）
CP_SELECT  = 2   # 選択行
CP_DIM     = 3   # 薄い選択（非フォーカスペイン）
CP_OK      = 4   # 緑（保存・追加）
CP_ERR     = 5   # 赤（削除・エラー）
CP_INFO    = 6   # シアン（情報）


def _safe_addstr(win, y, x, text, attr=0):
    """画面端で例外が起きないように wrap した addstr"""
    try:
        win.addstr(y, x, text, attr)
    except curses.error:
        pass


class Editor:
    def __init__(self, stdscr, songlist_path, json_path, root_path):
        self.scr        = stdscr
        self.json_path  = json_path
        self.root_path  = root_path   # 末尾 os.sep 付き

        self.all_songs  = parse_songlist(songlist_path)
        self.courses    = load_courses(json_path)
        if not self.courses:
            self.courses.append(make_course(1))

        # UI state
        self.focus        = 0          # 0=左 1=右
        self.query        = ''
        self.diff_filter  = set(DIFF_KEYS)
        self.filtered     = []
        self.left_cur     = 0
        self.left_scroll  = 0
        self.right_cur    = 0
        self.right_scroll = 0
        self.course_idx   = 0
        self.status_msg   = ''
        self.status_type  = 'ok'       # 'ok' | 'err' | 'info'

        # bmsplayer ディレクトリ名（パス変換用）
        self.bmsplayer_name = os.path.basename(root_path.rstrip(os.sep))

        self._update_filter()

        curses.curs_set(0)
        curses.start_color()
        curses.use_default_colors()
        curses.init_pair(CP_HEADER, curses.COLOR_BLACK,  curses.COLOR_CYAN)
        curses.init_pair(CP_SELECT, curses.COLOR_BLACK,  curses.COLOR_WHITE)
        curses.init_pair(CP_DIM,    curses.COLOR_YELLOW, -1)
        curses.init_pair(CP_OK,     curses.COLOR_GREEN,  -1)
        curses.init_pair(CP_ERR,    curses.COLOR_RED,    -1)
        curses.init_pair(CP_INFO,   curses.COLOR_CYAN,   -1)

    # ──────────────────────────────────────────────
    def course(self):
        return self.courses[self.course_idx]

    def _rel_path(self, stored_path):
        """songlist.dat のパス → danCourses.json 用の ROOT_PATH 相対パス

        songlist.dat は起動ディレクトリ依存で以下の形式がありえる:
          1. 絶対パス: /Users/.../bmsplayer/BMS/...
          2. bmsplayer/ 付き相対: bmsplayer/BMS/...
          3. 既に相対: BMS/...
        """
        # 1. 絶対パスが root_path で始まる
        if stored_path.startswith(self.root_path):
            return stored_path[len(self.root_path):]
        # 2. "bmsplayer/" 等のプレフィックスが付いている
        prefix = self.bmsplayer_name + '/'
        if stored_path.startswith(prefix):
            return stored_path[len(prefix):]
        # 3. それ以外はそのまま
        return stored_path

    def _update_filter(self):
        q = self.query.lower()
        self.filtered = [
            s for s in self.all_songs
            if s['chartName'] in self.diff_filter and (
                q in s['title'].lower()
                or q in s['artist'].lower()
                or q in s['subtitle'].lower()
            )
        ]
        self.left_cur = min(self.left_cur, max(0, len(self.filtered) - 1))

    def _status(self, msg, kind='ok'):
        self.status_msg  = msg
        self.status_type = kind

    # ──────────────────────────────────────────────
    def run(self):
        while True:
            self._draw()
            key = self.scr.getch()
            if not self._handle(key):
                break

    def _handle(self, key):
        c = self.course()

        # ─── グローバルキー ───
        if key == ord('\t'):
            self.focus = 1 - self.focus
            return True

        if key == 27:   # ESC → 終了確認
            return False

        if key in (ord('s'), ord('S')):
            save_courses(self.json_path, self.courses)
            self._status(f'保存完了 → {self.json_path}', 'ok')
            return True

        if key in (ord('c'), ord('C')):
            self._course_menu()
            return True

        if key in (ord('n'), ord('N')):
            self.course_idx  = (self.course_idx + 1) % len(self.courses)
            self.right_cur   = 0
            self.right_scroll = 0
            self._status(f"コース: {self.course()['name']}", 'info')
            return True

        if key in (ord('p'), ord('P')):
            self.course_idx  = (self.course_idx - 1) % len(self.courses)
            self.right_cur   = 0
            self.right_scroll = 0
            self._status(f"コース: {self.course()['name']}", 'info')
            return True

        # ─── 左ペイン ───
        if self.focus == 0:
            if key in (curses.KEY_UP, ord('k')):
                self.left_cur = max(0, self.left_cur - 1)
            elif key in (curses.KEY_DOWN, ord('j')):
                self.left_cur = min(len(self.filtered) - 1, self.left_cur + 1)
            elif key in (ord('\n'), ord('\r'), curses.KEY_ENTER):
                if self.filtered:
                    s   = self.filtered[self.left_cur]
                    rel = self._rel_path(s['filename'])
                    c['songs'].append({'path': rel})
                    self.right_cur = len(c['songs']) - 1
                    label = s['title'] or os.path.basename(s['filename'])
                    self._status(f"追加: {label} [{s['chartName']}]", 'ok')
            elif key in (curses.KEY_BACKSPACE, 127, 8):
                self.query = self.query[:-1]
                self._update_filter()
            elif ord('1') <= key <= ord('5'):
                dk = DIFF_KEYS[key - ord('1')]
                if dk in self.diff_filter:
                    self.diff_filter.discard(dk)
                else:
                    self.diff_filter.add(dk)
                self._update_filter()
            elif 32 <= key < 127:
                self.query += chr(key)
                self._update_filter()

        # ─── 右ペイン ───
        elif self.focus == 1:
            songs = c['songs']
            if key in (curses.KEY_UP, ord('k')):
                self.right_cur = max(0, self.right_cur - 1)
            elif key in (curses.KEY_DOWN, ord('j')):
                self.right_cur = min(len(songs) - 1, self.right_cur + 1)
            elif key in (ord('d'), ord('D')):
                if songs and 0 <= self.right_cur < len(songs):
                    removed = songs.pop(self.right_cur)
                    self.right_cur = min(self.right_cur, max(0, len(songs) - 1))
                    self._status(f"削除: {os.path.basename(removed['path'])}", 'err')
            elif key in (ord('u'), ord('U')):    # 上に移動
                i = self.right_cur
                if 0 < i < len(songs):
                    songs[i], songs[i - 1] = songs[i - 1], songs[i]
                    self.right_cur -= 1
            elif key in (ord('m'), ord('M')):    # 下に移動
                i = self.right_cur
                if i < len(songs) - 1:
                    songs[i], songs[i + 1] = songs[i + 1], songs[i]
                    self.right_cur += 1

        return True

    # ──────────────────────────────────────────────
    #  コース設定メニュー
    # ──────────────────────────────────────────────
    def _course_menu(self):
        options = [
            ('名前を変更',        self._edit_name),
            ('ゲージ種別を変更',  self._edit_gauge),
            ('visible 切替',      self._toggle_visible),
            ('startGauge 変更',   self._edit_start_gauge),
            ('新規コース追加',    self._add_course),
            ('このコースを削除',  self._delete_course),
            ('< キャンセル >',    None),
        ]
        cur = 0
        mw  = 28
        mh  = len(options) + 4

        while True:
            H, W = self.scr.getmaxyx()
            win = curses.newwin(mh, mw, max(0, (H - mh) // 2), max(0, (W - mw) // 2))
            win.keypad(True)
            win.border()
            _safe_addstr(win, 0, 2, ' コース設定 ', curses.color_pair(CP_HEADER))
            for i, (label, _) in enumerate(options):
                attr = curses.color_pair(CP_SELECT) if i == cur else 0
                _safe_addstr(win, i + 2, 2, label.ljust(mw - 4), attr)
            win.refresh()

            k = win.getch()
            if k == curses.KEY_UP and cur > 0:
                cur -= 1
            elif k == curses.KEY_DOWN and cur < len(options) - 1:
                cur += 1
            elif k in (ord('\n'), ord('\r'), curses.KEY_ENTER):
                fn = options[cur][1]
                del win
                self.scr.touchwin()
                if fn is not None:
                    fn()
                break
            elif k == 27:
                del win
                self.scr.touchwin()
                break

    # ──────────────────────────────────────────────
    #  ゲージ種別選択
    # ──────────────────────────────────────────────
    def _edit_gauge(self):
        c   = self.course()
        cur = GAUGE_TYPES.index(c['gaugeType']) if c['gaugeType'] in GAUGE_TYPES else 1
        mw  = 22
        mh  = len(GAUGE_TYPES) + 4

        while True:
            H, W = self.scr.getmaxyx()
            win = curses.newwin(mh, mw, max(0, (H - mh) // 2), max(0, (W - mw) // 2))
            win.keypad(True)
            win.border()
            _safe_addstr(win, 0, 2, ' ゲージ種別 ', curses.color_pair(CP_HEADER))
            for i, g in enumerate(GAUGE_TYPES):
                attr = curses.color_pair(CP_SELECT) if i == cur else 0
                _safe_addstr(win, i + 2, 2, g.ljust(mw - 4), attr)
            win.refresh()

            k = win.getch()
            if k == curses.KEY_UP and cur > 0:
                cur -= 1
            elif k == curses.KEY_DOWN and cur < len(GAUGE_TYPES) - 1:
                cur += 1
            elif k in (ord('\n'), ord('\r'), curses.KEY_ENTER):
                c['gaugeType'] = GAUGE_TYPES[cur]
                self._status(f"ゲージ: {GAUGE_TYPES[cur]}", 'info')
                del win; self.scr.touchwin()
                break
            elif k == 27:
                del win; self.scr.touchwin()
                break

    def _edit_name(self):
        c = self.course()
        v = self._input_dialog('コース名:', c['name'])
        if v is not None:
            c['name'] = v
            self._status(f'名前変更: {v}', 'info')

    def _toggle_visible(self):
        c = self.course()
        c['visible'] = not c['visible']
        self._status(f"visible: {c['visible']}", 'info')

    def _edit_start_gauge(self):
        c = self.course()
        v = self._input_dialog('startGauge (0〜100):', str(c.get('startGauge', 100.0)))
        if v is not None:
            try:
                c['startGauge'] = max(0.0, min(100.0, float(v)))
                self._status(f"startGauge: {c['startGauge']:.1f}%", 'info')
            except ValueError:
                self._status('無効な数値です', 'err')

    def _add_course(self):
        idx = len(self.courses) + 1
        self.courses.append(make_course(idx))
        self.course_idx  = len(self.courses) - 1
        self.right_cur   = 0
        self.right_scroll = 0
        self._status(f"新規コース追加: {self.course()['name']}", 'ok')

    def _delete_course(self):
        if len(self.courses) <= 1:
            self._status('コースが1つしかないので削除できません', 'err')
            return
        self.courses.pop(self.course_idx)
        self.course_idx  = max(0, self.course_idx - 1)
        self.right_cur   = 0
        self.right_scroll = 0
        self._status('コースを削除しました', 'err')

    # ──────────────────────────────────────────────
    #  文字入力ダイアログ
    # ──────────────────────────────────────────────
    def _input_dialog(self, label, default=''):
        buf = list(default)
        mw  = max(len(label) + 24, 44)
        mh  = 5
        curses.curs_set(1)

        while True:
            H, W = self.scr.getmaxyx()
            win = curses.newwin(mh, mw, max(0, (H - mh) // 2), max(0, (W - mw) // 2))
            win.keypad(True)
            win.border()
            _safe_addstr(win, 1, 2, label)
            text = ''.join(buf)
            disp = text[-(mw - 6):]
            _safe_addstr(win, 2, 2, disp.ljust(mw - 4))
            try:
                win.move(2, 2 + len(disp))
            except curses.error:
                pass
            win.refresh()

            k = win.getch()
            if k in (ord('\n'), ord('\r'), curses.KEY_ENTER):
                curses.curs_set(0)
                del win; self.scr.touchwin()
                return ''.join(buf)
            elif k == 27:
                curses.curs_set(0)
                del win; self.scr.touchwin()
                return None
            elif k in (curses.KEY_BACKSPACE, 127, 8):
                if buf:
                    buf.pop()
            elif 32 <= k < 127:
                buf.append(chr(k))

    # ──────────────────────────────────────────────
    #  描画
    # ──────────────────────────────────────────────
    def _draw(self):
        H, W = self.scr.getmaxyx()
        self.scr.erase()
        mid = W // 2

        self._draw_left(H, mid)
        self._draw_right(H, W, mid)
        self._draw_status(H, W)
        self._draw_help(H, W)

        self.scr.refresh()

    # ─── 左ペイン（検索・曲リスト） ───
    def _draw_left(self, H, mid):
        w = mid
        # ヘッダ
        left_active = (self.focus == 0)
        title = f" 曲リスト ({len(self.filtered)}/{len(self.all_songs)}件) "
        _safe_addstr(self.scr, 0, 0,
                     title[:w].ljust(w),
                     curses.color_pair(CP_HEADER) | (curses.A_BOLD if left_active else 0))

        # 検索バー
        bar = f" 検索> {self.query}"
        if left_active:
            bar += '_'
        _safe_addstr(self.scr, 1, 0, bar[:w].ljust(w))

        # 難易度フィルタ
        fline = ' '
        for i, dk in enumerate(DIFF_KEYS):
            s = DIFF_SHORT[dk]
            if dk in self.diff_filter:
                fline += f'[{i+1}:{s}] '
            else:
                fline += f'({i+1}:{s}) '
        _safe_addstr(self.scr, 2, 0, fline[:w].ljust(w), curses.color_pair(CP_INFO))

        # 区切り線
        _safe_addstr(self.scr, 3, 0, '─' * (w - 1))

        # 曲リスト
        list_y = 4
        list_h = max(0, H - list_y - 2)   # ステータス2行分
        if list_h <= 0:
            return

        # スクロール調整（left_scroll を常に安全な範囲に丸める）
        n = len(self.filtered)
        if n == 0:
            self.left_scroll = 0
        else:
            self.left_scroll = max(0, min(self.left_scroll, n - 1))
            if self.left_cur < self.left_scroll:
                self.left_scroll = self.left_cur
            if self.left_cur >= self.left_scroll + list_h:
                self.left_scroll = max(0, self.left_cur - list_h + 1)

        # スライスで表示範囲を確定してから描画（index 範囲外を完全排除）
        visible = self.filtered[self.left_scroll: self.left_scroll + list_h]
        for i, s in enumerate(visible):
            ry  = list_y + i
            idx = self.left_scroll + i
            if ry >= H - 2:
                break

            title_str = (s['title'] or os.path.basename(s['filename']))[:16].ljust(16)
            diff_str  = s['chartName'][:3].ljust(3)
            lv_str    = f"Lv{s['level']:2d}"
            bpm_str   = f"{int(s['bpm']):3d}bpm"
            line      = f" {title_str} {diff_str} {lv_str} {bpm_str}"
            line      = line[:w - 1].ljust(w - 1)

            if idx == self.left_cur:
                attr = curses.color_pair(CP_SELECT) if left_active else curses.color_pair(CP_DIM)
            else:
                attr = 0
            _safe_addstr(self.scr, ry, 0, line, attr)

    # ─── 右ペイン（コース曲リスト） ───
    def _draw_right(self, H, W, mid):
        w = W - mid
        c = self.course()
        right_active = (self.focus == 1)

        # ヘッダ（コース名・ゲージ・visible）
        vis   = '表示' if c.get('visible', True) else '非表示'
        sg    = c.get('startGauge', 100.0)
        hdr   = f" {c['name']} [{c['gaugeType']}] {vis} {sg:.0f}% "
        _safe_addstr(self.scr, 0, mid,
                     hdr[:w].ljust(w),
                     curses.color_pair(CP_HEADER) | (curses.A_BOLD if right_active else 0))

        # コースナビ
        nav = f" ◀P  {self.course_idx + 1}/{len(self.courses)}  N▶  曲数:{len(c['songs'])}"
        _safe_addstr(self.scr, 1, mid, nav[:w].ljust(w), curses.color_pair(CP_INFO))

        # id表示
        id_line = f" id: {c.get('id', '')} "
        _safe_addstr(self.scr, 2, mid, id_line[:w].ljust(w))

        # 区切り線
        _safe_addstr(self.scr, 3, mid, '─' * (w - 1))

        # 曲リスト
        songs  = c['songs']
        list_y = 4
        list_h = max(0, H - list_y - 2)

        if not songs:
            _safe_addstr(self.scr, list_y, mid, ' （曲なし）', curses.color_pair(CP_DIM))
        else:
            ns = len(songs)
            self.right_scroll = max(0, min(self.right_scroll, ns - 1))
            if self.right_cur < self.right_scroll:
                self.right_scroll = self.right_cur
            if self.right_cur >= self.right_scroll + list_h:
                self.right_scroll = max(0, self.right_cur - list_h + 1)

        visible = songs[self.right_scroll: self.right_scroll + list_h]
        for i, song in enumerate(visible):
            ry  = list_y + i
            idx = self.right_scroll + i
            if ry >= H - 2:
                break
            p    = song.get('path', '')
            name = os.path.basename(p)
            line = f" {idx+1:2d}. {name}"
            line = line[:w - 1].ljust(w - 1)

            if idx == self.right_cur:
                attr = curses.color_pair(CP_SELECT) if right_active else curses.color_pair(CP_DIM)
            else:
                attr = 0
            _safe_addstr(self.scr, ry, mid, line, attr)

    # ─── ステータスバー ───
    def _draw_status(self, H, W):
        msg  = self.status_msg or ''
        kind = self.status_type
        attr = {
            'ok':   curses.color_pair(CP_OK),
            'err':  curses.color_pair(CP_ERR),
            'info': curses.color_pair(CP_INFO),
        }.get(kind, 0)
        _safe_addstr(self.scr, H - 2, 0, msg[:W - 1].ljust(W - 1), attr)

    # ─── ヘルプバー ───
    def _draw_help(self, H, W):
        if self.focus == 0:
            help_t = ' Tab:切替  ↑↓:移動  Enter:追加  1-5:難易度  文字:検索  BS:削除  P/N:コース  C:設定  S:保存  ESC:終了'
        else:
            help_t = ' Tab:切替  ↑↓:移動  D:削除  U:↑移動  M:↓移動  P/N:コース  C:設定  S:保存  ESC:終了'
        _safe_addstr(self.scr, H - 1, 0, help_t[:W - 1].ljust(W - 1),
                     curses.color_pair(CP_HEADER))


# ─────────────────────────────────────────────────────────────────
#  エントリポイント
# ─────────────────────────────────────────────────────────────────

def main():
    script_dir    = Path(__file__).parent
    bmsplayer_dir = script_dir / 'bmsplayer'
    songlist_path = bmsplayer_dir / 'songlist.dat'
    json_path     = bmsplayer_dir / 'danCourses.json'
    root_path     = str(bmsplayer_dir) + os.sep

    if not songlist_path.exists():
        print(f'エラー: songlist.dat が見つかりません\n  {songlist_path}', file=sys.stderr)
        print('先に BMS プレイヤーを起動してキャッシュを生成してください。', file=sys.stderr)
        sys.exit(1)

    def _curses_main(stdscr):
        stdscr.keypad(True)
        editor = Editor(stdscr, str(songlist_path), str(json_path), root_path)
        editor.run()

    curses.wrapper(_curses_main)
    print('dan_editor 終了。danCourses.json を確認してください。')


if __name__ == '__main__':
    main()
