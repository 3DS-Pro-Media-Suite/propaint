#---------------------------------------------------------------------------------
# Pro Paint
#---------------------------------------------------------------------------------
.SUFFIXES:

ifeq ($(strip $(DEVKITARM)),)
$(error "Please set DEVKITARM in your environment. export DEVKITARM=<path to>devkitARM")
endif

TOPDIR ?= $(CURDIR)
include $(DEVKITARM)/3ds_rules

TARGET		:=	propaint
BUILD		:=	build
SOURCES		:=	source source/document source/brush source/canvas source/ui source/sheets source/files tpressure ftpserver
DATA		:=
INCLUDES	:=	source source/document source/brush source/canvas source/ui source/sheets source/files source/data tpressure ftpserver
ROMFS		:=	romfs

APP_TITLE		:=	Pro Paint
APP_DESCRIPTION	:=	Layered drawing
APP_AUTHOR		:=	Makelek

VERSION_MAJOR	:=	1
VERSION_MINOR	:=	0
VERSION_MICRO	:=	0
UNIQUE_ID		:=	0xB9BD3
PRODUCT_CODE	:=	CTR-H-PPNT

ARCH	:=	-march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft

CFLAGS	:=	-g -Wall -Wextra -Wno-unused-parameter -O2 -mword-relocations \
			-ffunction-sections -fno-strict-aliasing -std=gnu11 -ffp-contract=off \
			$(ARCH)

CFLAGS	+=	$(INCLUDE) -D__3DS__ -DAPP_VERSION=\"$(VERSION_MAJOR).$(VERSION_MINOR).$(VERSION_MICRO)\"

ASFLAGS	:=	-g $(ARCH)
LDFLAGS	=	-specs=3dsx.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)

LIBS	:= -lpng16 -lz -lctru -lm

LIBDIRS	:= $(PORTLIBS) $(CTRULIB)

#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))
#---------------------------------------------------------------------------------

export OUTPUT	:=	$(CURDIR)/$(TARGET)
export TOPDIR	:=	$(CURDIR)
export VPATH	:=	$(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) $(foreach dir,$(DATA),$(CURDIR)/$(dir))
export DEPSDIR	:=	$(CURDIR)/$(BUILD)

CFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
BINFILES	:=	$(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.bcfnt)))

export LD		:=	$(CC)
export OFILES_BIN	:=	$(addsuffix .o,$(BINFILES))
export OFILES	:=	$(OFILES_BIN) $(CFILES:.c=.o)
export HFILES	:=	$(addsuffix .h,$(subst .,_,$(BINFILES)))

export INCLUDE	:=	$(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
			$(foreach dir,$(LIBDIRS),-I$(dir)/include) \
			-I$(CURDIR)/$(BUILD)

export LIBPATHS	:=	$(foreach dir,$(LIBDIRS),-L$(dir)/lib)

export APP_ICON		:=	$(TOPDIR)/icon.png
export _3DSXDEPS	:=	$(OUTPUT).smdh
export _3DSXFLAGS	+=	--smdh=$(CURDIR)/$(TARGET).smdh
export _3DSXFLAGS	+=	--romfs=$(CURDIR)/$(ROMFS)

.PHONY: all cia clean

all: $(BUILD)
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

# Needs makerom and bannertool in your PATH.
cia: all
	@bannertool makebanner -i meta/banner.png -a meta/banner.wav -o $(BUILD)/banner.bnr
	@bannertool makesmdh -s "$(APP_TITLE)" -l "$(APP_DESCRIPTION)" -p "$(APP_AUTHOR)" -i icon.png -o $(BUILD)/icon.smdh
	@makerom -f cia -o $(TARGET).cia -target t -exefslogo -elf $(TARGET).elf -rsf meta/app.rsf \
		-icon $(BUILD)/icon.smdh -banner $(BUILD)/banner.bnr \
		-major $(VERSION_MAJOR) -minor $(VERSION_MINOR) -micro $(VERSION_MICRO) \
		-DAPP_TITLE=$(TARGET) -DAPP_PRODUCT_CODE=$(PRODUCT_CODE) -DAPP_UNIQUE_ID=$(UNIQUE_ID) -DAPP_ROMFS=$(ROMFS)
	@echo built ... $(TARGET).cia

$(BUILD):
	@mkdir -p $@

clean:
	@echo clean ...
	@rm -fr $(BUILD) $(TARGET).3dsx $(TARGET).cia $(OUTPUT).smdh $(TARGET).elf

#---------------------------------------------------------------------------------
else

$(OUTPUT).3dsx	:	$(OUTPUT).elf $(_3DSXDEPS)

$(OUTPUT).elf	:	$(OFILES)

$(CFILES:.c=.o)	:	$(HFILES)

%.bcfnt.o	%_bcfnt.h :	%.bcfnt
	@echo $(notdir $<)
	@$(bin2o)

-include $(DEPSDIR)/*.d

endif
