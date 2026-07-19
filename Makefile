#---------------------------------------------------------------------------------
.SUFFIXES:
#---------------------------------------------------------------------------------

# Parallel builds by default (override: make -j4 …  or  make JOBS=8 …)
JOBS ?= $(shell nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)
ifeq ($(filter -j%,$(MAKEFLAGS))$(filter --jobserver-%,$(MAKEFLAGS)),)
MAKEFLAGS += -j$(JOBS)
endif

# Host unit tests / coverage — also runs as a dependency of `make` / `make cia`.
# Standalone: `make coverage`, `make coverage-html`, or `make test-host`
# Bypass during a 3DS build: `make SKIP_COVERAGE=1` / `make cia SKIP_COVERAGE=1`
.PHONY: coverage coverage-html test-host
coverage:
	@$(MAKE) -C $(CURDIR)/tests coverage

coverage-html:
	@$(MAKE) -C $(CURDIR)/tests coverage-html

test-host:
	@$(MAKE) -C $(CURDIR)/tests test

# Load the 3DS toolchain only when building the app (default `make`, or any
# non-host goal). Pure host goals skip this entirely.
ifeq ($(MAKECMDGOALS),)
NEED_3DS := 1
else ifneq ($(filter-out coverage coverage-html test-host,$(MAKECMDGOALS)),)
NEED_3DS := 1
endif

ifdef NEED_3DS

ifeq ($(strip $(DEVKITARM)),)
$(error "Please set DEVKITARM in your environment. export DEVKITARM=<path to devkitARM>")
endif

TOPDIR ?= $(CURDIR)
include $(DEVKITARM)/3ds_rules
# 3ds_rules (and base_rules) add pattern targets first; without this, `make` does not
# run our `all` and never builds the .3dsx.
.DEFAULT_GOAL := all

#---------------------------------------------------------------------------------
# TARGET is the base name of output files (e.g. szmy.3dsx) in the project root.
# When the sub-make runs in build/, notdir(CURDIR) is "build" — do not set TARGET=build.
# Optional override: TARGET := myname  (else defaults to the project folder name, e.g. szmy)
# BUILD is the directory where object files & intermediate files will be placed
# SOURCES is a list of directories containing source code
# DATA is a list of directories containing data files
# INCLUDES is a list of directories containing header files
# GRAPHICS is a list of directories containing graphics files
# GFXBUILD is the directory where converted graphics files will be placed
#
# APP_TITLE is the name of the app stored in the SMDH file (Optional)
# APP_DESCRIPTION is the description of the app stored in the SMDH file (Optional)
# APP_AUTHOR is the author of the app stored in the SMDH file (Optional).
# Short form of Czyrus Tuazon (CyT / zaccken); keep this brief for the Home Menu.
# ICON is the filename of the icon (.png), relative to the project folder.
# BANNER_IMAGE / BANNER_AUDIO: Home Menu top-screen banner (256x128 PNG + WAV/OGG).
#---------------------------------------------------------------------------------
# Derive app basename: .../myproj (TARGET=myproj) or .../myproj/build in sub-make (still myproj, not "build")
__HERE := $(notdir $(CURDIR))
ifeq ($(__HERE),build)
TARGET := $(notdir $(patsubst %/build,%,$(abspath .)))
else
TARGET := $(__HERE)
endif
BUILD := build
SOURCES := source
DATA := data
INCLUDES := include
GRAPHICS := gfx
GFXBUILD := $(BUILD)

APP_TITLE := SZMY Music Player
APP_DESCRIPTION := Play MP3, FLAC, Opus, WAV, BRSTM and more from your SD card.
APP_AUTHOR := CyT

# CIA / title version (makerom -major/-minor/-micro). RemasterVersion is in szmy.rsf.
APP_VERSION_MAJOR := 1
APP_VERSION_MINOR := 0
APP_VERSION_MICRO := 0

