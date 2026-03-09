#---------------------------------------------------------------------------------
# .nroを作成するための Makefile (FFmpeg対応版 / 並列フルビルド)
#
# 使い方:
#   make rebuild     -- clean してから並列ビルド (通常はこれ)
#   make -j$(nproc)  -- 並列ビルドのみ (cleanなし)
#   make clean       -- ビルド成果物を削除
#
# "Nothing to be done" 問題について:
#   .d依存追跡を廃止しているため、オブジェクトが残っていると
#   makeがソース変更を検知できない。毎回 make rebuild で解決。
#---------------------------------------------------------------------------------
TARGET      := sdl2_red_square
BUILD       := build
OUTPUT      := $(BUILD)/$(TARGET)
SOURCES     := main.cpp BmsonLoader.cpp SoundManager.cpp NoteRenderer.cpp \
               SceneSelect.cpp ScenePlay.cpp SceneResult.cpp PlayEngine.cpp ScoreManager.cpp \
               SceneTitle.cpp SceneDecision.cpp SceneSelectView.cpp SongManager.cpp \
               ChartProjector.cpp JudgeManager.cpp SceneOption.cpp SceneModeSelect.cpp \
               SceneSideSelect.cpp VirtualFolderManager.cpp BgaManager.cpp BmsLoader.cpp \
               Scene2PDiffSelect.cpp

# --- devkitProのパス設定 (自動取得) ---
ifeq ($(strip $(DEVKITPRO)),)
$(error "DEVKITPRO environment variable is not set. Please restart your terminal.")
endif

DEVKITA64   := $(DEVKITPRO)/devkitA64
LIBNX       := $(DEVKITPRO)/libnx
PORTLIBS    := $(DEVKITPRO)/portlibs/switch

# --- ツール類のパスを指定 ---
CC      := $(DEVKITA64)/bin/aarch64-none-elf-g++
ELF2NRO := $(DEVKITPRO)/tools/bin/elf2nro
NACPTOOL:= $(DEVKITPRO)/tools/bin/nacptool

# --- アイコンなどの設定 ---
ICON        := $(LIBNX)/default_icon.jpg
NACP        := $(OUTPUT).nacp
NACP_TITLE  := "SDL2 Red Square"
NACP_AUTHOR := "User"
NACP_VERSION:= "1.0.0"

# --- コンパイルオプション ---
CFLAGS  := -Wall -Os -ffunction-sections -fdata-sections -march=armv8-a -mtune=cortex-a57 -mtp=soft -fPIE
CFLAGS  += -D__SWITCH__
CFLAGS  += -I$(PORTLIBS)/include -I$(LIBNX)/include
CFLAGS  += -I$(PORTLIBS)/include/SDL2
CFLAGS  += -I$(PORTLIBS)/include/SDL2_mixer
CFLAGS  += -I$(PORTLIBS)/include/SDL2_ttf
CFLAGS  += -I.

# --- リンクオプション ---
LDFLAGS := -specs=$(LIBNX)/switch.specs -march=armv8-a -mtune=cortex-a57 -fPIE
LDFLAGS += -L$(PORTLIBS)/lib -L$(LIBNX)/lib
LDFLAGS += -Wl,--start-group \
    -lavformat -lavcodec -lswscale -lswresample -lavutil \
    -ldav1d \
    -lSDL2 -lSDL2_image -lSDL2_mixer -lSDL2_ttf \
    -lmodplug -lmpg123 -lvorbisfile -lopusfile -lvorbis -lopus -logg \
    -lfreetype -lharfbuzz -lbz2 -lpng -ljpeg -lwebp -lz \
    -Wl,--end-group
LDFLAGS += -lEGL -lglapi -ldrm_nouveau -lnx -lm -lpthread

# --- 並列数: 環境変数 JOBS で上書き可能、デフォルトは論理コア数 ---
JOBS ?= $(shell nproc 2>/dev/null || echo 4)

# --- オブジェクトファイル ---
OBJS := $(addprefix $(BUILD)/, $(SOURCES:.cpp=.o))

.PHONY: all rebuild clean

all: $(OUTPUT).nro

# --- rebuild: clean → 並列フルビルド (通常の使い方) ---
rebuild:
	$(MAKE) clean
	$(MAKE) -j$(JOBS) all

# --- リンク ---
$(OUTPUT).elf: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# --- 個別コンパイル ---
$(BUILD)/%.o: %.cpp
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<

# --- NACP / NRO ---
$(NACP):
	$(NACPTOOL) --create $(NACP_TITLE) $(NACP_AUTHOR) $(NACP_VERSION) $@

$(OUTPUT).nro: $(OUTPUT).elf $(NACP)
	$(ELF2NRO) $< $@ --icon=$(ICON) --nacp=$(NACP)

clean:
	@rm -rf $(BUILD)