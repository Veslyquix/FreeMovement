.SUFFIXES:
.PHONY: all clean lyn


# Making sure devkitARM exists and is set up.
ifeq ($(strip $(DEVKITARM)),)
	$(error "Please set DEVKITARM in your environment. export DEVKITARM=<path to>devkitARM")
endif

# Including devkitARM tool definitions
include $(DEVKITARM)/base_tools


# ==================
# = OBJECTS & DMPS =
# ==================

# Mokha's decomp-named C library (FE-CLib-Mokha)
CLIB ?= $(DEVKITPRO)/FE-CLib_2025

# defining path of lyn
LYN ?= $(DEVKITPRO)/lyn
LYN += -longcalls

# reference object
LYNREF := $(CLIB)/reference/fireemblem8.ref.o

# OBJ to event
%.lyn.event: %.o $(LYNREF)
	$(NOTIFY_PROCESS)
	$(LYN) $< $(LYNREF) > $@

# OBJ to DMP rule
%.dmp: %.o
	$(NOTIFY_PROCESS)
	$(OBJCOPY) -S $< -O binary $@




# ========================
# = ASSEMBLY/COMPILATION =
# ========================

# Setting C/ASM include directories up
INCLUDE_DIRS := $(CLIB)/include
INCFLAGS     := $(foreach dir, $(INCLUDE_DIRS), -I "$(dir)")

# setting up compilation flags
ARCH    := -mcpu=arm7tdmi -mthumb -mthumb-interwork
CFLAGS  := $(ARCH) $(INCFLAGS) -std=gnu2x -Wall -Os -mtune=arm7tdmi -ffreestanding -fno-jump-tables -mlong-calls
ASFLAGS := $(ARCH) $(INCFLAGS)

# defining dependency flags
CACHE_DIR := .cache
$(shell mkdir -p $(CACHE_DIR))
CDEPFLAGS = -MMD -MT "$*.o" -MT "$*.asm" -MF "$(CACHE_DIR)/$(notdir $*).d" -MP
SDEPFLAGS = --MD "$(CACHE_DIR)/$(notdir $*).d"

# so header changes rebuild what uses them
-include $(wildcard $(CACHE_DIR)/*.d)

# ASM to OBJ rule
%.o: %.s
	$(NOTIFY_PROCESS)
	$(AS) $(ASFLAGS) $(SDEPFLAGS) -I $(dir $<) $< -o $@ $(ERROR_FILTER)

# C to ASM rule
# I would be fine with generating an intermediate .s file but this breaks dependencies
%.o: %.c
	$(NOTIFY_PROCESS)
	$(CC) $(CFLAGS) $(CDEPFLAGS) -g -c $< -o $@ $(ERROR_FILTER)

# C to ASM rule
%.asm: %.c
	$(NOTIFY_PROCESS)
	$(CC) $(CFLAGS) $(CDEPFLAGS) -S $< -o $@ -fverbose-asm $(ERROR_FILTER)

# Avoid make deleting objects it thinks it doesn't need anymore
# Without this make may fail to detect some files as being up to date
.PRECIOUS: %.o;



# ==============
# = MAKE CLEAN =
# ==============
ASMFILES := $(CFILES:.c=.asm)
LYNFILES := $(OFILES:.o=.lyn.event)
DMPFILES := $(OFILErrS:.o=.dmp)
CLEAN_FILES := $(OFILES) $(ASMFILES) $(LYNFILES)

ifeq ($(MAKECMDGOALS),clean)
  CFILES := $(shell find -type f -name '*.c')
  SFILES := $(shell find -type f -name '*.s')
# ASM_C_GENERATED := $(CFILES:.c=.o) $(SFILES:.s=.o) $(CFILES:.c=.asm)
  ASM_C_GENERATED := $(CFILES:.c=.o) $(CFILES:.c=.asm) $(SFILES:.s=.o)
# ASM_C_GENERATED += $(ASM_C_GENERATED:.o=.dmp) $(ASM_C_GENERATED:.o=.lyn.event)
  ASM_C_GENERATED += $(ASM_C_GENERATED:.o=.lyn.event)
  CLEAN_FILES += $(ASM_C_GENERATED)
endif

clean:
	rm -f $(CLEAN_FILES)
	
	
	
# ============
# = MAKE ALL =
# ============
SRC_PATH = ./
SRC_SKILLS_PATH = ./SkillSystem/Skills
SRC_BATTLE_PATH = ./BattleSystemRework

# DIRS = $(shell find $(SRC_PATH) -maxdepth 6 -type d)
DIRS = $(shell find $(SRC_PATH) -type d)

SRC_FILES := $(foreach dir, $(DIRS), $(wildcard $(dir)/*.c))
S_FILES := $(foreach dir, $(DIRS), $(wildcard $(dir)/*.s))
LYN_TARGET := $(patsubst %.c,%.lyn.event, $(SRC_FILES) ) $(patsubst %.s,%.lyn.event, $(S_FILES) )
ASM_TARGET := $(patsubst %.c,%.asm, $(SRC_FILES) )
OBJ_TARGET := $(patsubst %.c,%.o, $(SRC_FILES) ) $(patsubst %.s,%.o, $(S_FILES) )

# deleted after each build, which shouldn't make the next build redo them
.SECONDARY: $(OBJ_TARGET)

all:
	$(MAKE) $(ASM_TARGET)
	$(MAKE) $(LYN_TARGET)
	rm -f $(OBJ_TARGET)
	
lyn:
	$(MAKE) $(LYN_TARGET)
	rm -f $(OBJ_TARGET)
	
	

