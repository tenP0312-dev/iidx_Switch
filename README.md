unofficial port of beatmaniaIIDX to nintendo switch.

# BMSWITCH

**Nintendo Switch 向け BMS / BMSON リズムゲームプレイヤー**

BMS・BMSON 形式の譜面ファイルを Switch 上でプレイするための Homebrew アプリです。

---

## 動作環境

- Nintendo Switch（Homebrew 環境）
- Atmosphere などの CFW が導入済みであること

---


## 機能

### 対応フォーマット

| フォーマット | 説明 |
|---|---|
| `.bmson` | BMSON 形式（推奨） |
| `.bms` / `.bme` / `.bml` | BMS 形式 |

### ゲームプレイ

- **7KEY + SCRATCH** の標準 IIDX スタイル
- **BGA 動画再生**：MP4（H.264）、最大 30fps
- **256 同時発音**：BGM・SE を含む大量サウンドチャンネルに対応
- **FAST / SLOW 表示**：判定タイミングのズレを ms 単位で表示（ON / OFF / ms 表示切替）
- **フルコンボエフェクト**：FC 達成時に専用演出

### 判定

| 判定 | |
|---|---|
| P-GREAT | |
| GREAT | |
| GOOD | |
| BAD | |
| POOR | |

### スコア・ランク

EX SCORE（P-GREAT × 2 + GREAT × 1）でスコアを計算し、達成率に応じてランクを付与します。

```
AAA  ≥ 88.8%
AA   ≥ 77.7%
A    ≥ 66.6%
B    ≥ 55.5%
C    ≥ 44.4%
D    ≥ 33.3%
E    ≥ 22.2%
F    ＜ 22.2%
```

### クリアランプ

プレイ結果は 8 段階のクリアランプで記録されます。

```
FULL COMBO  → EX HARD CLEAR → HARD CLEAR → CLEAR
→ EASY CLEAR → ASSIST CLEAR → DAN CLEAR → FAILED
```

### プレイオプション

選曲画面でリアルタイムに変更できます。

**STYLE（ノーツ配置）**
| オプション | 説明 |
|---|---|
| OFF | 譜面通り |
| RANDOM | レーンをランダムに入れ替え |
| R-RANDOM | 鏡像も含めたランダム |
| S-RANDOM | ノーツ単位でランダム |
| MIRROR | 左右反転 |

**GAUGE（ゲージ種別）**
| オプション | 説明 |
|---|---|
| OFF（NORMAL） | 通常ゲージ |
| A-EASY | 緩やかなアシストイージー |
| EASY | イージーゲージ |
| HARD | ハードゲージ |
| EX-HARD | EXハードゲージ |
| DAN | 段位認定ゲージ |
| HAZARD | 1 ミス即死 |

**ASSIST（アシスト）**
| オプション | 説明 |
|---|---|
| OFF | なし |
| AUTO SCR | スクラッチ自動 |
| LEGACY | レガシーノート判定 |
| 5KEYS | 5 鍵モード |
| ASCR+LEG | AUTO SCR + LEGACY |
| ASCR+5K | AUTO SCR + 5KEYS |
| FULL ASST | 全レーン自動（スコア加算あり） |
| AUTO PLAY | フルオート |

**EX（特殊モード）**
| オプション | 説明 |
|---|---|
| OFF | なし |
| ALL SCR | 全ノーツをスクラッチへ |
| SCR ONLY | スクラッチのみプレイ |
| MORE NOTES | BGM チャンネルをノーツとして追加 |

### 選曲・ソート

**ソート順**：TITLE / LEVEL / CLEAR LAMP / SCORE / BPM / ARTIST / NOTES 数

**仮想フォルダ**（ON/OFF 切替可能）
- LEVEL フォルダ（レベル別）
- LAMP フォルダ（クリアランプ別）
- RANK フォルダ（ランク別）
- TYPE フォルダ（難易度種別別）
- NOTES フォルダ（ノーツ数範囲別）
- ALPHA フォルダ（アルファベット別）

### 2P 対応

- 1P / 2P の独立したキーコンフィグ
- PLAY SIDE 切替（レーンを左右どちらに配置するか）

---

## 設定（オプション画面）

| 項目 | 説明 |
|---|---|
| GREEN NUMBER | ノーツが判定ラインに到達するまでの時間（ms） |
| SUDDEN+ | 画面上部にレーンカバーを追加 |
| LIFT | 画面下部のレーンを持ち上げる量 |
| LANE WIDTH | 白鍵レーンの幅（px） |
| SCRATCH WIDTH | スクラッチレーンの幅（px） |
| JUDGE OFFSET | 判定タイミングの補正（ms、±200 ms） |
| VISUAL OFFSET | ノーツの描画位置補正（px、±200 px） |
| FAST / SLOW 表示 | OFF / ON / ms 付き表示 |
| GAUGE DISPLAY | 1% ステップ / 2% ステップ（IIDX 風） |
| MORE NOTES COUNT | MORE NOTES モードで追加するノーツ数 |
| BOMB DURATION | ボムエフェクトの表示時間（ms） |
| BOMB SIZE | ボムエフェクトのサイズ |
| START UP SCREEN | 起動時の画面（TITLE / SELECT） |

---

## キーコンフィグ

オプション画面からジョイスティックの各ボタンを自由に割り当て可能です。

- **1P キーコンフィグ**：START / EFFECT / LANE 1〜7 / SCRATCH A・B
- **2P キーコンフィグ**：START / LANE 1〜7 / SCRATCH A・B
- **システムキーコンフィグ**：DECIDE / BACK / UP / DOWN / LEFT / RIGHT / OPTION / DIFF / SORT / RANDOM

---

### 使用ライブラリ

- [SDL2](https://www.libsdl.org/)
- [SDL2_mixer](https://github.com/libsdl-org/SDL_mixer)
- [SDL2_ttf](https://github.com/libsdl-org/SDL_ttf)
- [SDL2_image](https://github.com/libsdl-org/SDL_image)
- [FFmpeg](https://ffmpeg.org/)
- [libnx](https://github.com/switchbrew/libnx)
- [nlohmann/json](https://github.com/nlohmann/json)
