#---------------------------------------------------------------------------------
# FreeSpace 2 for the New Nintendo 3DS  (fs2_open 3.7.2 + picaGL)
#
#   make            -> fs2-3ds.3dsx
#   make -j4        (recommended)
#
# Needs devkitARM + libctru (DEVKITPRO, DEVKITARM set).
#---------------------------------------------------------------------------------
.SUFFIXES:

ifeq ($(strip $(DEVKITARM)),)
$(error "Please set DEVKITARM in your environment. export DEVKITARM=<path to>devkitARM")
endif

include $(DEVKITARM)/3ds_rules

TARGET      := fs2-3ds
BUILD       := build
APP_TITLE       := FreeSpace 2
APP_DESCRIPTION := Space combat - New 3DS port of fs2_open
APP_AUTHOR      := Volition / SCP / fs2-3ds
APP_ICON        := $(CURDIR)/icon.png

#---------------------------------------------------------------------------------
# sources
#---------------------------------------------------------------------------------
ENGINE_SRC  := $(shell cat $(CURDIR)/build-sources.txt)
N3DS_SRC    := $(wildcard code/n3ds/*.cpp)
LUA_SRC     := $(filter-out lua/lua.c lua/luac.c lua/print.c lua/ltests.c,$(wildcard lua/*.c))
JPEG_SRC    := $(addprefix libjpeg/,jcapimin.c jcapistd.c jccoefct.c jccolor.c jcdctmgr.c jchuff.c jcinit.c \
               jcmainct.c jcmarker.c jcmaster.c jcomapi.c jcparam.c jcphuff.c jcprepct.c jcsample.c jctrans.c \
               jdapimin.c jdapistd.c jdatadst.c jdatasrc.c jdcoefct.c jdcolor.c jddctmgr.c jdhuff.c jdinput.c \
               jdmainct.c jdmarker.c jdmaster.c jdmerge.c jdphuff.c jdpostct.c jdsample.c jdtrans.c jerror.c \
               jfdctflt.c jfdctfst.c jfdctint.c jidctflt.c jidctfst.c jidctint.c jidctred.c jmemansi.c jmemmgr.c \
               jquant1.c jquant2.c jutils.c)
VORBIS_SRC  := $(addprefix lib/vorbis/lib/,mdct.c smallft.c block.c envelope.c window.c lsp.c lpc.c analysis.c \
               synthesis.c psy.c info.c floor1.c floor0.c res0.c mapping0.c registry.c codebook.c sharedbook.c \
               lookup.c bitrate.c vorbisfile.c)
THEORA_SRC  := $(addprefix lib/theora/lib/,apiwrapper.c bitpack.c decapiwrapper.c decinfo.c decode.c dequant.c \
               fragment.c huffdec.c idct.c info.c internal.c quant.c state.c)
PICAGL_SRC  := $(wildcard lib/picaGL/source/*.c) $(wildcard lib/picaGL/source/utils/*.c)
PICAGL_SHD  := $(wildcard lib/picaGL/source/shaders/*.v.pica)

ALL_SRC     := $(ENGINE_SRC) $(N3DS_SRC) $(LUA_SRC) $(JPEG_SRC) $(VORBIS_SRC) $(THEORA_SRC) $(PICAGL_SRC)
OBJS        := $(addprefix $(BUILD)/,$(addsuffix .o,$(basename $(ALL_SRC))))
SHBIN_OBJS  := $(addprefix $(BUILD)/shaders/,$(notdir $(PICAGL_SHD:.v.pica=.shbin.o)))

#---------------------------------------------------------------------------------
# flags
#---------------------------------------------------------------------------------
ARCH     := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft

DEFINES  := -D__3DS__ -DARM11 -D_3DS -DSCP_UNIX -DSCP_3DS -DUSE_OPENAL -DNO_DIRECT3D -DNO_NETWORK_SPEECH \
            -DNDEBUG -DFS2_SPEECH=0 -DLUA_ANSI -DHAVE_STDINT_H

INCLUDES := -I$(CURDIR)/code -I$(CURDIR)/code/n3ds/include -I$(CURDIR)/lua -I$(CURDIR)/libjpeg \
            -I$(CURDIR)/lib/picaGL/include -I$(CURDIR)/lib/picaGL/source -I$(BUILD)/shaders \
            -I$(CURDIR)/lib/vorbis/include -I$(CURDIR)/lib/vorbis/lib -I$(CURDIR)/lib/theora/include \
            -I$(CURDIR)/openal/include \
            -I$(PORTLIBS)/include -I$(PORTLIBS)/include/SDL -I$(CTRULIB)/include

OPT      := -O2 -ffast-math -fno-strict-aliasing -fsigned-char -ffunction-sections -fdata-sections \
            -mword-relocations
WARN     := -w

CFLAGS   := $(ARCH) $(OPT) $(WARN) $(DEFINES) $(INCLUDES)
CXXFLAGS := $(CFLAGS) -std=gnu++98 -fpermissive -Wno-narrowing
LDFLAGS  := -specs=3dsx.specs $(ARCH) -Wl,-Map,$(BUILD)/$(TARGET).map -Wl,--gc-sections
LIBS     := -L$(PORTLIBS)/lib -L$(CTRULIB)/lib -logg -lpng -lz -lctru -lm

#---------------------------------------------------------------------------------
all: $(TARGET).3dsx

$(TARGET).3dsx: $(TARGET).elf $(TARGET).smdh
	3dsxtool $< $@ --smdh=$(TARGET).smdh
	@echo built ... $@

$(TARGET).smdh: $(APP_ICON)
	smdhtool --create "$(APP_TITLE)" "$(APP_DESCRIPTION)" "$(APP_AUTHOR)" $(APP_ICON) $@

$(TARGET).elf: $(SHBIN_OBJS) $(OBJS)
	$(CXX) $(LDFLAGS) -o $@ $^ $(LIBS)
	@echo linked ... $@

# picaGL shaders -> shbin -> object (with a header picaGL includes)
$(BUILD)/shaders/%.shbin.o: lib/picaGL/source/shaders/%.v.pica
	@mkdir -p $(dir $@)
	picasso -o $(BUILD)/shaders/$*.shbin $<
	python3 tools/bin2s.py $(BUILD)/shaders/$*.shbin | $(AS) -o $@
	@echo "extern const u8 $*_shbin_end[];" > $(BUILD)/shaders/$*_shbin.h
	@echo "extern const u8 $*_shbin[];" >> $(BUILD)/shaders/$*_shbin.h
	@echo "extern const u32 $*_shbin_size;" >> $(BUILD)/shaders/$*_shbin.h

# picaGL sources need the generated shader headers
$(addprefix $(BUILD)/,$(addsuffix .o,$(basename $(PICAGL_SRC)))): $(SHBIN_OBJS)

# engine code: float literals stay single precision (fs2 is float-only; on the ARM11 VFP a
# stray "x * 0.5" otherwise turns into double maths plus two conversions)
$(BUILD)/code/%.o: ENGINE_FLAGS := -fsingle-precision-constant

$(BUILD)/%.o: %.cpp
	@mkdir -p $(dir $@)
	@echo $<
	@$(CXX) -MMD -MP $(CXXFLAGS) $(ENGINE_FLAGS) -c $< -o $@

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	@echo $<
	@$(CC) -MMD -MP $(CFLAGS) -std=gnu99 -c $< -o $@

clean:
	rm -rf $(BUILD) $(TARGET).3dsx $(TARGET).elf $(TARGET).smdh

.PHONY: all clean

-include $(OBJS:.o=.d)
