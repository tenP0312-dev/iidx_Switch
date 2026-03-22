# -*- coding: utf-8 -*-
"""
BOXWAV Packer  (unified edition v2)
python3 boxwav_packer.py

────────────────────────────────────────────────────────────────
モード一覧
────────────────────────────────────────────────────────────────
  1) bmson → boxwav
  2) BMS   → boxwav  (.bms / .bme / .bml)
  3) すべて → boxwav  (bmson + BMS 混在フォルダ対応)      ★新規
  4) WAV 変換のみ     (22050Hz / mono / 8bit に一括変換)  ★新規
  5) WAV 変換 + boxwav 化  【全自動】                     ★新規
     フォーマット確認 → 必要なものだけ変換 → boxwav 化 → WAV 削除まで一括
  6) 動画変換         (Switch 向け 256px / 30fps / H.264)

コマンドライン:
  --bmson / --bms / --all / --wav / --wav-pack / --video  PATH...
  --auto PATH...   : モード 5 を全確認スキップで完全無人実行

────────────────────────────────────────────────────────────────
WAV 変換目標フォーマット
────────────────────────────────────────────────────────────────
  サンプルレート : 22050 Hz
  チャンネル数  : 1  (mono)
  ビット深度    : 8bit unsigned PCM  (pcm_u8)

  この 3 条件を全て満たす WAV は変換をスキップする (ゼロコスト)。

────────────────────────────────────────────────────────────────
boxwav フォーマット
────────────────────────────────────────────────────────────────
  通常 (entry_count < 0xFFFFFFFE):
    [entry_count: uint32_le][dummy: 8 bytes]
    [name:32][size:4][data] × entry_count

  REDIRECT (0xFFFFFFFF):
    [FF FF FF FF][target_name:32][dummy:4]

  REDIRECT+EXTRA (0xFFFFFFFE):
    [FE FF FF FF][target_name:32][extra_count:4]
    [name:32][size:4][data] × extra_count
"""

import os, sys, re, json, struct, threading, subprocess, tempfile, wave, shlex
from collections import defaultdict
from concurrent.futures import ThreadPoolExecutor, as_completed
from datetime import datetime

# ─── 定数 ───────────────────────────────────────────────────────────────────
MAX_BOXWAV_SIZE      = 10 * 1024 * 1024
TARGET_EXT           = ".boxwav"
MAGIC_REDIRECT       = 0xFFFFFFFF
MAGIC_REDIRECT_EXTRA = 0xFFFFFFFE

BMSON_EXTENSIONS = {".bmson"}
BMS_EXTENSIONS   = {".bms", ".bme", ".bml"}
ALL_CHART_EXTS   = BMSON_EXTENSIONS | BMS_EXTENSIONS
VIDEO_EXTENSIONS = {".mp4", ".wmv", ".mpeg", ".mpg", ".mov", ".mkv"}

# 変換対象サウンド拡張子（WAV以外も変換→WAVに統一してboxwavに格納）
SOUND_EXTENSIONS = {".wav", ".ogg", ".mp3", ".flac", ".opus", ".aif", ".aiff"}

# WAV 変換目標
WAV_TARGET_SR   = 22050
WAV_TARGET_CH   = 1
WAV_TARGET_BITS = 8

# 並列数
WAV_WORKERS  = max(1, os.cpu_count() or 4)   # ffmpeg は CPU バウンド
PACK_WORKERS = 4

_log_lock   = threading.Lock()
_print_lock = threading.Lock()


# ═══════════════════════════════════════════════════════════════════════════════
#  ログ / 出力
# ═══════════════════════════════════════════════════════════════════════════════
_LOG_FILE = "log_packer.txt"

def _set_log_file(path: str):
    global _LOG_FILE
    _LOG_FILE = path

def log_error(msg: str):
    with _log_lock:
        with open(_LOG_FILE, "a", encoding="utf-8") as f:
            f.write(f"[{datetime.now():%Y-%m-%d %H:%M:%S}] {msg}\n")

def log_info(msg: str):
    with _log_lock:
        with open(_LOG_FILE, "a", encoding="utf-8") as f:
            f.write(f"[{datetime.now():%Y-%m-%d %H:%M:%S}] [INFO] {msg}\n")

def safe_print(msg: str):
    with _print_lock:
        print(msg)


# ═══════════════════════════════════════════════════════════════════════════════
#  WAV フォーマット検査 & 変換
# ═══════════════════════════════════════════════════════════════════════════════

def wav_needs_convert(path: str) -> tuple[bool, str]:
    """
    変換不要なら (False, "")、必要なら (True, 理由) を返す。
    WAV以外の拡張子は常に変換対象。
    """
    ext = os.path.splitext(path)[1].lower()
    if ext != ".wav":
        return True, f"{ext} → WAV変換必要"

    try:
        with wave.open(path, "rb") as w:
            sr   = w.getframerate()
            ch   = w.getnchannels()
            bits = w.getsampwidth() * 8
    except Exception as e:
        return True, f"読み取り不可 ({e})"

    reasons = []
    if sr   != WAV_TARGET_SR:   reasons.append(f"SR {sr}→{WAV_TARGET_SR}")
    if ch   != WAV_TARGET_CH:   reasons.append(f"ch {ch}→{WAV_TARGET_CH}")
    if bits != WAV_TARGET_BITS: reasons.append(f"{bits}bit→{WAV_TARGET_BITS}bit")
    return bool(reasons), ", ".join(reasons)


