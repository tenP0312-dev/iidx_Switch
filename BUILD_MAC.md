# Mac ビルド手順

## 1. 依存パッケージのインストール

```bash
brew install sdl2 sdl2_mixer sdl2_ttf sdl2_image ffmpeg cmake
```

## 2. ビルド

```bash
mkdir build && cd build
cmake ..
make -j$(sysctl -n hw.logicalcpu)
```

## 3. 実行

```bash
# ビルドしたバイナリと同じフォルダに以下を置く
#   font.ttf
#   BMS/        ← BMSフォルダ
#   scores/     ← 自動作成されるが手動でも可

./GeminiRhythm
```

## ファイル構成 (実行フォルダ)

```
GeminiRhythm        ← バイナリ
font.ttf
BMS/
  曲フォルダ/
    *.bmson
    *.boxwav
scores/
```

## トラブルシュート

**SDL2が見つからない場合**
```bash
brew install sdl2
```

**FFmpegが見つからない場合**
```bash
brew install ffmpeg
```

**M1/M2 Macでアーキテクチャエラーが出る場合**
```bash
cmake .. -DCMAKE_OSX_ARCHITECTURES=arm64
```

**Intel Macの場合**
```bash
cmake .. -DCMAKE_OSX_ARCHITECTURES=x86_64
```
