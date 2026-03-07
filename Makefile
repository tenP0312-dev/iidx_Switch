#---------------------------------------------------------------------------------
# .nroを作成するための Makefile (FFmpeg対応版 / 個別コンパイル)
#---------------------------------------------------------------------------------
TARGET      := sdl2_red_square
BUILD       := build
OUTPUT      := $(BUILD)/$(TARGET)
SOURCES     := main.cpp BmsonLoader.cpp SoundManager.cpp NoteRenderer.cpp \
               SceneSelect.cpp ScenePlay.cpp SceneResult.cpp PlayEngine.cpp ScoreManager.cpp \
               SceneTitle.cpp SceneDecision.cpp SceneSelectView.cpp SongManager.cpp \
               ChartProjector.cpp JudgeManager.cpp SceneOption.cpp SceneModeSelect.cpp \
               SceneSideSelect.cpp VirtualFolderManager.cpp BgaManager.cpp

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
CFLAGS  := -g -Wall -Os -ffunction-sections -fdata-sections -march=armv8-a -mtune=cortex-a57 -mtp=soft -fPIE
CFLAGS  += -D__SWITCH__
CFLAGS  += -I$(PORTLIBS)/include -I$(LIBNX)/include
CFLAGS  += -I$(PORTLIBS)/include/SDL2
CFLAGS  += -I$(PORTLIBS)/include/SDL2_mixer
CFLAGS  += -I$(PORTLIBS)/include/SDL2_ttf
CFLAGS  += -I.

# --- リンクオプション ---
LDFLAGS := -specs=$(LIBNX)/switch.specs -g -march=armv8-a -mtune=cortex-a57 -fPIE
LDFLAGS += -L$(PORTLIBS)/lib -L$(LIBNX)/lib
LDFLAGS += -Wl,--start-group \
    -lavformat -lavcodec -lswscale -lswresample -lavutil \
    -ldav1d \
    -lSDL2 -lSDL2_image -lSDL2_mixer -lSDL2_ttf \
    -lmodplug -lmpg123 -lvorbisfile -lopusfile -lvorbis -lopus -logg \
    -lfreetype -lharfbuzz -lbz2 -lpng -ljpeg -lwebp -lz \
    -Wl,--end-group
LDFLAGS += -lEGL -lglapi -ldrm_nouveau -lnx -lm -lpthread

# --- 個別コンパイル用の設定 ---
# .cpp → build/.o に変換
OBJS := $(addprefix $(BUILD)/, $(SOURCES:.cpp=.o))

# ヘッダの依存関係ファイル (.d) も build/ に生成
# -MMD: システムヘッダを除いた依存関係を .d ファイルに書き出す
# -MP:  ヘッダが削除された時にエラーにならないようにする
DEPFLAGS = -MMD -MP
DEPS := $(OBJS:.o=.d)

.PHONY: all clean

all: $(OUTPUT).nro

# --- リンク ---
$(OUTPUT).elf: $(OBJS)
	@echo "Linking..."
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# --- 個別コンパイル ---
# build/foo.o: foo.cpp (+ 依存ヘッダは .d から自動追跡)
$(BUILD)/%.o: %.cpp
	@mkdir -p $(BUILD)
	@echo "Compiling $<..."
	$(CC) $(CFLAGS) $(DEPFLAGS) -c -o $@ $<

# --- 依存関係ファイルをインクルード ---
# ヘッダを変更した時に関連する .o だけ再ビルドされる
-include $(DEPS)

# --- NACP / NRO ---
$(NACP):
	@echo "Creating NACP..."
	$(NACPTOOL) --create $(NACP_TITLE) $(NACP_AUTHOR) $(NACP_VERSION) $@

$(OUTPUT).nro: $(OUTPUT).elf $(NACP)
	@echo "Creating NRO..."
	$(ELF2NRO) $< $@ --icon=$(ICON) --nacp=$(NACP)
	@echo "Success! Output is in: $(OUTPUT).nro"

clean:
	@echo "Cleaning..."
	@rm -rf $(BUILD)
