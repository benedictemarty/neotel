# Makefile — NeoTel : terminal Minitel 1B / Minitel 2 pour Neo6502 (cc65 + ca65)
#
# Cibles :
#   make            build/neotel.bin (brut, $0800) + build/neotel.neo
#   make run        lance dans l'emulateur officiel neo (SDL), faux modem sur pty
#   make run-phos   lance dans Phosphoneo (SDL), faux modem sur pty
#                   (MODEM="--serve" : page de test locale, sans reseau)
#   make test       tests host (gcc) + tests cible (Phosphoneo headless)
#   make test-host  tests host seuls
#   make test-emu   tests cible seuls
#   make test-servers  fumee sur PAVI 3617 et MiniPavi (reseau, hors make test)
#   make ref        regenere les captures de reference (tests/ref/*.ppm)
#   make clean

CC65_HOME  ?= /usr/share/cc65
NEO_FW     ?= $(HOME)/Neo6502firmware
PHOSPHONEO ?= $(HOME)/Phosphoneo/build/phosphoneo
NEO_EMU    ?= $(NEO_FW)/bin/neo

CC      = cl65
# --disable-opt OptStackOps : cc65 2.19 genere un index faux (registre A suppose
# inchange apres un calcul de pointeur) — cf. AsteroNeo docs/cc65-optstackops.md
CFLAGS  = -t none -O --cpu 65c02 --static-locals -Wc --disable-opt,OptStackOps -I src
AFLAGS  = --cpu 65c02
CFG     = cfg/neo6502.cfg

BUILD   = build
BIN     = $(BUILD)/neotel.bin
NEO     = $(BUILD)/neotel.neo
MAP     = $(BUILD)/neotel.map
LBL     = $(BUILD)/neotel.lbl

CSRC    = src/main.c src/videotex.c src/fonts.c src/display.c src/keyboard.c \
          src/serial.c src/at_modem.c src/ui.c src/neo_time.c src/neo_gfx.c \
          src/terminal.c src/settings.c src/teleinfo.c src/display80.c src/font80.c
ASRC    = src/asm/crt0.s src/asm/display_asm.s src/asm/neo_delay.s src/asm/display80_asm.s

OBJ     = $(patsubst src/%.c,$(BUILD)/%.o,$(CSRC)) \
          $(patsubst src/asm/%.s,$(BUILD)/%.o,$(ASRC))

.PHONY: all run run-phos test test-host test-emu test-servers ref clean help

all: $(NEO)

$(BUILD):
	mkdir -p $(BUILD)

# Tout .o depend de tous les en-tetes : un vtx_context_t qui change de taille
# dans un .o et pas dans l'autre corrompt la BSS (constate, v0.2).
HDRS = $(wildcard src/*.h)

$(BUILD)/%.o: src/%.c $(HDRS) | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/%.o: src/asm/%.s | $(BUILD)
	ca65 $(AFLAGS) -I $(CC65_HOME)/asminc -o $@ $<

$(BIN): $(OBJ) $(CFG)
	ld65 -C $(CFG) -m $(MAP) -Ln $(LBL) -o $@ $(OBJ) none.lib

$(NEO): $(BIN)
	python3 tools/mkneo.py $(BIN) $(NEO) 0800 0800 "NeoTel"

# Lancement interactif : le faux modem (Hayes, vraies sockets TCP) est cree
# sur un pty et expose au firmware comme modem USB CDC (NEO_CDC_TTY).
# MODEM="--serve" pour la page de test locale, sans reseau.
MODEM ?=
run: $(NEO)
	tools/run_emu.sh neo $(MODEM)

run-phos: $(NEO)
	tools/run_emu.sh phos $(MODEM)

test: test-host test-emu

test-host:
	$(MAKE) -C tests/host

test-emu: $(NEO)
	tests/run.sh check

# Fumee sur les vrais serveurs Minitel (reseau ; hors `make test`)
test-servers: $(NEO)
	tests/test_servers.sh

ref: $(NEO)
	tests/run.sh ref

clean:
	rm -rf $(BUILD) tests/out/*
	$(MAKE) -C tests/host clean

help:
	@sed -n '3,12p' Makefile