def convert_wav_inplace(path: str) -> tuple[bool, str]:
    """
    path を目標フォーマット(22050Hz/mono/8bit WAV)に変換。
    - 入力が .wav の場合: その場に上書き
    - 入力が .ogg/.mp3 等の場合: 同名の .wav を生成し元ファイルを削除
    戻り値: (成功フラグ, 出力パス or エラーメッセージ)
    """
    ext = os.path.splitext(path)[1].lower()
    is_wav_input = (ext == ".wav")

    # 出力先: 常に .wav（同ディレクトリ・同ステム）
    out_path = os.path.splitext(path)[0] + ".wav"
    fd, tmp = tempfile.mkstemp(suffix=".wav", dir=os.path.dirname(path))
    os.close(fd)
    try:
        r = subprocess.run(
            ["ffmpeg", "-y", "-i", path,
             "-ar", str(WAV_TARGET_SR),
             "-ac", str(WAV_TARGET_CH),
             "-acodec", "pcm_u8",
             "-map_metadata", "-1",
             tmp],
            capture_output=True)
        if r.returncode != 0:
            os.remove(tmp)
            return False, r.stderr.decode(errors="replace")[-300:]
        os.replace(tmp, out_path)
        # 非WAV入力の場合は元ファイルを削除
        if not is_wav_input and os.path.abspath(path) != os.path.abspath(out_path):
            try:
                os.remove(path)
            except Exception:
                pass
        return True, out_path
    except Exception as e:
        if os.path.exists(tmp): os.remove(tmp)
        return False, str(e)


def scan_and_convert_wavs(wav_paths: list[str],
                           workers: int = WAV_WORKERS) -> tuple[int, int, int]:
    """
    必要な WAV だけ並列変換。
    Returns: (already_ok, converted, failed)
    """
    needs: list[tuple[str, str]] = []
    already_ok = 0

    for p in wav_paths:
        need, reason = wav_needs_convert(p)
        if need:
            needs.append((p, reason))
        else:
            already_ok += 1

    if not needs:
        return already_ok, 0, 0

    total = len(needs)
    safe_print(f"\n  🔄 変換対象: {total} 件 / スキップ (既に OK): {already_ok} 件")
    safe_print(f"  並列数: {workers}")

    converted = failed = 0
    counter_lock = threading.Lock()

    def do_one(item: tuple[str, str]):
        nonlocal converted, failed
        p, reason = item
        ok, result = convert_wav_inplace(p)
        with counter_lock:
            if ok:
                converted += 1
                done = converted + failed
                if done % 100 == 0 or done == total:
                    safe_print(f"  [{done}/{total}] 変換中...")
            else:
                failed += 1
                safe_print(f"  ❌ 変換失敗: {os.path.basename(p)}  ({result[:80]})")
                log_error(f"audio convert failed: {p} | {result}")

    with ThreadPoolExecutor(max_workers=workers) as ex:
        list(ex.map(do_one, needs))

    safe_print(f"  ✅ 変換完了: {converted} 件 / ❌ 失敗: {failed} 件")
    return already_ok, converted, failed


# ═══════════════════════════════════════════════════════════════════════════════
#  ファイル収集 & グループ化
# ═══════════════════════════════════════════════════════════════════════════════

def collect_chart_files(paths: list, exts: set) -> list:
    result = []
    for path in paths:
        path = path.strip().strip('"').strip("'")
        if os.path.isdir(path):
            for root, _, files in os.walk(path):
                for f in files:
                    if os.path.splitext(f)[1].lower() in exts:
                        result.append(os.path.join(root, f))
        elif os.path.isfile(path) and os.path.splitext(path)[1].lower() in exts:
            result.append(path)
        elif path:
            print(f"  ⚠ 無視: {path}")
    return result

def group_by_dir(chart_files: list) -> dict:
    groups = defaultdict(list)
    for path in chart_files:
        groups[os.path.dirname(os.path.abspath(path))].append(path)
    return dict(groups)


# ═══════════════════════════════════════════════════════════════════════════════
#  チャートから WAV リストを抽出
# ═══════════════════════════════════════════════════════════════════════════════

def collect_wav_names_from_bmson(path: str) -> tuple[list, str]:
    with open(path, "r", encoding="utf-8-sig") as f:
        data = json.load(f)
    preview = data.get("info", {}).get("preview_music", "").strip()
    names = list(dict.fromkeys(
        ch["name"] for ch in data.get("sound_channels", [])
        if ch.get("name") and ch["name"] != preview))
    return names, preview