# Home Menu banner (top screen). Override: BANNER_IMAGE=... BANNER_AUDIO=...
BANNER_IMAGE ?= banner.png
BANNER_AUDIO ?= banner.wav
BANNERTOOL ?=

#---------------------------------------------------------------------------------
# options for code generation
#---------------------------------------------------------------------------------
ARCH := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft

CFLAGS := -g -Wall -O2 -mword-relocations \
	-ffunction-sections \
	$(ARCH)

CFLAGS += $(INCLUDE) -D__3DS__

CXXFLAGS := $(CFLAGS) -fno-rtti -fno-exceptions -std=gnu++11

ASFLAGS := -g $(ARCH)
LDFLAGS = -specs=3dsx.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)

LIBS := -lvgmstream -lctru -lm

#---------------------------------------------------------------------------------
# vgmstream: multi-format audio decoding (WAV, BRSTM, ADPCM, etc.)
#---------------------------------------------------------------------------------
VGMSTREAM_DIR := $(TOPDIR)/vgmstream
VGMSTREAM_LIB := $(VGMSTREAM_DIR)/build-3ds/libvgmstream.a

#---------------------------------------------------------------------------------
# Optional portlibs: mpg123 (vgmstream MPEG) and opusfile (dedicated Opus player).
# Install: dkp-pacman -S 3ds-mpg123 3ds-opusfile
#---------------------------------------------------------------------------------
DEVKITPRO ?= $(patsubst %/devkitARM,%,$(DEVKITARM))
PORTLIBS_3DS := $(DEVKITPRO)/portlibs/3ds
MPG123_LIB   := $(PORTLIBS_3DS)/lib/libmpg123.a
OPUSFILE_LIB := $(PORTLIBS_3DS)/lib/libopusfile.a

LIBDIRS := $(CTRULIB)
LIBS := -lvgmstream -lcitro2d -lcitro3d -lctru -lm

ifneq ($(wildcard $(MPG123_LIB)),)
  ENABLE_MP3 := 1
endif
ifneq ($(wildcard $(OPUSFILE_LIB)),)
  ENABLE_OPUS := 1
  # pkg-config opusfile: headers live under include/opus/
  CFLAGS += -DENABLE_OPUS -I$(PORTLIBS_3DS)/include/opus
endif

ifneq ($(ENABLE_MP3)$(ENABLE_OPUS),)
  LIBDIRS := $(CTRULIB) $(PORTLIBS_3DS)
  LIBS := -lvgmstream
  ifneq ($(ENABLE_MP3),)
    LIBS += -lmpg123
  endif
  ifneq ($(ENABLE_OPUS),)
    LIBS += -lopusfile -lopus -logg
  endif
  LIBS += -lcitro2d -lcitro3d -lctru -lm
endif

#---------------------------------------------------------------------------------
# list of directories containing libraries
#---------------------------------------------------------------------------------
# CIA: `make cia` — not part of default `all`. makerom needs exactly $(TARGET).rsf (e.g. szmy.rsf).
# Override: CIA_RSF := path/to/title.rsf
# Keep UniqueId and memory settings in that file stable so Home Menu / installs don’t desync.
#---------------------------------------------------------------------------------
CIA_RSF ?= $(TARGET).rsf

#---------------------------------------------------------------------------------
# no real need to edit anything past this point unless you need to add additional
# rules for different file extensions
#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))
#---------------------------------------------------------------------------------

export OUTPUT := $(CURDIR)/$(TARGET)
export TOPDIR := $(CURDIR)

export VPATH := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
	$(foreach dir,$(GRAPHICS),$(CURDIR)/$(dir)) \
	$(foreach dir,$(DATA),$(CURDIR)/$(dir)) \
	$(CURDIR)/$(BUILD)

export DEPSDIR := $(CURDIR)/$(BUILD)

CFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
PICAFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.v.pica)))
SHLISTFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.shlist)))
GFXFILES := $(foreach dir,$(GRAPHICS),$(notdir $(wildcard $(dir)/*.t3s)))
BINFILES := $(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.*)))

#---------------------------------------------------------------------------------
ifeq ($(strip $(CPPFILES)),)
	export LD := $(CC)
else
	export LD := $(CXX)
endif
#---------------------------------------------------------------------------------

#---------------------------------------------------------------------------------
ifeq ($(GFXBUILD),$(BUILD))
	export T3XFILES := $(GFXFILES:.t3s=.t3x)
else
	export ROMFS_T3XFILES := $(patsubst %.t3s, $(GFXBUILD)/%.t3x, $(GFXFILES))
	export T3XHFILES := $(patsubst %.t3s, $(BUILD)/%.h, $(GFXFILES))
endif
#---------------------------------------------------------------------------------

export OFILES_SOURCES := $(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)

export OFILES_BIN := $(addsuffix .o,$(BINFILES)) \
	$(PICAFILES:.v.pica=.shbin.o) $(SHLISTFILES:.shlist=.shbin.o) \
	$(addsuffix .o,$(T3XFILES))

# Embedded top BG: 400x240 from root up.png. Bottom: 320x240 from root bottom.png.
# Plus the generic now-playing icon (root generic_music.png @ 96x96).
export GFX_EMBED      := top_screen_bg_embed.bmp.o bottom_screen_bg_embed.bmp.o \
	generic_music_embed.bmp.o
export OFILES         := $(OFILES_BIN) $(OFILES_SOURCES) $(GFX_EMBED)

export HFILES := $(PICAFILES:.v.pica=_shbin.h) $(SHLISTFILES:.shlist=_shbin.h) \
	$(addsuffix .h,$(subst .,_,$(BINFILES))) \
	$(GFXFILES:.t3s=.h)

export INCLUDE := $(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
	$(foreach dir,$(LIBDIRS),-I$(dir)/include) \
	-I$(VGMSTREAM_DIR)/src \
	-I$(VGMSTREAM_DIR)/src/coding/libs \
	-I$(CURDIR)/$(BUILD)

export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib) -L$(VGMSTREAM_DIR)/build-3ds

export _3DSXDEPS := $(if $(NO_SMDH),,$(OUTPUT).smdh)

ifeq ($(strip $(ICON)),)
	icons := $(wildcard *.png)
	ifneq (,$(findstring $(TARGET).png,$(icons)))
		export APP_ICON := $(TOPDIR)/$(TARGET).png
	else
		ifneq (,$(findstring icon.png,$(icons)))
			export APP_ICON := $(TOPDIR)/icon.png
		endif
	endif
else
	export APP_ICON := $(TOPDIR)/$(ICON)
endif

ifeq ($(strip $(NO_SMDH)),)
	# smdh in project root; do not use CURDIR (wrong when sub-make runs from build/)
	export _3DSXFLAGS += --smdh=$(TOPDIR)/$(TARGET).smdh
endif

ifneq ($(ROMFS),)
	export _3DSXFLAGS += --romfs=$(TOPDIR)/$(ROMFS)
endif

# PNG → embed BMP. Prefer ImageMagick / Pillow; always fall back to checked-in
# gfx/*.bmp so a fresh clone rebuilds without converters (MSYS often has Windows
# convert.exe on PATH, which is not ImageMagick and must not be used).
# $(1)=label $(2)=src png $(3)=out bmp $(4)=WxH $(5)=fallback bmp
define PNG_TO_EMBED_BMP
	@echo $(1): build embed $(4) from $(notdir $(2)) ...
	@out="$(3)"; src="$(2)"; fb="$(5)"; ok=0; \
	rm -f "$$out"; \
	if command -v magick >/dev/null 2>&1; then \
		magick "$$src" -strip -filter Triangle -resize "$(4)!" -depth 8 "$$out" \
			&& ok=1; \
	fi; \
	if [ "$$ok" != 1 ] && command -v convert >/dev/null 2>&1 \
		&& convert -version 2>&1 | grep -qi ImageMagick; then \
		convert "$$src" -strip -filter Triangle -resize "$(4)!" -depth 8 "$$out" \
			&& ok=1; \
	fi; \
	if [ "$$ok" != 1 ]; then \
		py=; command -v python3 >/dev/null 2>&1 && py=python3; \
		[ -z "$$py" ] && command -v python >/dev/null 2>&1 && py=python; \
		if [ -n "$$py" ]; then \
			w=$$(echo "$(4)" | cut -dx -f1); h=$$(echo "$(4)" | cut -dx -f2); \
			$$py -c "from PIL import Image; Image.open(r'$$src').convert('RGB').resize(($$w,$$h)).save(r'$$out','BMP')" \
				&& ok=1 && echo "  (Pillow -> $$out)"; \
		fi; \
	else \
		echo "  (ImageMagick -> $$out)"; \
	fi; \
	if [ "$$ok" != 1 ] || [ ! -s "$$out" ]; then \
		echo "  (fallback: $$fb)" >&2; \
		cp -f "$$fb" "$$out"; \
	fi
endef

# 400x240 = top (up.png); 320x240 = bottom (bottom-clean.png).
TOP_BG_SRC    := $(CURDIR)/up.png
TOP_BG_EMBED  := $(CURDIR)/$(BUILD)/top_screen_bg_embed.bmp
BOT_BG_SRC    := $(CURDIR)/bottom-clean.png
BOT_BG_EMBED  := $(CURDIR)/$(BUILD)/bottom_screen_bg_embed.bmp
GEN_ICON_SRC   := $(CURDIR)/generic_music.png
GEN_ICON_EMBED := $(CURDIR)/$(BUILD)/generic_music_embed.bmp

$(TOP_BG_EMBED): $(TOP_BG_SRC) $(CURDIR)/gfx/top_screen_bg.bmp | $(BUILD)
	$(call PNG_TO_EMBED_BMP,top_screen_bg,$(TOP_BG_SRC),$(TOP_BG_EMBED),400x240,$(CURDIR)/gfx/top_screen_bg.bmp)

$(BOT_BG_EMBED): $(BOT_BG_SRC) $(CURDIR)/gfx/bottom_screen_bg.bmp | $(BUILD)
	$(call PNG_TO_EMBED_BMP,bottom_screen_bg,$(BOT_BG_SRC),$(BOT_BG_EMBED),320x240,$(CURDIR)/gfx/bottom_screen_bg.bmp)

$(GEN_ICON_EMBED): $(GEN_ICON_SRC) $(CURDIR)/gfx/generic_music.bmp | $(BUILD)
	$(call PNG_TO_EMBED_BMP,generic_music,$(GEN_ICON_SRC),$(GEN_ICON_EMBED),96x96,$(CURDIR)/gfx/generic_music.bmp)

# Bottom play/pause: 1% black transparent, trim, cap 32px, tile 32x32 BMP4 (half of prior 64).
# Unchanged magick strategy per request; 24-bit fallback still uses alpha_key() in C.
$(CURDIR)/$(BUILD)/play_active.bmp: $(CURDIR)/gfx/play_active.bmp | $(BUILD)
	@echo "btn play_active (alpha, trim, <=32) ..."
	@in="$(CURDIR)/gfx/play_active.bmp"; out="$(CURDIR)/$(BUILD)/play_active.bmp"; \
	if command -v magick >/dev/null 2>&1; then \
		magick "$$in" -strip -fuzz 1% -transparent black -trim +repage -resize "32x32>" \
			-gravity center -background none -extent 32x32 -define bmp:format=bmp4 "BMP4:$$out" 2>/dev/null || cp -f "$$in" "$$out"; \
	elif command -v convert >/dev/null 2>&1; then \
		convert "$$in" -strip -fuzz 1% -transparent black -trim +repage -resize "32x32>" \
			-gravity center -background none -extent 32x32 -define bmp:format=bmp4 "BMP4:$$out" 2>/dev/null || cp -f "$$in" "$$out"; \
	else cp -f "$$in" "$$out"; fi

$(CURDIR)/$(BUILD)/play_inactive.bmp: $(CURDIR)/gfx/play_inactive.bmp | $(BUILD)
	@echo "btn play_inactive ..."
	@in="$(CURDIR)/gfx/play_inactive.bmp"; out="$(CURDIR)/$(BUILD)/play_inactive.bmp"; \
	if command -v magick >/dev/null 2>&1; then \
		magick "$$in" -strip -fuzz 1% -transparent black -trim +repage -resize "32x32>" \
			-gravity center -background none -extent 32x32 -define bmp:format=bmp4 "BMP4:$$out" 2>/dev/null || cp -f "$$in" "$$out"; \
	elif command -v convert >/dev/null 2>&1; then \
		convert "$$in" -strip -fuzz 1% -transparent black -trim +repage -resize "32x32>" \
			-gravity center -background none -extent 32x32 -define bmp:format=bmp4 "BMP4:$$out" 2>/dev/null || cp -f "$$in" "$$out"; \
	else cp -f "$$in" "$$out"; fi

$(CURDIR)/$(BUILD)/pause_active.bmp: $(CURDIR)/gfx/pause_active.bmp | $(BUILD)
	@echo "btn pause_active ..."
	@in="$(CURDIR)/gfx/pause_active.bmp"; out="$(CURDIR)/$(BUILD)/pause_active.bmp"; \
	if command -v magick >/dev/null 2>&1; then \
		magick "$$in" -strip -fuzz 1% -transparent black -trim +repage -resize "32x32>" \
			-gravity center -background none -extent 32x32 -define bmp:format=bmp4 "BMP4:$$out" 2>/dev/null || cp -f "$$in" "$$out"; \
	elif command -v convert >/dev/null 2>&1; then \
		convert "$$in" -strip -fuzz 1% -transparent black -trim +repage -resize "32x32>" \
			-gravity center -background none -extent 32x32 -define bmp:format=bmp4 "BMP4:$$out" 2>/dev/null || cp -f "$$in" "$$out"; \
	else cp -f "$$in" "$$out"; fi

$(CURDIR)/$(BUILD)/pause_inactive.bmp: $(CURDIR)/gfx/pause_inactive.bmp | $(BUILD)
	@echo "btn pause_inactive ..."
	@in="$(CURDIR)/gfx/pause_inactive.bmp"; out="$(CURDIR)/$(BUILD)/pause_inactive.bmp"; \
	if command -v magick >/dev/null 2>&1; then \
		magick "$$in" -strip -fuzz 1% -transparent black -trim +repage -resize "32x32>" \
			-gravity center -background none -extent 32x32 -define bmp:format=bmp4 "BMP4:$$out" 2>/dev/null || cp -f "$$in" "$$out"; \
	elif command -v convert >/dev/null 2>&1; then \
		convert "$$in" -strip -fuzz 1% -transparent black -trim +repage -resize "32x32>" \
			-gravity center -background none -extent 32x32 -define bmp:format=bmp4 "BMP4:$$out" 2>/dev/null || cp -f "$$in" "$$out"; \
	else cp -f "$$in" "$$out"; fi

BTN_BMPS := $(CURDIR)/$(BUILD)/play_active.bmp $(CURDIR)/$(BUILD)/play_inactive.bmp \
	$(CURDIR)/$(BUILD)/pause_active.bmp $(CURDIR)/$(BUILD)/pause_inactive.bmp

#---------------------------------------------------------------------------------
$(VGMSTREAM_LIB):
	@$(MAKE) -C $(VGMSTREAM_DIR) -f Makefile.3ds PORTLIBS_3DS="$(PORTLIBS_3DS)" ENABLE_MP3="$(ENABLE_MP3)"

#---------------------------------------------------------------------------------
# Coverage gate first (fail fast), then parallel 3DS build via MAKEFLAGS -j.
# Skip coverage with: make SKIP_COVERAGE=1 …
.PHONY: all 3ds-all clean cia

all:
ifndef SKIP_COVERAGE
	@$(MAKE) -C $(CURDIR)/tests coverage
endif
	@$(MAKE) 3ds-all

# Use the full path for the t3x (not bare $(T3XFILES)) so we hit the rule that
# depends on $(TOP_BG_EMBED), not the generic 3ds_rules %.t3x pattern.
3ds-all: $(BUILD) $(GFXBUILD) $(DEPSDIR) $(TOP_BG_EMBED) $(BOT_BG_EMBED) $(GEN_ICON_EMBED) $(BTN_BMPS) $(GFXBUILD)/top_screen_bg.t3x $(VGMSTREAM_LIB)
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile all

#---------------------------------------------------------------------------------
# Banner: bannertool makebanner → banner.bin (requires tools/bannertool[.exe] or PATH)
# Get Windows/Linux builds: https://github.com/carstene1ns/3ds-bannertool/releases
#---------------------------------------------------------------------------------
APP_BANNER := $(TOPDIR)/banner.bin

define FIND_BANNERTOOL
	bt="$(BANNERTOOL)"; \
	if [ -z "$$bt" ]; then \
		if [ -x "$(TOPDIR)/tools/bannertool.exe" ]; then bt="$(TOPDIR)/tools/bannertool.exe"; \
		elif [ -x "$(TOPDIR)/tools/bannertool" ]; then bt="$(TOPDIR)/tools/bannertool"; \
		elif command -v bannertool >/dev/null 2>&1; then bt="bannertool"; \
		else \
			echo "Error: bannertool not found."; \
			echo "  Put bannertool in PATH, or at tools/bannertool.exe"; \
			echo "  https://github.com/carstene1ns/3ds-bannertool/releases"; \
			exit 1; \
		fi; \
	fi
endef

$(APP_BANNER): $(TOPDIR)/$(BANNER_IMAGE) $(TOPDIR)/$(BANNER_AUDIO)
	@echo building banner.bin from $(BANNER_IMAGE)
	@$(FIND_BANNERTOOL); \
	"$$bt" makebanner -i "$(TOPDIR)/$(BANNER_IMAGE)" -a "$(TOPDIR)/$(BANNER_AUDIO)" -o "$(APP_BANNER)"

#---------------------------------------------------------------------------------
# CIA target: build installable .cia (install and launch from Home Menu for New3DS extended RAM)
# Requires: makerom in PATH (e.g. from devkitPro buildtools or 3dstools)
#---------------------------------------------------------------------------------
cia: all $(APP_BANNER)
	@echo building $(OUTPUT).cia
	@echo "  RSF:  $(CIA_RSF)"
	@echo "  elf:  $(OUTPUT).elf"
	@echo "  icon: $(OUTPUT).smdh"
	@echo "  banner: $(APP_BANNER)"
	@test -f "$(CIA_RSF)" || ( echo "Error: RSF not found: $(CIA_RSF). Add it or set CIA_RSF=..." ; exit 1 )
	@test -f "$(OUTPUT).elf" || ( echo "Error: no ELF. Run 'make' first." ; exit 1 )
	@test -f "$(OUTPUT).smdh" || ( echo "Error: missing $(OUTPUT).smdh (run make without NO_SMDH, add icon)."; exit 1 )
	@test -f "$(APP_BANNER)" || ( echo "Error: missing $(APP_BANNER)."; exit 1 )
	@makerom -f cia -o $(OUTPUT).cia -rsf $(CIA_RSF) -target t \
		-elf $(OUTPUT).elf -icon $(OUTPUT).smdh -banner $(APP_BANNER) \
		-major $(APP_VERSION_MAJOR) -minor $(APP_VERSION_MINOR) -micro $(APP_VERSION_MICRO)
	@echo built: $(OUTPUT).cia \(v$(APP_VERSION_MAJOR).$(APP_VERSION_MINOR).$(APP_VERSION_MICRO)\)

$(BUILD):
	@mkdir -p $@

ifneq ($(GFXBUILD),$(BUILD))
$(GFXBUILD):
	@mkdir -p $@
endif

ifneq ($(DEPSDIR),$(BUILD))
$(DEPSDIR):
	@mkdir -p $@
endif

#---------------------------------------------------------------------------------
clean: clean-vgmstream
	@echo clean ...
	@rm -fr $(BUILD) $(OUTPUT).3dsx $(OUTPUT).smdh $(OUTPUT).elf $(OUTPUT).cia $(APP_BANNER) $(GFXBUILD)

clean-vgmstream:
	@$(MAKE) -C $(VGMSTREAM_DIR) -f Makefile.3ds clean 2>/dev/null || true

#---------------------------------------------------------------------------------
$(GFXBUILD)/top_screen_bg.t3x $(BUILD)/top_screen_bg.h : $(CURDIR)/gfx/top_screen_bg.t3s $(TOP_BG_EMBED)
	@echo $(notdir $<)
	@tex3ds -i $< -H $(BUILD)/top_screen_bg.h -d $(DEPSDIR)/top_screen_bg.d -o $(GFXBUILD)/top_screen_bg.t3x

$(GFXBUILD)/%.t3x $(BUILD)/%.h : %.t3s
#---------------------------------------------------------------------------------
	@echo $(notdir $<)
	@tex3ds -i $< -H $(BUILD)/$*.h -d $(DEPSDIR)/$*.d -o $(GFXBUILD)/$*.t3x

#---------------------------------------------------------------------------------
else

# When make runs with cwd=build/, the default goal is not reliably $(OUTPUT).3dsx; `make all` fixes that.
.PHONY: all
all: $(OUTPUT).3dsx

#---------------------------------------------------------------------------------
# main targets
#---------------------------------------------------------------------------------
$(OUTPUT).3dsx : $(OUTPUT).elf $(_3DSXDEPS)

$(OFILES_SOURCES) : $(HFILES)

$(OUTPUT).elf : $(OFILES)

#---------------------------------------------------------------------------------
# Embedded .bmp: bin2o writes top_screen_bg_embed_bmp.h + .o in $(BUILD)
#---------------------------------------------------------------------------------
top_screen_bg_embed.bmp.o: top_screen_bg_embed.bmp
	@echo $(notdir $<)
	@$(bin2o)

bottom_screen_bg_embed.bmp.o: bottom_screen_bg_embed.bmp
	@echo $(notdir $<)
	@$(bin2o)

generic_music_embed.bmp.o: generic_music_embed.bmp
	@echo $(notdir $<)
	@$(bin2o)

topbg.o: top_screen_bg_embed.bmp.o top_screen_bg.t3x.o

#---------------------------------------------------------------------------------
play_active.bmp.o: play_active.bmp
	@echo $(notdir $<)
	@$(bin2o)

play_inactive.bmp.o: play_inactive.bmp
	@echo $(notdir $<)
	@$(bin2o)

pause_active.bmp.o: pause_active.bmp
	@echo $(notdir $<)
	@$(bin2o)

pause_inactive.bmp.o: pause_inactive.bmp
	@echo $(notdir $<)
	@$(bin2o)

botbuttons.o: bottom_screen_bg_embed.bmp.o generic_music_embed.bmp.o

#---------------------------------------------------------------------------------
%.bin.o %_bin.h : %.bin
#---------------------------------------------------------------------------------
	@echo $(notdir $<)
	@$(bin2o)

#---------------------------------------------------------------------------------
.PRECIOUS : %.t3x %.shbin
#---------------------------------------------------------------------------------
%.t3x.o %_t3x.h : %.t3x
#---------------------------------------------------------------------------------
	$(SILENTMSG) $(notdir $<)
	$(bin2o)

#---------------------------------------------------------------------------------
%.shbin.o %_shbin.h : %.shbin
#---------------------------------------------------------------------------------
	$(SILENTMSG) $(notdir $<)
	$(bin2o)

-include $(DEPSDIR)/*.d

#---------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------

endif # NEED_3DS
#---------------------------------------------------------------------------------
