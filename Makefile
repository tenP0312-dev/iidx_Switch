#---------------------------------------------------------------------------------
# Nintendo Switch (nro) & macOS 共通 Makefile
#---------------------------------------------------------------------------------
TARGET      := GeminiRhythm
BUILD       := build
SOURCES     := main.cpp BmsonLoader.cpp SoundManager.cpp NoteRenderer.cpp \
               SceneSelect.cpp ScenePlay.cpp SceneResult.cpp PlayEngine.cpp ScoreManager.cpp \
               SceneTitle.cpp SceneDecision.cpp SceneSelectView.cpp SongManager.cpp \
               ChartProjector.cpp JudgeManager.cpp SceneOption.cpp SceneModeSelect.cpp \
               SceneSideSelect.cpp VirtualFolderManager.cpp BgaManager.cpp BmsLoader.cpp \
               Scene2PDiffSelect.cpp

# 並列ビルド数の設定
JOBS        ?= $(shell sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)

# オブジェクトファイルと依存関係ファイル
OBJS        := $(addprefix $(BUILD)/, $(SOURCES:.cpp=.o))
DEPS        := $(OBJS:.o=.d)

# 基本設定
CXXFLAGS    := -Wall -O2 -MMD -MP -I.
LDFLAGS     := 

# --- macOS 設定 (Homebrew パス自動検知) ---
ifeq ($(OS),Windows_NT)
    # Windows非対応
else
    UNAME_S := $(shell uname -s)
    ifeq ($(UNAME_S),Darwin)
        BREW_PREFIX := $(shell brew --prefix)
        MAC_CXX      := clang++
        MAC_CXXFLAGS := $(CXXFLAGS) -std=c++20 \
                        -I$(BREW_PREFIX)/include \
                        -I$(BREW_PREFIX)/include/SDL2
        MAC_LDFLAGS  := -L$(BREW_PREFIX)/lib \
                        -lSDL2 -lSDL2_mixer -lSDL2_ttf -lSDL2_image \
                        -lavformat -lavcodec -lswscale -lavutil \
                        -framework Cocoa -framework AudioToolbox -framework CoreAudio
    endif
endif

# --- Switch 設定 (devkitPro) ---
ifeq ($(strip $(DEVKITPRO)),)
    # Switch環境変数が無い場合は警告
else
    DEVKITA64 := $(DEVKITPRO)/devkitA64
    LIBNX     := $(DEVKITPRO)/libnx
    PORTLIBS  := $(DEVKITPRO)/portlibs/switch
    
    SW_CXX      := $(DEVKITA64)/bin/aarch64-none-elf-g++
    SW_CXXFLAGS := $(CXXFLAGS) -march=armv8-a -mtune=cortex-a57 -mtp=soft -fPIE -D__SWITCH__ \
                   -I$(PORTLIBS)/include -I$(LIBNX)/include -I$(PORTLIBS)/include/SDL2
    SW_LDFLAGS  := -specs=$(LIBNX)/switch.specs -march=armv8-a -mtune=cortex-a57 -fPIE \
                   -L$(PORTLIBS)/lib -L$(LIBNX)/lib \
                   -Wl,--start-group \
                   -lavformat -lavcodec -lswscale -lswresample -lavutil -ldav1d \
                   -lSDL2 -lSDL2_image -lSDL2_mixer -lSDL2_ttf \
                   -lmodplug -lmpg123 -lvorbisfile -lopusfile -lvorbis -lopus -logg \
                   -lfreetype -lharfbuzz -lbz2 -lpng -ljpeg -lwebp -lz \
                   -Wl,--end-group -lEGL -lglapi -ldrm_nouveau -lnx -lm -lpthread

    ELF2NRO     := $(DEVKITPRO)/tools/bin/elf2nro
    NACPTOOL    := $(DEVKITPRO)/tools/bin/nacptool
    ICON        := $(LIBNX)/default_icon.jpg
endif

#---------------------------------------------------------------------------------
# ターゲット別ビルドルール
#---------------------------------------------------------------------------------

.PHONY: all switch mac clean rebuild

# デフォルトは Switch 版
all: switch

# --- Switch 版ビルド ---
switch:
	@mkdir -p $(BUILD)
	$(MAKE) -j$(JOBS) $(BUILD)/$(TARGET).nro CXX="$(SW_CXX)" CXXFLAGS="$(SW_CXXFLAGS)" LDFLAGS="$(SW_LDFLAGS)"

# --- Mac 版ビルド ---
mac:
	@mkdir -p $(BUILD)
	$(MAKE) -j$(JOBS) $(BUILD)/$(TARGET)_mac CXX="$(MAC_CXX)" CXXFLAGS="$(MAC_CXXFLAGS)" LDFLAGS="$(MAC_LDFLAGS)"

# 実行ファイル生成 (Switch ELF)
$(BUILD)/$(TARGET).elf: $(OBJS)
	$(CXX) -o $@ $^ $(LDFLAGS)

# NRO 生成
$(BUILD)/$(TARGET).nro: $(BUILD)/$(TARGET).elf
	$(NACPTOOL) --create "GeminiRhythm" "User" "1.0.0" $(BUILD)/$(TARGET).nacp
	$(ELF2NRO) $< $@ --icon=$(ICON) --nacp=$(BUILD)/$(TARGET).nacp

# 実行ファイル生成 (Mac)
$(BUILD)/$(TARGET)_mac: $(OBJS)
	$(CXX) -o $@ $^ $(LDFLAGS)

# コンパイルルール
$(BUILD)/%.o: %.cpp
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

clean:
	rm -rf $(BUILD)

rebuild:
	$(MAKE) clean
	$(MAKE) all

# 依存関係ファイルの読み込み（これでヘッダー変更が検知される）
-include $(DEPS)