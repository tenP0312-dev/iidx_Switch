# -*- coding: utf-8 -*-
"""
combine_digits.py  -  数字画像結合ツール
依存: pip install Pillow

────────────────────────────────────────
モード
  1) 0〜9 横並び  →  0123456789 一列の画像
  2) コンボ用     →  各色ごとに 0000/1111/.../9999 を縦に並べた画像

入力形式
  A) 個別ファイル: playm_gg_00.png 〜 playm_gg_09.png など
  B) スプライトシート: 10行 × N列 で並んだ1枚の画像
────────────────────────────────────────
"""

import sys
from pathlib import Path
from PIL import Image


# ═══════════════════════════════════════
#  画像結合ユーティリティ
# ═══════════════════════════════════════

def concat_h(images: list) -> Image.Image:
    """隙間なし横結合"""
    w = sum(i.width for i in images)
    h = max(i.height for i in images)
    out = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    x = 0
    for img in images:
        out.paste(img.convert("RGBA"), (x, 0))
        x += img.width
    return out

def concat_v(images: list) -> Image.Image:
    """隙間なし縦結合"""
    w = max(i.width for i in images)
    h = sum(i.height for i in images)
    out = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    y = 0
    for img in images:
        out.paste(img.convert("RGBA"), (0, y))
        y += img.height
    return out

def save_png(img: Image.Image, path: Path):
    img.save(path, format="PNG")
    print(f"  ✓  {path.name}  ({img.width}×{img.height})")


# ═══════════════════════════════════════
#  入力形式 A: 個別ファイルから読み込み
# ═══════════════════════════════════════

def find_digit_files(folder: Path) -> dict:
    found = {}
    exts = {".png", ".jpg", ".jpeg", ".webp", ".bmp"}
    for f in sorted(folder.iterdir()):
        if f.suffix.lower() not in exts:
            continue
        stem = f.stem
        if stem[-3:-2] == "_" and stem[-2:].isdigit():
            n = int(stem[-2:])
            if 0 <= n <= 9:
                found[n] = f
        elif stem[-1:].isdigit():
            n = int(stem[-1])
            if 0 <= n <= 9 and n not in found:
                found[n] = f
    return found


# ═══════════════════════════════════════
#  入力形式 B: スプライトシートから切り出し
# ═══════════════════════════════════════

def slice_spritesheet(path: Path, rows: int = 10) -> list:
    """
    スプライトシートを切り出す。
    rows 行 × N 列と仮定。
    戻り値: [[col0_row0, col1_row0, ...], [col0_row1, ...], ...]
      → result[digit][color_index]
    """
    img = Image.open(path)
    w, h = img.size
    cell_h = h // rows
    # 列数を自動検出: 割り切れる列数の中で cell_w が cell_h に最も近いもの
    best_cols, best_diff = 1, float("inf")
    for c in range(1, 17):
        if w % c != 0:
            continue
        cw = w // c
        diff = abs(cw - cell_h)
        if diff < best_diff:
            best_diff, best_cols = diff, c
    cols   = best_cols
    cell_w = w // cols

    print(f"  スプライトシート検出: {cols} 列 × {rows} 行  (1セル {cell_w}×{cell_h})")

    result = []
    for row in range(rows):
        row_imgs = []
        for col in range(cols):
            x = col * cell_w
            y = row * cell_h
            cell = img.crop((x, y, x + cell_w, y + cell_h))
            row_imgs.append(cell)
        result.append(row_imgs)  # result[digit] = [色0, 色1, ...]
    return result


# ═══════════════════════════════════════
#  モード 1: 0〜9 横並び
# ═══════════════════════════════════════

def run_strip(digit_imgs: list, out_dir: Path, label: str = ""):
    """digit_imgs: [img_0, img_1, ..., img_9]"""
    result = concat_h(digit_imgs)
    name = f"digits_0to9{label}.png"
    save_png(result, out_dir / name)


# ═══════════════════════════════════════
#  モード 2: コンボ用 縦並び
#  0000      (digit 0 を repeat 枚横に並べたもの)
#  1111
#  ...
#  9999
#  → これを縦に積む
# ═══════════════════════════════════════

def run_combo(digit_imgs: list, out_dir: Path,
              repeat: int = 4, label: str = ""):
    """digit_imgs: [img_0, img_1, ..., img_9]"""
    rows = []
    for n in range(10):
        row = concat_h([digit_imgs[n]] * repeat)
        rows.append(row)
    result = concat_v(rows)
    name = f"combo_{repeat}digit{label}.png"
    save_png(result, out_dir / name)