_WAV_RE = re.compile(r"^#WAV[0-9A-Za-z]{2}\s+(.+)", re.IGNORECASE | re.MULTILINE)

def read_bms_text(path: str) -> str:
    for enc in ("utf-8-sig", "shift-jis", "latin-1"):
        try:
            with open(path, "r", encoding=enc) as f:
                return f.read()
        except (UnicodeDecodeError, LookupError):
            pass
    return ""

def collect_wav_names_from_bms(path: str) -> tuple[list, str]:
    text = read_bms_text(path)
    seen: dict = {}
    for m in _WAV_RE.finditer(text):
        name = m.group(1).strip().strip('"').strip("'")
        if name and name not in seen:
            seen[name] = None
    return list(seen.keys()), ""

def collect_wav_names(path: str) -> tuple[list, str]:
    ext = os.path.splitext(path)[1].lower()
    if ext in BMSON_EXTENSIONS:  return collect_wav_names_from_bmson(path)
    if ext in BMS_EXTENSIONS:    return collect_wav_names_from_bms(path)
    return [], ""


# ═══════════════════════════════════════════════════════════════════════════════
#  boxwav 書き込み
# ═══════════════════════════════════════════════════════════════════════════════

def encode_name_32(name: str) -> bytes:
    enc = name.encode("utf-8")
    while len(enc) > 31:
        name = name[:-1]
        enc  = name.encode("utf-8")
    return enc.ljust(32, b"\x00")


