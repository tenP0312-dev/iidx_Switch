# -*- coding: utf-8 -*-
"""
resize_images.py  - 画像一括リサイズツール
依存: pip install Pillow

────────────────────────────────────────────────────
モード一覧
────────────────────────────────────────────────────
  1) 片軸指定  : Y=200 に潰す / Y=200 で比率維持 など
  2) W×H 指定 : 引き伸ばし / 比率維持(fit) / クロップ / 余白埋め
  3) 解像度換算: 「4K基準の画像を HD で使う」など
  4) フォーマット変換のみ  (PNG→WebP / JPG など)

コマンドライン:
  python resize_images.py PATH...   パスを渡すと即スキャン
  python resize_images.py            対話モード
────────────────────────────────────────────────────
"""

import os, sys, threading
from pathlib import Path
from concurrent.futures import ThreadPoolExecutor
from datetime import datetime

try:
    from PIL import Image
except ImportError:
    print("Pillow が必要です: pip install Pillow")
    sys.exit(1)

# ─── 定数 ────────────────────────────────────────────────────────────────────

SUPPORTED_READ  = {".png", ".jpg", ".jpeg", ".webp", ".bmp", ".tiff", ".tif", ".gif"}
SUPPORTED_WRITE = {".png", ".jpg", ".jpeg", ".webp", ".bmp", ".tiff"}

PRESETS = {
    "4k":    (3840, 2160),
    "1440p": (2560, 1440),
    "1080p": (1920, 1080),
    "hd":    (1280,  720),
    "sd":    ( 854,  480),
    "sq4k":  (4096, 4096),
    "sq2k":  (2048, 2048),
    "sq1k":  (1024, 1024),
}

WORKERS = max(1, (os.cpu_count() or 4))
_print_lock = threading.Lock()


# ═══════════════════════════════════════════════════════════════════════════════
#  ユーティリティ
# ═══════════════════════════════════════════════════════════════════════════════

def safe_print(msg: str):
    with _print_lock:
        print(msg)

def sep():
    print("─" * 60)

def header(title: str):
    print("=" * 60)
    print(f"  {title}")
    print("=" * 60)


# ═══════════════════════════════════════════════════════════════════════════════
#  ファイル収集
# ═══════════════════════════════════════════════════════════════════════════════

def collect_images(raw_paths: list, recursive: bool = False) -> list:
    result = []
    for raw in raw_paths:
        p = Path(raw.strip().strip('"').strip("'"))
        if p.is_file():
            if p.suffix.lower() in SUPPORTED_READ:
                result.append(p)
            else:
                print(f"  ⚠ 対応外スキップ: {p.name}")
        elif p.is_dir():
            pattern = "**/*" if recursive else "*"
            for f in sorted(p.glob(pattern)):
                if f.is_file() and f.suffix.lower() in SUPPORTED_READ:
                    result.append(f)
        else:
            print(f"  ⚠ 見つからない: {raw}")
    # 重複除去
    seen, unique = set(), []
    for p in result:
        key = p.resolve()
        if key not in seen:
            seen.add(key); unique.append(p)
    return unique


# ═══════════════════════════════════════════════════════════════════════════════
#  リサイズ処理
# ═══════════════════════════════════════════════════════════════════════════════

def do_stretch(img, w, h):
    return img.resize((w, h), Image.LANCZOS)

def do_fit(img, w, h):
    img = img.copy()
    img.thumbnail((w, h), Image.LANCZOS)
    return img

def do_crop(img, w, h):
    ow, oh = img.size
    scale = max(w / ow, h / oh)
    nw, nh = int(ow * scale), int(oh * scale)
    img = img.resize((nw, nh), Image.LANCZOS)
    l, t = (nw - w) // 2, (nh - h) // 2
    return img.crop((l, t, l + w, t + h))

