DEVKITPRO ?= /opt/devkitpro
DEVKITARM ?= $(DEVKITPRO)/devkitARM
CTRULIB := $(DEVKITPRO)/libctru
CC := $(DEVKITARM)/bin/arm-none-eabi-gcc
ARCH := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft
CFLAGS := -g -O2 -Wall $(ARCH) -I$(CTRULIB)/include -D__3DS__ -ffunction-sections
LDFLAGS := -specs=3dsx.specs -g $(ARCH) -L$(CTRULIB)/lib
all: notifier.cia notifier.3dsx
notifier.elf: source/main.c
	$(CC) $(CFLAGS) $< $(LDFLAGS) -lctru -lm -o $@
icon.icn: icon.png
	bannertool makesmdh -s "3DS Notifier" -l "Custom notifications" -p "plszynon" -i icon.png -o icon.icn
banner.bnr: banner.png audio.wav
	bannertool makebanner -i banner.png -a audio.wav -o banner.bnr
notifier.cia: notifier.elf app.rsf icon.icn banner.bnr
	makerom -f cia -o notifier.cia -elf notifier.elf -rsf app.rsf -icon icon.icn -banner banner.bnr -exefslogo -target t
notifier.3dsx: notifier.elf icon.icn
	3dsxtool notifier.elf notifier.3dsx --smdh=icon.icn
