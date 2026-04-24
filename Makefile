#---------------------------------------------------------------------------------
.SUFFIXES:
#---------------------------------------------------------------------------------

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
# Optional override: TARGET := Mp3_Player
# BUILD is the directory where object files & intermediate files will be placed
# SOURCES is a list of directories containing source code
# DATA is a list of directories containing data files
# INCLUDES is a list of directories containing header files
# GRAPHICS is a list of directories containing graphics files
# GFXBUILD is the directory where converted graphics files will be placed
#
# APP_TITLE is the name of the app stored in the SMDH file (Optional)
# APP_DESCRIPTION is the description of the app stored in the SMDH file (Optional)
# APP_AUTHOR is the author of the app stored in the SMDH file (Optional)
# ICON is the filename of the icon (.png), relative to the project folder.
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

APP_TITLE := Hello World
APP_DESCRIPTION := 3DS Homebrew Hello World
APP_AUTHOR := Developer

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
VGMSTREAM_DIR := $(TOPDIR)/vgmstream-master
VGMSTREAM_LIB := $(VGMSTREAM_DIR)/build-3ds/libvgmstream.a

#---------------------------------------------------------------------------------
# MP3: optional, via libmpg123 from devkitPro portlibs. Install: dkp-pacman -S 3ds-mpg123
#---------------------------------------------------------------------------------
DEVKITPRO ?= $(patsubst %/devkitARM,%,$(DEVKITARM))
PORTLIBS_3DS := $(DEVKITPRO)/portlibs/3ds
MPG123_LIB   := $(PORTLIBS_3DS)/lib/libmpg123.a
ifneq ($(wildcard $(MPG123_LIB)),)
  LIBDIRS := $(CTRULIB) $(PORTLIBS_3DS)
  LIBS := -lvgmstream -lmpg123 -lctru -lm
  ENABLE_MP3 := 1
else
  LIBDIRS := $(CTRULIB)
  LIBS := -lvgmstream -lctru -lm
endif

#---------------------------------------------------------------------------------
# list of directories containing libraries
#---------------------------------------------------------------------------------
# CIA (installable to Home Menu): `make cia` — not part of the default `make` / `all` target.
# makerom needs an RSF. Prefer $(TARGET).rsf; if you only have a copy from an old name (e.g. Mp3_Player), we fall back. Override: CIA_RSF := myapp.rsf
#---------------------------------------------------------------------------------
CIA_RSF ?= $(if $(wildcard $(TARGET).rsf),$(TARGET).rsf,$(if $(wildcard Mp3_Player.rsf),Mp3_Player.rsf,$(TARGET).rsf))

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
	$(foreach dir,$(DATA),$(CURDIR)/$(dir))

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

# top_screen_bg.bmp is embedded with bin2o (separate from DATA binfiles; avoids HFILES on generated .h)
export GFX_EMBED      := top_screen_bg.bmp.o
export OFILES         := $(OFILES_BIN) $(OFILES_SOURCES) $(GFX_EMBED)

export HFILES := $(PICAFILES:.v.pica=_shbin.h) $(SHLISTFILES:.shlist=_shbin.h) \
	$(addsuffix .h,$(subst .,_,$(BINFILES))) \
	$(GFXFILES:.t3s=.h)

export INCLUDE := $(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
	$(foreach dir,$(LIBDIRS),-I$(dir)/include) \
	-I$(VGMSTREAM_DIR)/src \
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

.PHONY: all clean cia

#---------------------------------------------------------------------------------
$(VGMSTREAM_LIB):
	@$(MAKE) -C $(VGMSTREAM_DIR) -f Makefile.3ds PORTLIBS_3DS="$(PORTLIBS_3DS)" ENABLE_MP3="$(ENABLE_MP3)"

#---------------------------------------------------------------------------------
all: $(BUILD) $(GFXBUILD) $(DEPSDIR) $(ROMFS_T3XFILES) $(T3XHFILES) $(VGMSTREAM_LIB)
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile all

#---------------------------------------------------------------------------------
# CIA target: build installable .cia (install and launch from Home Menu for New3DS extended RAM)
# Requires: makerom in PATH (e.g. from devkitPro buildtools or 3dstools)
#---------------------------------------------------------------------------------
cia: all
	@echo building $(TARGET).cia with RSF: $(CIA_RSF)
	@makerom -f cia -o $(TARGET).cia -rsf $(CIA_RSF) -target t -elf $(TARGET).elf -icon $(TARGET).smdh
	@echo built ... $(TARGET).cia

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
	@rm -fr $(BUILD) $(TARGET).3dsx $(OUTPUT).smdh $(TARGET).elf $(TARGET).cia $(GFXBUILD)

clean-vgmstream:
	@$(MAKE) -C $(VGMSTREAM_DIR) -f Makefile.3ds clean 2>/dev/null || true

#---------------------------------------------------------------------------------
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
# Embedded .bmp: bin2o writes top_screen_bg_bmp.h + top_screen_bg.bmp.o in $(BUILD)
#---------------------------------------------------------------------------------
top_screen_bg.bmp.o: top_screen_bg.bmp
	@echo $(notdir $<)
	@$(bin2o)

topbg.o: top_screen_bg.bmp.o

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