def do_pad(img, w, h, bg=(0, 0, 0)):
    img = img.copy()
    img.thumbnail((w, h), Image.LANCZOS)
    mode = "RGBA" if img.mode == "RGBA" else "RGB"
    fill = bg + (255,) if mode == "RGBA" else bg
    canvas = Image.new(mode, (w, h), fill)
    canvas.paste(img, ((w - img.width) // 2, (h - img.height) // 2))
    return canvas

def do_axis(img, axis, value, squash):
    ow, oh = img.size
    if axis == "y":
        nh = value
        nw = ow if squash else max(1, round(ow * value / oh))
    else:
        nw = value
        nh = oh if squash else max(1, round(oh * value / ow))
    return img.resize((nw, nh), Image.LANCZOS)

def do_reinterpret(img, src_preset, dst_preset):
    sw, sh = PRESETS[src_preset]
    dw, dh = PRESETS[dst_preset]
    ow, oh = img.size
    nw = max(1, round(ow * dw / sw))
    nh = max(1, round(oh * dh / sh))
    return img.resize((nw, nh), Image.LANCZOS)


# ═══════════════════════════════════════════════════════════════════════════════
#  保存
# ═══════════════════════════════════════════════════════════════════════════════

def make_dst(src: Path, cfg: dict) -> Path:
    out_dir = cfg.get("out_dir")
    suffix  = cfg.get("suffix", "")
    ext     = cfg.get("ext") or src.suffix
    if not ext.startswith("."):
        ext = "." + ext

    if cfg.get("inplace"):
        return src
    if out_dir:
        d = Path(out_dir); d.mkdir(parents=True, exist_ok=True)
    else:
        d = src.parent / "resized"; d.mkdir(parents=True, exist_ok=True)
    return d / (src.stem + suffix + ext.lower())

def save_image(img, dst: Path, quality: int = 90):
    ext = dst.suffix.lower()
    fmt = {".jpg":"JPEG", ".jpeg":"JPEG", ".png":"PNG",
           ".webp":"WEBP", ".bmp":"BMP", ".tiff":"TIFF"}.get(ext, "PNG")

    if fmt == "JPEG" and img.mode in ("RGBA", "P", "LA"):
        bg = Image.new("RGB", img.size, (255, 255, 255))
        mask = img.split()[3] if img.mode == "RGBA" else None
        bg.paste(img if img.mode == "RGBA" else img.convert("RGBA"), mask=mask)
        img = bg

    kwargs = {}
    if fmt in ("JPEG", "WEBP"):
        kwargs["quality"] = quality
    img.save(dst, format=fmt, **kwargs)


# ═══════════════════════════════════════════════════════════════════════════════
#  1ファイル処理 & バッチ実行
# ═══════════════════════════════════════════════════════════════════════════════

def process_one(src: Path, cfg: dict):
    """戻り値: (成功, orig_size_str, new_size_str or エラー)"""
    try:
        img = Image.open(src)
        orig = str(img.size)
        mode = cfg["mode"]

        if   mode == "axis":         out = do_axis(img, cfg["axis"], cfg["value"], cfg.get("squash", False))
        elif mode == "stretch":      out = do_stretch(img, cfg["w"], cfg["h"])
        elif mode == "fit":          out = do_fit(img, cfg["w"], cfg["h"])
        elif mode == "crop":         out = do_crop(img, cfg["w"], cfg["h"])
        elif mode == "pad":          out = do_pad(img, cfg["w"], cfg["h"], cfg.get("bg", (0,0,0)))
        elif mode == "reinterpret":  out = do_reinterpret(img, cfg["src_preset"], cfg["dst_preset"])
        elif mode == "fmt_only":     out = img
        else: return False, "", f"不明なモード: {mode}"

        if cfg.get("dry_run"):
            return True, orig, str(out.size) + "  [dry-run]"

        dst = make_dst(src, cfg)
        save_image(out, dst, cfg.get("quality", 90))
        return True, orig, str(out.size)
    except Exception as e:
        return False, "", str(e)


def run_batch(files: list, cfg: dict):
    total = len(files)
    ok = fail = 0
    lock = threading.Lock()

    def do(src):
        nonlocal ok, fail
        success, orig, result = process_one(src, cfg)
        with lock:
            if success:
                ok += 1
                safe_print(f"  ✓  {src.name}  {orig} → {result}")
            else:
                fail += 1
                safe_print(f"  ✗  {src.name}  エラー: {result}")

    with ThreadPoolExecutor(max_workers=WORKERS) as ex:
        list(ex.map(do, files))

    print()
    print(f"  完了: ✅ {ok} 件 / ❌ {fail} 件  (合計 {total} 件)")


# ═══════════════════════════════════════════════════════════════════════════════
#  対話ヘルパー
# ═══════════════════════════════════════════════════════════════════════════════

def prompt_paths(label="画像ファイルまたはフォルダのパスを入力") -> list:
    sep()
    print(f"  📂 {label}")
    print("  (複数可・空行で確定・ドラッグ&ドロップ可)")
    sep()
    paths = []
    while True:
        line = input(f"  パス {len(paths)+1}: ").strip().strip('"').strip("'")
        if not line:
            if not paths:
                print("  ⚠ 1件以上入力してください。"); continue
            break
        paths.append(line)
    return paths

def prompt_int(label, default=None):
    while True:
        hint = f" [{default}]" if default is not None else ""
        raw = input(f"  {label}{hint}: ").strip()
        if not raw and default is not None:
            return default
        try:
            v = int(raw)
            if v > 0: return v
            print("  ⚠ 正の整数を入力してください。")
        except ValueError:
            print("  ⚠ 数値を入力してください。")

def prompt_yn(q, default=False):
    hint = "[Y/n]" if default else "[y/N]"
    raw = input(f"  {q} {hint}: ").strip().lower()
    return (raw == "y") if raw else default

def prompt_output_cfg() -> dict:
    cfg = {}
    print()
    sep()
    print("  📁 出力設定")
    sep()
    print("    1) ./resized/ フォルダに保存（デフォルト）")
    print("    2) 元ファイルを上書き")
    print("    3) 出力先を指定")
    sel = input("  選択 [1]: ").strip() or "1"
    if sel == "2":
        cfg["inplace"] = True
    elif sel == "3":
        d = input("  出力先パス: ").strip().strip('"').strip("'")
        cfg["out_dir"] = d

    print()
    if prompt_yn("出力フォーマットを変換しますか？", default=False):
        print("    1) PNG  2) JPG  3) WEBP  4) BMP  5) TIFF")
        fmap = {"1":"png","2":"jpg","3":"webp","4":"bmp","5":"tiff"}
        raw = input("  フォーマット [1-5]: ").strip()
        if raw in fmap:
            cfg["ext"] = fmap[raw]

    cfg["quality"] = 90
    if cfg.get("ext") in ("jpg","webp"):
        cfg["quality"] = prompt_int("品質 (1-95)", default=90)

    sfx = input("  出力ファイル名サフィックス（例: _hd）空 Enter でスキップ: ").strip()
    if sfx:
        cfg["suffix"] = sfx

    return cfg

def print_confirm_summary(files: list, cfg: dict):
    """実行前の設定サマリーを表示する"""
    mode_labels = {
        "axis":        "片軸指定",
        "stretch":     "W×H 引き伸ばし",
        "fit":         "W×H 比率維持（枠に収める）",
        "crop":        "W×H 中央クロップ",
        "pad":         "W×H 余白埋め",
        "reinterpret": "解像度換算",
        "fmt_only":    "フォーマット変換のみ",
    }
    print()
    print("  ┌─────────────────────────────────────────────┐")
    print("  │              実行設定の確認                 │")
    print("  ├─────────────────────────────────────────────┤")
    print(f"  │  対象ファイル : {len(files)} 件")

    # リサイズ設定
    mode = cfg.get("mode", "")
    print(f"  │  モード       : {mode_labels.get(mode, mode)}")

    if mode == "axis":
        axis_lbl = "X (横幅)" if cfg["axis"] == "x" else "Y (縦幅)"
        method   = "潰す（比率無視）" if cfg.get("squash") else "比率維持"
        print(f"  │  軸           : {axis_lbl}  →  {cfg['value']} px")
        print(f"  │  方式         : {method}")

    elif mode in ("stretch","fit","crop","pad"):
        print(f"  │  サイズ       : {cfg['w']} × {cfg['h']} px")
        if mode == "pad":
            print(f"  │  背景色       : RGB{cfg.get('bg', (0,0,0))}")

    elif mode == "reinterpret":
        src_p, dst_p = cfg["src_preset"], cfg["dst_preset"]
        sw, sh = PRESETS[src_p]; dw, dh = PRESETS[dst_p]
        print(f"  │  基準解像度   : {src_p} ({sw}×{sh})")
        print(f"  │  変換先解像度 : {dst_p} ({dw}×{dh})")
        print(f"  │  倍率         : x={dw/sw:.4f}  y={dh/sh:.4f}")

    # 出力先
    if cfg.get("inplace"):
        out_str = "元ファイルを上書き ⚠"
    elif cfg.get("out_dir"):
        out_str = cfg["out_dir"]
    else:
        out_str = "./resized/"
    print(f"  │  出力先       : {out_str}")

    # フォーマット・品質
    ext = cfg.get("ext")
    print(f"  │  フォーマット : {ext.upper() if ext else '変換なし（元のまま）'}")
    if ext in ("jpg","webp"):
        print(f"  │  品質         : {cfg.get('quality', 90)}")

    # サフィックス
    sfx = cfg.get("suffix")
    if sfx:
        print(f"  │  ファイル名   : <元の名前>{sfx}.<ext>")

    # dry-run
    if cfg.get("dry_run"):
        print(f"  │  dry-run      : ON（保存しません）")

    print("  └─────────────────────────────────────────────┘")


def preview_files(files: list):
    print()
    print(f"  対象: {len(files)} ファイル")
    show = files if len(files) <= 8 else files[:5]
    for f in show:
        try:
            img = Image.open(f)
            print(f"    {f.name}  {img.size}")
            img.close()
        except Exception:
            print(f"    {f.name}  (読み取り失敗)")
    if len(files) > 8:
        print(f"    ... 他 {len(files)-5} 件")


# ═══════════════════════════════════════════════════════════════════════════════
#  各モードの実装
# ═══════════════════════════════════════════════════════════════════════════════

def run_mode_axis(files: list, dry_run: bool):
    sep()
    print("  【モード 1】片軸指定リサイズ")
    sep()
    print("    1) X 軸（横幅）を指定")
    print("    2) Y 軸（縦幅）を指定")
    axis = "x" if input("  軸 [1=X / 2=Y]: ").strip() == "1" else "y"
    label = "横幅" if axis == "x" else "縦幅"
    value = prompt_int(f"{label} (px)")

    print()
    print("    1) 比率維持（もう一方の軸を自動計算）")
    print("    2) 潰す（もう一方の軸はそのまま）")
    squash = input("  方式 [1/2]: ").strip() == "2"

    cfg = {"mode":"axis", "axis":axis, "value":value,
           "squash":squash, "dry_run":dry_run, **prompt_output_cfg()}

    print_confirm_summary(files, cfg)
    if not prompt_yn("この設定で実行しますか？", default=True):
        print("  キャンセルしました。"); return
    print()
    run_batch(files, cfg)


def run_mode_wh(files: list, dry_run: bool):
    sep()
    print("  【モード 2】W×H 指定リサイズ")
    sep()
    w = prompt_int("横幅 W (px)")
    h = prompt_int("縦幅 H (px)")

    print()
    print("    1) 引き伸ばし  (比率無視)")
    print("    2) 比率維持・枠に収める  (余白なし)")
    print("    3) 比率維持・中央クロップ")
    print("    4) 比率維持・余白を塗りつぶし")
    sub = input("  方式 [1-4]: ").strip()
    mode = {"1":"stretch","2":"fit","3":"crop","4":"pad"}.get(sub, "stretch")

    bg = (0, 0, 0)
    if mode == "pad":
        raw = input("  背景色 R,G,B [0,0,0]: ").strip() or "0,0,0"
        try:
            bg = tuple(int(x) for x in raw.split(","))
        except Exception:
            bg = (0, 0, 0)

    cfg = {"mode":mode, "w":w, "h":h, "bg":bg,
           "dry_run":dry_run, **prompt_output_cfg()}

    print_confirm_summary(files, cfg)
    if not prompt_yn("この設定で実行しますか？", default=True):
        print("  キャンセルしました。"); return
    print()
    run_batch(files, cfg)


def run_mode_reinterpret(files: list, dry_run: bool):
    sep()
    print("  【モード 3】解像度換算リサイズ")
    sep()
    preset_list = list(PRESETS.keys())
    for i, name in enumerate(preset_list, 1):
        w, h = PRESETS[name]
        print(f"    {i:2}) {name:8}  {w}×{h}")
    print()
    print("  「この画像は ○○ 解像度基準で作られた」→「△△ で使うための px に変換」")
    print()

    def pick_preset(label):
        while True:
            raw = input(f"  {label} (番号 or 名前): ").strip().lower()
            if raw in PRESETS:
                return raw
            try:
                idx = int(raw) - 1
                if 0 <= idx < len(preset_list):
                    return preset_list[idx]
            except ValueError:
                pass
            print(f"  ⚠ プリセット名か番号 1〜{len(preset_list)} を入力してください。")

    src_p = pick_preset("元の基準解像度 (画像が作られた基準)")
    dst_p = pick_preset("変換先の解像度 (実際に使う解像度)")
    sw, sh = PRESETS[src_p]; dw, dh = PRESETS[dst_p]
    print(f"\n  {src_p} ({sw}×{sh}) → {dst_p} ({dw}×{dh})")
    print(f"  倍率: x={dw/sw:.4f}  y={dh/sh:.4f}")

    cfg = {"mode":"reinterpret", "src_preset":src_p, "dst_preset":dst_p,
           "dry_run":dry_run, **prompt_output_cfg()}

    print_confirm_summary(files, cfg)
    if not prompt_yn("この設定で実行しますか？", default=True):
        print("  キャンセルしました。"); return
    print()
    run_batch(files, cfg)


def run_mode_fmt(files: list, dry_run: bool):
    sep()
    print("  【モード 4】フォーマット変換のみ")
    sep()
    print("    1) PNG  2) JPG  3) WEBP  4) BMP  5) TIFF")
    fmap = {"1":"png","2":"jpg","3":"webp","4":"bmp","5":"tiff"}
    raw = input("  変換先フォーマット [1-5]: ").strip()
    ext = fmap.get(raw, "png")

    quality = 90
    if ext in ("jpg","webp"):
        quality = prompt_int("品質 (1-95)", default=90)

    out_cfg = prompt_output_cfg()
    out_cfg["ext"] = ext
    out_cfg["quality"] = quality
    cfg = {"mode":"fmt_only", "dry_run":dry_run, **out_cfg}

    print_confirm_summary(files, cfg)
    if not prompt_yn("この設定で実行しますか？", default=True):
        print("  キャンセルしました。"); return
    print()
    run_batch(files, cfg)


# ═══════════════════════════════════════════════════════════════════════════════
#  エントリポイント
# ═══════════════════════════════════════════════════════════════════════════════

def main():
    header("resize_images  -  画像一括リサイズツール")
    print(f"  対応入力 : {' / '.join(sorted(SUPPORTED_READ))}")
    print(f"  並列処理 : {WORKERS} スレッド")
    print("=" * 60)

    raw_paths = sys.argv[1:] or prompt_paths()

    recursive = False
    if any(Path(p.strip().strip('"').strip("'")).is_dir() for p in raw_paths):
        recursive = prompt_yn("\nサブフォルダも対象にしますか？", default=False)

    files = collect_images(raw_paths, recursive=recursive)

    if not files:
        print("\n  対象の画像ファイルが見つかりませんでした。")
        input("Enter で終了..."); return

    preview_files(files)

    dry_run = prompt_yn("\n変換せず内容だけ確認しますか？ (dry-run)", default=False)

    # ── モード選択 ──────────────────────────────────────────────────────────────
    print()
    sep()
    print("  モードを選択")
    sep()
    print("  1) 片軸指定     Y=200 に潰す / Y=200 で比率維持 など")
    print("  2) W×H 指定    引き伸ばし / fit / クロップ / 余白埋め")
    print("  3) 解像度換算   「4K 基準の画像を HD で使う」など")
    print("  4) フォーマット変換のみ  (PNG→WebP など)")
    print()
    while True:
        sel = input("  番号 [1-4]: ").strip()
        if sel in ("1","2","3","4"): break
        print("  ⚠ 1〜4 を入力してください。")

    print()
    dispatch = {"1":run_mode_axis, "2":run_mode_wh,
                "3":run_mode_reinterpret, "4":run_mode_fmt}
    dispatch[sel](files, dry_run)

    print()
    input("終了するには Enter を押してください...")


if __name__ == "__main__":
    main()