def write_wav_entries(wav_names: list, base_dir: str,
                      out_dir: str, base_name: str) -> tuple:
    """Returns: (part_count, packed_wav_paths, bytes_written)"""
    created_parts: list = []
    part_idx = entry_count = total_written = bytes_written = 0
    packed_wavs: list = []

    def open_part():
        nonlocal part_idx
        part_idx += 1
        suffix = "" if part_idx == 1 else str(part_idx)
        p  = os.path.join(out_dir, f"{base_name}{suffix}{TARGET_EXT}")
        fh = open(p, "wb")
        fh.write(b"\x00" * 12)   # ヘッダー予約
        created_parts.append(p)
        return fh

    def read_as_wav_bytes(wav_path: str) -> bytes:
        """非WAVファイルはffmpegでメモリ上にWAVデコード、WAVはそのまま読む"""
        ext = os.path.splitext(wav_path)[1].lower()
        if ext != ".wav":
            r = subprocess.run(
                ["ffmpeg", "-y", "-i", wav_path,
                 "-ar", str(WAV_TARGET_SR),
                 "-ac", str(WAV_TARGET_CH),
                 "-acodec", "pcm_u8",
                 "-map_metadata", "-1",
                 "-f", "wav", "pipe:1"],
                capture_output=True)
            if r.returncode == 0 and r.stdout:
                return r.stdout
            # 変換失敗時は元ファイルをそのまま返す（フォールバック）
        with open(wav_path, "rb") as f:
            return f.read()

    fout = open_part()
    n = len(wav_names)

    for i, wav_name in enumerate(wav_names, 1):
        # 実ファイルパスをステムフォールバックで解決
        direct_path = os.path.join(base_dir, wav_name)
        if os.path.exists(direct_path):
            wav_path = direct_path
        else:
            stem = os.path.splitext(direct_path)[0]
            wav_path = direct_path  # デフォルト
            for ext in SOUND_EXTENSIONS:
                alt = stem + ext
                if os.path.exists(alt):
                    wav_path = alt
                    break
        wav_data = read_as_wav_bytes(wav_path)
        actual_size = len(wav_data)
        item_size   = 32 + 4 + actual_size

        if entry_count > 0 and total_written + item_size > MAX_BOXWAV_SIZE:
            fout.seek(0); fout.write(struct.pack("<I", entry_count)); fout.close()
            safe_print(f"  └ パート {part_idx} ({total_written/1024/1024:.1f} MB, {entry_count} 件)")
            entry_count = total_written = 0
            fout = open_part()

        fout.write(encode_name_32(wav_name))
        fout.write(struct.pack("<I", actual_size))
        fout.write(wav_data)

        entry_count   += 1
        total_written += item_size
        bytes_written += item_size
        packed_wavs.append(wav_path)

        if i % 5 == 0 or i == n:
            pct = int(i / n * 100)
            bar = "█" * (pct // 5) + "-" * (20 - pct // 5)
            with _print_lock:
                sys.stdout.write(f"\r  └ [{bar}] {pct:3d}% ({i}/{n})")
                sys.stdout.flush()

    print()
    if entry_count > 0:
        fout.seek(0); fout.write(struct.pack("<I", entry_count)); fout.close()
        safe_print(f"  └ パート {part_idx} ({total_written/1024/1024:.1f} MB, {entry_count} 件)")
    else:
        fout.close()
        os.remove(created_parts[-1]); created_parts.pop()
        part_idx -= 1

    return part_idx, packed_wavs, bytes_written


def write_redirect(out_path: str, target: str):
    with open(out_path, "wb") as f:
        f.write(struct.pack("<I", MAGIC_REDIRECT))
        f.write(encode_name_32(target))
        f.write(b"\x00" * 4)

def write_redirect_extra(out_path: str, target: str,
                         extra_wavs: list, base_dir: str):
    with open(out_path, "wb") as f:
        f.write(struct.pack("<I", MAGIC_REDIRECT_EXTRA))
        f.write(encode_name_32(target))
        f.write(struct.pack("<I", len(extra_wavs)))
        for wav_name in extra_wavs:
            data = open(os.path.join(base_dir, wav_name), "rb").read()
            f.write(encode_name_32(wav_name))
            f.write(struct.pack("<I", len(data)))
            f.write(data)


# ═══════════════════════════════════════════════════════════════════════════════
#  フォルダグループのパック (bmson / BMS 共通)
# ═══════════════════════════════════════════════════════════════════════════════

def pack_folder_group(chart_paths: list, output_dir,
                      idx_start: int, total: int) -> tuple:
    """Returns: (success_count, packed_wavs, bytes_written, failed_names)"""
    base_dir = os.path.dirname(os.path.abspath(chart_paths[0]))
    out_dir  = output_dir if output_dir else base_dir
    os.makedirs(out_dir, exist_ok=True)

    wav_orders: dict = {}   # path -> [実ファイル名, ...]
    wav_sets:   dict = {}   # path -> {ステム(拡張子なし小文字), ...}
    for path in chart_paths:
        try:
            names, _ = collect_wav_names(path)
            wav_orders[path] = names
            # 積集合の計算はステムで行う（.wav/.ogg混在でも一致させる）
            wav_sets[path]   = {os.path.splitext(n)[0].lower() for n in names}
        except Exception as e:
            log_error(f"チャート読み込みエラー ({os.path.basename(path)}): {e}")
            wav_orders[path] = []; wav_sets[path] = set()

    all_sets   = [s for s in wav_sets.values() if s]
    # common_setはステムの積集合
    common_set = set.intersection(*all_sets) if len(all_sets) > 1 else set()

    def resolve_wav_path(w):
        """BMSに書かれた名前wに対して実際のファイルパスを返す。
        wがそのまま存在すればそのパス、なければ同ステムで別拡張子を探す。"""
        direct = os.path.join(base_dir, w)
        if os.path.exists(direct):
            return direct
        stem = os.path.splitext(w)[0]
        for ext in SOUND_EXTENSIONS:
            alt = os.path.join(base_dir, stem + ext)
            if os.path.exists(alt):
                return alt
        return None

    def existing(wavs):
        # 元の名前（BMS定義名）を返す。実ファイルが存在するものだけ。
        result = []
        for w in wavs:
            if resolve_wav_path(w) is not None:
                result.append(w)
        return result

    def resolve_wav_name(w):
        """後方互換用ラッパー。resolve_wav_pathがNoneならNone、あればwを返す。"""
        return w if resolve_wav_path(w) is not None else None

    common_existing = existing([w for w in wav_orders[chart_paths[0]] if os.path.splitext(w)[0].lower() in common_set])

    success = 0; all_packed: list = []; total_bytes = 0; failed: list = []

    # ── 共通 WAV なし or 1 難易度 ─────────────────────────────────────────────
    if len(chart_paths) == 1 or not common_existing:
        for i, path in enumerate(chart_paths):
            fname = os.path.basename(path)
            bname = os.path.splitext(fname)[0]
            wavs  = existing(wav_orders[path])
            miss  = [w for w in wav_orders[path]
                     if resolve_wav_name(w) is None]
            if miss:
                safe_print(f"  ⚠ WAV 未発見 {len(miss)} 件 ({fname})")
                log_error(f"Missing WAVs ({fname}): " + ", ".join(miss))
            if not wavs:
                safe_print(f"  ❌ 有効な WAV なし: {fname}")
                failed.append(fname); continue
            safe_print(f"\n📦 [{idx_start+i}/{total}] {fname}")
            try:
                _, packed, bw = write_wav_entries(wavs, base_dir, out_dir, bname)
                safe_print(f"  ✅ {bname}{TARGET_EXT}")
                success += 1; all_packed.extend(packed); total_bytes += bw
            except Exception as e:
                log_error(f"Pack error ({fname}): {e}")
                safe_print(f"  ❌ エラー: {e}")
                failed.append(fname)
        return success, all_packed, total_bytes, failed

    # ── 共通 WAV あり → 共有最適化 ────────────────────────────────────────────
    folder_name     = os.path.basename(base_dir)
    shared_bname    = folder_name
    shared_filename = shared_bname + TARGET_EXT

    safe_print(f"\n🔗 [{idx_start}/{total}〜] 共有最適化: {folder_name} "
               f"({len(chart_paths)} 難易度 / 共通 {len(common_existing)} WAV)")

    try:
        _, ps, bs = write_wav_entries(common_existing, base_dir, out_dir, shared_bname)
        all_packed.extend(ps); total_bytes += bs
        safe_print(f"  ✅ 共通: {shared_filename} ({len(ps)} 件)")
    except Exception as e:
        log_error(f"Shared pack error ({folder_name}): {e}")
        safe_print(f"  ❌ 共通 boxwav 失敗 → フォールバック: {e}")
        for i, path in enumerate(chart_paths):
            fname = os.path.basename(path); bname = os.path.splitext(fname)[0]
            wavs  = existing(wav_orders[path])
            if not wavs: failed.append(fname); continue
            safe_print(f"\n📦 [{idx_start+i}/{total}] {fname}")
            try:
                _, packed, bw = write_wav_entries(wavs, base_dir, out_dir, bname)
                success += 1; all_packed.extend(packed); total_bytes += bw
            except Exception as e2:
                log_error(f"Pack error ({fname}): {e2}"); failed.append(fname)
        return success, all_packed, total_bytes, failed

    for path in chart_paths:
        fname = os.path.basename(path); bname = os.path.splitext(fname)[0]
        out_path    = os.path.join(out_dir, bname + TARGET_EXT)
        unique_all  = [w for w in wav_orders[path] if os.path.splitext(w)[0].lower() not in common_set]
        unique_exist= existing(unique_all)
        unique_miss = [w for w in unique_all
                       if resolve_wav_name(w) is None]
        if unique_miss:
            log_error(f"Missing unique WAVs ({fname}): " + ", ".join(unique_miss))
        try:
            if not unique_exist:
                write_redirect(out_path, shared_filename)
                safe_print(f"  ✅ {bname}{TARGET_EXT}  → REDIRECT")
            else:
                write_redirect_extra(out_path, shared_filename, unique_exist, base_dir)
                all_packed.extend(os.path.join(base_dir, w) for w in unique_exist)
                safe_print(f"  ✅ {bname}{TARGET_EXT}  → REDIRECT+EXTRA ({len(unique_exist)} WAV)")
            success += 1
        except Exception as e:
            log_error(f"Redirect error ({fname}): {e}")
            safe_print(f"  ❌ エラー: {fname} ({e})")
            failed.append(fname)

    return success, all_packed, total_bytes, failed


# ═══════════════════════════════════════════════════════════════════════════════
#  WAV 変換のみモード (旧 convert_wav_parallel.py 相当)
# ═══════════════════════════════════════════════════════════════════════════════

def run_wav_convert_only(paths: list):
    print("=" * 60)
    print("  サウンド変換モード  "
          f"({WAV_TARGET_SR}Hz / {WAV_TARGET_CH}ch / {WAV_TARGET_BITS}bit WAV)")
    print(f"  対象拡張子: {', '.join(sorted(SOUND_EXTENSIONS))}")
    print("=" * 60)

    sound_paths: list[str] = []
    for path in paths:
        path = path.strip().strip('"').strip("'")
        if os.path.isdir(path):
            for root, _, files in os.walk(path):
                for f in files:
                    if os.path.splitext(f)[1].lower() in SOUND_EXTENSIONS:
                        sound_paths.append(os.path.join(root, f))
        elif os.path.isfile(path) and os.path.splitext(path)[1].lower() in SOUND_EXTENSIONS:
            sound_paths.append(path)

    if not sound_paths:
        print("  対象サウンドファイルが見つかりませんでした。")
        input("Enter で終了..."); return

    print(f"\n  対象サウンド: {len(sound_paths)} 件")
    w = input(f"  並列数 (Enter で自動={WAV_WORKERS}): ").strip()
    workers = int(w) if w.isdigit() and int(w) > 0 else WAV_WORKERS

    ok, conv, fail = scan_and_convert_wavs(sound_paths, workers)
    print(f"\n  既に OK: {ok} 件 / 変換成功: {conv} 件 / 失敗: {fail} 件")
    input("\nEnter で終了...")


# ═══════════════════════════════════════════════════════════════════════════════
#  パック共通処理
# ═══════════════════════════════════════════════════════════════════════════════

def run_pack(exts: set, mode_label: str,
             raw_paths: list, output_dir,
             auto_convert_wav: bool = False,
             ask_delete: bool = True,
             force_delete: bool = False,
             skip_existing: bool = False):
    """
    auto_convert_wav : True のとき WAV 検査→変換フェーズを先に実行する
    ask_delete       : False にすると WAV 削除確認をスキップ
    force_delete     : ask_delete=False のとき True なら削除、False なら残す
    skip_existing    : True のとき既存 .boxwav をスキップ
    """
    chart_files = collect_chart_files(raw_paths, exts)
    if not chart_files:
        print(f"\n{' / '.join(sorted(exts))} ファイルが見つかりませんでした。")
        input("Enter で終了..."); return

    total_files = len(chart_files)
    print(f"\n  対象 {mode_label}: {total_files} 件")
    if output_dir: print(f"  出力先: {output_dir}")
    print()

    # 通常モードのみ dry_run / skip_existing を聞く
    dry_run = False
    if not auto_convert_wav:
        print("─" * 60)
        dry_run = prompt_confirm("  [DRY RUN] 書き込まずにサイズ見積もりのみ確認しますか？")
        if not dry_run and not skip_existing:
            skip_existing = prompt_confirm("  既存の .boxwav をスキップしますか？")
        print()

    groups   = group_by_dir(chart_files)
    n_groups = len(groups)

    # ── DRY RUN ─────────────────────────────────────────────────────────────
    if dry_run:
        print(f"  [DRY RUN] {n_groups} グループを解析中...\n")
        for base_dir, paths in groups.items():
            ws: dict = {}
            for p in paths:
                try:
                    names, _ = collect_wav_names(p)
                    ws[p] = set(names)
                except Exception:
                    ws[p] = set()
            all_s  = [s for s in ws.values() if s]
            common = set.intersection(*all_s) if len(all_s) > 1 else set()
            union  = set.union(*all_s) if all_s else set()
            def sz(w):
                fp = os.path.join(base_dir, w)
                return os.path.getsize(fp) if os.path.exists(fp) else 0
            tm = sum(sz(w) for w in union)  / 1024 / 1024
            sm = sum(sz(w) for w in common) / 1024 / 1024
            sv = sm * (len(paths) - 1) if len(paths) > 1 else 0
            print(f"  {os.path.basename(base_dir)}: {len(paths)} 難易度 / "
                  f"全 WAV {tm:.1f} MB / 共通 {sm:.1f} MB / 削減見込み {sv:.1f} MB")
        input("\nEnter で終了..."); return

    # ── サウンド変換フェーズ ──────────────────────────────────────────────────
    if auto_convert_wav:
        all_sound: set[str] = set()
        for base_dir, paths in groups.items():
            for p in paths:
                try:
                    names, _ = collect_wav_names(p)
                    for n in names:
                        fp = os.path.join(base_dir, n)
                        if os.path.exists(fp):
                            all_sound.add(fp)
                        else:
                            # 同ステムで別拡張子を探す（例: v1.wav が v1.ogg として存在）
                            stem = os.path.splitext(fp)[0]
                            for ext in SOUND_EXTENSIONS:
                                alt = stem + ext
                                if os.path.exists(alt):
                                    all_sound.add(alt)
                                    break
                except Exception:
                    pass
        if all_sound:
            print(f"\n  📊 サウンドフォーマット確認中... ({len(all_sound)} 件)")
            ok, conv, fail = scan_and_convert_wavs(sorted(all_sound), WAV_WORKERS)
            log_info(f"audio convert: ok={ok} converted={conv} failed={fail}")
            print()

    # ── パックフェーズ ───────────────────────────────────────────────────────
    workers = min(PACK_WORKERS, n_groups)
    if n_groups > 1:
        print(f"  グループ数: {n_groups} / 並列: {workers}\n")

    total_start  = datetime.now()
    success      = 0
    all_packed:  list = []
    total_bytes  = 0
    failed_list: list = []
    counter_lock = threading.Lock()
    global_ctr   = [0]

    def run_group(base_dir, group_list):
        with counter_lock:
            idx = global_ctr[0]
            global_ctr[0] += len(group_list)
        out = output_dir if output_dir else base_dir
        if skip_existing:
            filtered = [p for p in group_list
                        if not os.path.exists(os.path.join(
                            out, os.path.splitext(os.path.basename(p))[0] + TARGET_EXT))]
            sk = len(group_list) - len(filtered)
            if sk: safe_print(f"  ⏭ {sk} 件スキップ (既存 .boxwav あり)")
            group_list = filtered
        if not group_list:
            return 0, [], 0, []
        return pack_folder_group(group_list, output_dir, idx + 1, total_files)

    if n_groups == 1:
        bd, gl = list(groups.items())[0]
        ok, wavs, bw, fails = run_group(bd, gl)
        success += ok; all_packed.extend(wavs); total_bytes += bw; failed_list.extend(fails)
    else:
        with ThreadPoolExecutor(max_workers=workers) as ex:
            futs = {ex.submit(run_group, bd, gl): bd for bd, gl in groups.items()}
            for fut in as_completed(futs):
                ok, wavs, bw, fails = fut.result()
                success += ok; all_packed.extend(wavs)
                total_bytes += bw; failed_list.extend(fails)

    elapsed   = (datetime.now() - total_start).total_seconds()
    speed_mbs = total_bytes / 1024 / 1024 / elapsed if elapsed > 0 else 0

    print("\n" + "=" * 60)
    print("  完了サマリー")
    print("=" * 60)
    print(f"  ✅ 成功: {success} / {total_files} 件")
    if failed_list:
        print(f"  ❌ 失敗: {len(failed_list)} 件")
        for n in failed_list: print(f"      {n}")
    print(f"  📦 書き込み: {total_bytes/1024/1024:.1f} MB  ({speed_mbs:.1f} MB/s)")
    print(f"  ⏱ 所要時間: {elapsed:.1f} 秒")
    if output_dir: print(f"  📁 出力先: {output_dir}")
    print()

    if all_packed:
        unique_packed = list(dict.fromkeys(all_packed))
        print(f"  パック済み WAV: {len(unique_packed)} 件")
        print("  ※ 削除されるのはパックに成功した WAV のみです。")
        print()
        if ask_delete:
            do_delete = prompt_confirm("  パック済み WAV ファイルを削除しますか？")
        else:
            do_delete = force_delete
            print(f"  WAV 削除: {'実行' if do_delete else 'スキップ'}")

        if do_delete:
            deleted = failed_del = 0
            for wav_path in unique_packed:
                try:
                    os.remove(wav_path); deleted += 1
                except OSError as e:
                    failed_del += 1; log_error(f"WAV 削除失敗: {wav_path}: {e}")
            print(f"  削除完了: {deleted} 件 / 失敗: {failed_del} 件")
            log_info(f"Bulk delete: {deleted} ok, {failed_del} failed")
        else:
            print("  削除をスキップしました。")
    else:
        print("  (削除対象なし)")

    if ask_delete:
        input("\n終了するには Enter キーを押してください...")


# ═══════════════════════════════════════════════════════════════════════════════
#  動画変換モード
# ═══════════════════════════════════════════════════════════════════════════════

def run_video_convert(paths: list):
    print("=" * 60)
    print("  動画変換モード  (Switch 向け: 256px / 30fps / H.264)")
    print("=" * 60)
    try:
        subprocess.run(["ffmpeg", "-version"], capture_output=True, check=True)
    except (FileNotFoundError, subprocess.CalledProcessError):
        print("  ❌ ffmpeg が見つかりません。")
        input("Enter で終了..."); return

    targets: list[str] = []
    for path in paths:
        path = path.strip().strip('"').strip("'")
        if os.path.isdir(path):
            for root, _, files in os.walk(path):
                for f in files:
                    if os.path.splitext(f)[1].lower() in VIDEO_EXTENSIONS:
                        targets.append(os.path.join(root, f))
        elif os.path.isfile(path) and os.path.splitext(path)[1].lower() in VIDEO_EXTENSIONS:
            targets.append(path)

    if not targets:
        print("  動画ファイルが見つかりませんでした。")
        input("Enter で終了..."); return

    print(f"\n  対象: {len(targets)} 件\n")
    ok = fail = 0
    for i, src in enumerate(targets, 1):
        dst   = os.path.splitext(src)[0] + "_converted.mp4"
        fname = os.path.basename(src)
        print(f"  [{i}/{len(targets)}] {fname}")
        try:
            r = subprocess.run(
                ["ffmpeg", "-i", src,
                 "-vf", "scale=-2:256", "-r", "30",
                 "-c:v", "libx264", "-crf", "23",
                 "-c:a", "aac", "-y", dst],
                capture_output=True, text=True)
            if r.returncode == 0:
                print(f"    ✅ {os.path.basename(dst)}  "
                      f"({os.path.getsize(dst)/1024/1024:.1f} MB)")
                ok += 1
            else:
                print(f"    ❌ ffmpeg エラー")
                log_error(f"video failed ({fname}): {r.stderr[-300:]}")
                fail += 1
        except Exception as e:
            print(f"    ❌ {e}")
            log_error(f"video exception ({fname}): {e}")
            fail += 1

    print(f"\n  完了: ✅ {ok} 件 / ❌ {fail} 件")
    input("\nEnter で終了...")


# ═══════════════════════════════════════════════════════════════════════════════
#  対話式プロンプト
# ═══════════════════════════════════════════════════════════════════════════════

def prompt_paths(label: str) -> list:
    print("─" * 60)
    print(f"  📂 {label}")
    print("  (複数可・空行で確定)")
    print("─" * 60)
    paths: list = []
    while True:
        line = input(f"  パス {len(paths)+1}: ").strip()
        if not line:
            if not paths: print("  ⚠ 1件以上入力してください。"); continue
            break
        # ターミナルからのドラッグ&ドロップで付くシェルエスケープを除去
        # 例: /foo/bar\ baz → /foo/bar baz  ,  /foo/can\'t → /foo/can't
        try:
            parsed = shlex.split(line)
        except ValueError:
            parsed = [line]
        for p in parsed:
            p = p.strip()
            if p:
                paths.append(p)
    return paths

def prompt_output_dir() -> str | None:
    print("\n─" * 60)
    print("  📁 出力先 (空 Enter → チャートと同じ場所)")
    print("─" * 60)
    line = input("  出力先: ").strip().strip('"').strip("'")
    if not line: return None
    try:
        os.makedirs(line, exist_ok=True)
    except OSError as e:
        print(f"  ⚠ {e}  → チャートと同じ場所に出力"); return None
    return line

def prompt_confirm(q: str) -> bool:
    return input(f"{q} [y/N]: ").strip().lower() == "y"

def select_mode() -> str:
    print()
    print("─" * 60)
    print("  モードを選択")
    print("─" * 60)
    print("  1) bmson → boxwav")
    print("  2) BMS   → boxwav  (.bms / .bme / .bml)")
    print("  3) すべて → boxwav  (bmson + BMS 混在フォルダ)  ★")
    print("  4) サウンド変換のみ  (ogg/mp3/flac等→22050Hz/mono/8bit WAV)")
    print("  5) サウンド変換 + boxwav 化  【全自動】             ★")
    print("     フォーマット確認→変換→boxwav化→元ファイル削除 を一括実行")
    print("  6) 動画変換  (Switch 向け mp4)")
    print()
    while True:
        sel = input("  番号 [1-6]: ").strip()
        if sel in ("1","2","3","4","5","6"): return sel
        print("  ⚠ 1〜6 を入力してください。")


# ═══════════════════════════════════════════════════════════════════════════════
#  エントリポイント
# ═══════════════════════════════════════════════════════════════════════════════

def main():
    print("=" * 60)
    print("  BOXWAV Packer  (unified edition v2)")
    print(f"  boxwav 1 パート上限 : {MAX_BOXWAV_SIZE//1024//1024} MB")
    print(f"  WAV 変換目標        : {WAV_TARGET_SR}Hz / "
          f"{WAV_TARGET_CH}ch / {WAV_TARGET_BITS}bit")
    print("=" * 60)

    args = sys.argv[1:]

    # ── --auto : WAV 変換 + boxwav 化 + WAV 削除 を全確認スキップで一気通貫 ──
    if args and args[0] == "--auto":
        raw_paths = args[1:] or prompt_paths("フォルダをドロップまたはパスを入力")
        _set_log_file("log_packer_auto.txt")
        print(f"\n  🚀 全自動モード開始\n"
              f"  WAV 変換 → boxwav 化 (既存スキップ) → WAV 削除\n")
        run_pack(ALL_CHART_EXTS, "チャート (bmson + BMS)", raw_paths,
                 output_dir=None,
                 auto_convert_wav=True,
                 ask_delete=False,
                 force_delete=True,
                 skip_existing=True)
        input("\n終了するには Enter キーを押してください...")
        return

    # ── フラグ指定 ────────────────────────────────────────────────────────────
    FLAG_MODE = {
        "--bmson": "1", "--bms": "2", "--all": "3",
        "--wav":   "4", "--wav-pack": "5", "--video": "6",
    }
    if args and args[0] in FLAG_MODE:
        mode      = FLAG_MODE[args.pop(0)]
        raw_paths = args or prompt_paths("パスを入力")
        output_dir = None
    elif args:
        # パスだけ渡されたとき: 拡張子で自動判定
        raw_paths  = args
        output_dir = None
        first = raw_paths[0].strip().strip('"').strip("'")
        ext = ""
        if os.path.isfile(first):
            ext = os.path.splitext(first)[1].lower()
        elif os.path.isdir(first):
            for root, _, files in os.walk(first):
                for f in files:
                    e = os.path.splitext(f)[1].lower()
                    if e in ALL_CHART_EXTS | VIDEO_EXTENSIONS:
                        ext = e; break
                if ext: break
        if ext in BMSON_EXTENSIONS:   mode = "1"
        elif ext in BMS_EXTENSIONS:   mode = "2"
        elif ext in VIDEO_EXTENSIONS: mode = "6"
        else:                         mode = select_mode()
    else:
        mode = select_mode()
        print()
        if mode in ("1", "2", "3", "5"):
            raw_paths  = prompt_paths("チャートファイルまたはフォルダのパスを入力")
            output_dir = prompt_output_dir() if mode != "5" else None
        elif mode == "4":
            raw_paths  = prompt_paths("サウンドフォルダのパスを入力")
            output_dir = None
        else:
            raw_paths  = prompt_paths("動画ファイルまたはフォルダのパスを入力")
            output_dir = None

    _set_log_file(f"log_packer_mode{mode}.txt")

    if mode == "1":
        print(f"\n  モード: bmson → boxwav\n")
        run_pack(BMSON_EXTENSIONS, "bmson", raw_paths, output_dir)

    elif mode == "2":
        print(f"\n  モード: BMS → boxwav\n")
        run_pack(BMS_EXTENSIONS, "BMS", raw_paths, output_dir)

    elif mode == "3":
        print(f"\n  モード: すべて → boxwav  (bmson + BMS 混在)\n")
        run_pack(ALL_CHART_EXTS, "チャート (bmson + BMS)", raw_paths, output_dir)

    elif mode == "4":
        run_wav_convert_only(raw_paths)

    elif mode == "5":
        print(f"\n  モード: WAV 変換 + boxwav 化  【全自動】")
        print(f"  ① WAV フォーマット確認 → 必要なものだけ変換")
        print(f"  ② boxwav 化 (既存 .boxwav はスキップ)")
        print(f"  ③ パック済み WAV 削除確認\n")
        run_pack(ALL_CHART_EXTS, "チャート (bmson + BMS)", raw_paths,
                 output_dir=output_dir,
                 auto_convert_wav=True,
                 ask_delete=True,
                 skip_existing=True)

    elif mode == "6":
        run_video_convert(raw_paths)


if __name__ == "__main__":
    main()
