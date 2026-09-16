# Tests hote (gcc) — logique portable compilee avec -DTEST_HOST contre le
# Neo6502 logiciel de neo_stub.c (bloc $FF00, VRAM, clavier, UART, temps).
CC = gcc
CFLAGS = -Wall -Wextra -Wno-unused-parameter -I ../../src -I . -DTEST_HOST
SRC = ../../src
STUB = neo_stub.c $(SRC)/settings.c
HDRS = $(wildcard $(SRC)/*.h) neo_stub.h

TESTS = test_videotex test_atmodem test_ui test_keyboard test_display test_terminal test_drcs test_settings
TOOLS = render_page

all: $(TESTS) $(TOOLS)
	@fail=0; for t in $(TESTS); do ./$$t || fail=1; done; \
	if [ $$fail -eq 0 ]; then echo "tests hote : OK"; else echo "tests hote : ECHEC"; exit 1; fi

test_videotex: test_videotex.c $(SRC)/videotex.c $(SRC)/fonts.c $(SRC)/terminal.c $(HDRS)
	$(CC) $(CFLAGS) -o $@ test_videotex.c $(SRC)/videotex.c $(SRC)/fonts.c $(SRC)/terminal.c

test_atmodem: test_atmodem.c $(SRC)/at_modem.c $(HDRS)
	$(CC) $(CFLAGS) -o $@ test_atmodem.c $(SRC)/at_modem.c

test_ui: test_ui.c $(SRC)/ui.c $(HDRS)
	$(CC) $(CFLAGS) -o $@ test_ui.c $(SRC)/ui.c

test_keyboard: test_keyboard.c $(SRC)/keyboard.c $(SRC)/serial.c $(STUB) $(HDRS)
	$(CC) $(CFLAGS) -o $@ test_keyboard.c $(SRC)/keyboard.c $(SRC)/serial.c $(STUB)

test_display: test_display.c $(SRC)/display.c $(SRC)/fonts.c $(SRC)/videotex.c $(SRC)/terminal.c $(STUB) $(HDRS)
	$(CC) $(CFLAGS) -o $@ test_display.c $(SRC)/display.c $(SRC)/fonts.c $(SRC)/videotex.c $(SRC)/terminal.c $(STUB)

test_terminal: test_terminal.c $(SRC)/terminal.c $(SRC)/serial.c $(SRC)/videotex.c $(SRC)/fonts.c $(STUB) $(HDRS)
	$(CC) $(CFLAGS) -o $@ test_terminal.c $(SRC)/terminal.c $(SRC)/serial.c $(SRC)/videotex.c $(SRC)/fonts.c $(STUB)

test_drcs: test_drcs.c $(SRC)/display.c $(SRC)/fonts.c $(SRC)/videotex.c $(SRC)/terminal.c $(SRC)/serial.c $(STUB) $(HDRS)
	$(CC) $(CFLAGS) -o $@ test_drcs.c $(SRC)/display.c $(SRC)/fonts.c $(SRC)/videotex.c $(SRC)/terminal.c $(SRC)/serial.c $(STUB)

test_settings: test_settings.c $(STUB) $(HDRS)
	$(CC) $(CFLAGS) -o $@ test_settings.c $(STUB)

render_page: render_page.c $(SRC)/display.c $(SRC)/fonts.c $(SRC)/videotex.c $(SRC)/terminal.c $(STUB) $(HDRS)
	$(CC) $(CFLAGS) -o $@ render_page.c $(SRC)/display.c $(SRC)/fonts.c $(SRC)/videotex.c $(SRC)/terminal.c $(STUB)

fuzz_videotex: fuzz_videotex.c $(SRC)/videotex.c $(SRC)/fonts.c $(SRC)/terminal.c
	clang -g -O1 -fsanitize=fuzzer,address -I ../../src -DTEST_HOST -o $@ fuzz_videotex.c $(SRC)/videotex.c $(SRC)/fonts.c $(SRC)/terminal.c

clean:
	rm -f $(TESTS) $(TOOLS) fuzz_videotex
