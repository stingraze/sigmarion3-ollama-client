# Ollama CE — GUI client for Windows CE
#
# Primary target: arm-mingw32ce-gcc (CeGCC / brain-hackers Brainux toolchain)
# Host helper: gcc -DHOST_BUILD

APP      := ollama-ce.exe
ALTAPP   := ollama-ce-ws2.exe
HOSTAPP  := ollama-ce-host
TESTAPP  := json_test

CE_SRC := src/utf8.c src/jsonutil.c src/http.c src/config.c src/ollama.c src/pretty.c src/gui.c
HOST_SRC := src/utf8.c src/jsonutil.c src/http.c src/config.c src/ollama.c src/pretty.c src/host_main.c
TEST_SRC := test/test_json.c src/jsonutil.c src/utf8.c src/http.c src/ollama.c src/pretty.c

CEGCC_CC := $(shell command -v arm-mingw32ce-gcc 2>/dev/null || \
              command -v arm-wince-mingw32ce-gcc 2>/dev/null || \
              command -v arm-wince-cegcc-gcc 2>/dev/null)
ifeq ($(origin CC),default)
CC := $(CEGCC_CC)
endif
ifeq ($(CC),)
CC := $(CEGCC_CC)
endif

HOSTCC ?= gcc

MINGW32CE_CFLAGS := -Os -Wall -I src \
	-DUNICODE -D_UNICODE -DWIN32 -D_WIN32 -D_WIN32_WCE=0x0300 -DUNDER_CE
CEGCC_CFLAGS := -mwin32 -Os -Wall -I src \
	-DUNICODE -D_UNICODE -DWIN32 -D_WIN32 -D_WIN32_WCE=0x0300 -DUNDER_CE

ifeq ($(findstring mingw32ce,$(CC)),mingw32ce)
CFLAGS := $(MINGW32CE_CFLAGS)
else
CFLAGS := $(CEGCC_CFLAGS)
endif

HOST_CFLAGS := -DHOST_BUILD -Os -Wall -Wextra -I src

LDFLAGS :=
# winsock = CE 3.0 WINSOCK.dll (Sigmarion III). ws2 = WS2.dll (CE 4+ / SHARP Brain).
WSL ?= winsock

CE_DEPS := src/compat.h src/config.h src/http.h src/jsonutil.h src/ollama.h \
	src/pretty.h src/utf8.h src/winsock2.h

.PHONY: all alt clean strip check-toolchain host test

all: check-toolchain $(APP)

# Same client linked against WS2.dll instead of WINSOCK.dll, for devices whose
# Winsock 1.1 stack misbehaves (CE 4+, SHARP Brain).
alt: check-toolchain $(ALTAPP)

$(ALTAPP): $(CE_SRC) $(CE_DEPS)
	$(CC) $(CFLAGS) -o $@ $(CE_SRC) $(LDFLAGS) -lcommctrl -lws2

check-toolchain:
	@case "$(CC)" in \
	  *mingw32ce*|*cegcc*) echo "Using $(CC)" ;; \
	  *) \
		echo "No CeGCC cross compiler found (CC=$(CC))."; \
		echo "Install the Brainux/brain-hackers toolchain:"; \
		echo "  https://github.com/brain-hackers/cegcc-build/releases"; \
		echo "Then: export PATH=\"/opt/cegcc/bin:\$$PATH\" && make"; \
		exit 1 ;; \
	esac

$(APP): $(CE_SRC) $(CE_DEPS)
ifeq ($(WSL),winsock)
	$(CC) $(CFLAGS) -o $@ $(CE_SRC) $(LDFLAGS) -lcommctrl -lwinsock
else ifeq ($(WSL),ws2)
	$(CC) $(CFLAGS) -o $@ $(CE_SRC) $(LDFLAGS) -lcommctrl -lws2
else ifeq ($(WSL),ws2_32)
	$(CC) $(CFLAGS) -o $@ $(CE_SRC) $(LDFLAGS) -lcommctrl -lws2_32
else
	$(CC) $(CFLAGS) -o $@ $(CE_SRC) $(LDFLAGS) -lcommctrl -lwinsock || \
	$(CC) $(CFLAGS) -o $@ $(CE_SRC) $(LDFLAGS) -lcommctrl -lws2 || \
	$(CC) $(CFLAGS) -o $@ $(CE_SRC) $(LDFLAGS) -lcommctrl -lws2_32
endif

host: $(HOSTAPP)

$(HOSTAPP): $(HOST_SRC) src/*.h
	$(HOSTCC) $(HOST_CFLAGS) -o $@ $(HOST_SRC)

test: $(TESTAPP)
	./$(TESTAPP)

$(TESTAPP): $(TEST_SRC) src/jsonutil.h src/http.h src/utf8.h src/ollama.h src/pretty.h
	$(HOSTCC) $(HOST_CFLAGS) -o $@ $(TEST_SRC)

strip:
	-which arm-mingw32ce-strip >/dev/null 2>&1 && arm-mingw32ce-strip $(APP) $(wildcard $(ALTAPP)) || true
	-which arm-wince-mingw32ce-strip >/dev/null 2>&1 && arm-wince-mingw32ce-strip $(APP) || true
	-which arm-wince-cegcc-strip >/dev/null 2>/dev/null && arm-wince-cegcc-strip $(APP) || true

clean:
	rm -f $(APP) $(ALTAPP) $(HOSTAPP) $(TESTAPP) src/*.o *.o