# ═══════════════════════════════════════
#  対話ヘルパー
# ═══════════════════════════════════════

def sep():
    print("─" * 54)

def prompt_path(label: str) -> Path:
    sep()
    print(f"  📂 {label}")
    print("  (ドラッグ&ドロップ可)")
    sep()
    while True:
        raw = input("  パス: ").strip().strip('"').strip("'")
        p = Path(raw)
        if p.exists():
            return p
        print("  ⚠ 見つかりません。")

def prompt_output(default: Path) -> Path:
    raw = input(f"  出力先 [{default}]: ").strip().strip('"').strip("'")
    p = Path(raw) if raw else default
    p.mkdir(parents=True, exist_ok=True)
    return p


# ═══════════════════════════════════════
#  エントリポイント
# ═══════════════════════════════════════

def main():
    print("=" * 54)
    print("  combine_digits  -  数字画像結合ツール")
    print("=" * 54)

    # ── 入力ソース選択 ────────────────────────────────────
    print()
    sep()
    print("  入力形式を選択")
    sep()
    print("  1) 個別ファイル  (playm_gg_00.png 〜 09.png など)")
    print("  2) スプライトシート  (10行×N列の1枚画像)")
    print()
    while True:
        src_sel = input("  番号 [1/2]: ").strip()
        if src_sel in ("1", "2"):
            break
        print("  ⚠ 1 か 2 を入力してください。")

    # ── ファイル読み込み ──────────────────────────────────
    if src_sel == "1":
        # 個別ファイル
        if len(sys.argv) > 1:
            folder = Path(sys.argv[1].strip().strip('"').strip("'"))
        else:
            folder = prompt_path("数字画像フォルダ")

        digit_files = find_digit_files(folder)
        missing = [i for i in range(10) if i not in digit_files]
        if missing:
            print(f"\n  ❌ 見つからない数字: {missing}")
            input("Enter で終了..."); return

        print(f"\n  対象ファイル:")
        for i in range(10):
            print(f"    {i}: {digit_files[i].name}")

        # 色バリエーションなし → 1セット
        color_sets = {
            "": [Image.open(digit_files[i]) for i in range(10)]
        }
        out_default = folder / "output"

    else:
        # スプライトシート
        sheet_path = prompt_path("スプライトシート画像")
        sprite_data = slice_spritesheet(sheet_path)  # [digit][color]

        n_colors = len(sprite_data[0])
        print(f"\n  検出された色数: {n_colors}")

        # 色ラベルを設定
        default_labels = ["_blue", "_green", "_pink", "_orange",
                          "_c4", "_c5", "_c6", "_c7"][:n_colors]
        print("  各列の色ラベルを入力 (空 Enter でデフォルト使用)")
        color_labels = []
        for i, dl in enumerate(default_labels):
            raw = input(f"    列 {i} のラベル [{dl}]: ").strip()
            color_labels.append(raw if raw else dl)

        # color_sets: {label: [img_0, img_1, ..., img_9]}
        color_sets = {}
        for ci, lbl in enumerate(color_labels):
            color_sets[lbl] = [sprite_data[d][ci] for d in range(10)]

        out_default = sheet_path.parent / "output"

    # ── 出力先 ────────────────────────────────────────────
    print()
    out_dir = prompt_output(out_default)

    # ── モード選択 ────────────────────────────────────────
    print()
    sep()
    print("  モードを選択")
    sep()
    print("  1) 0〜9 横並び")
    print("  2) コンボ用 (0000〜9999 縦並び)")
    print("  3) 両方")
    print()
    while True:
        mode = input("  番号 [1-3]: ").strip()
        if mode in ("1", "2", "3"):
            break
        print("  ⚠ 1〜3 を入力してください。")

    repeat = 4
    if mode in ("2", "3"):
        raw = input("  コンボの桁数 [4]: ").strip()
        if raw.isdigit() and int(raw) > 0:
            repeat = int(raw)

    # ── 実行 ──────────────────────────────────────────────
    print()
    for label, imgs in color_sets.items():
        if len(color_sets) > 1:
            print(f"  [{label}]")
        if mode in ("1", "3"):
            run_strip(imgs, out_dir, label=label)
        if mode in ("2", "3"):
            run_combo(imgs, out_dir, repeat=repeat, label=label)

    print()
    print(f"  完了 → {out_dir}")
    input("\n終了するには Enter を押してください...")


if __name__ == "__main__":
    main()
