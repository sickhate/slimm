# SLiMM — Stateless Lightweight Modern Manager
# MINIMAL=1 (default): production greeter ~52KB, STE2-only runtime
# make dev: MINIMAL=0 with TOML + Wayland overlay for development
# See README.md for lineage (SLiM → SLiM2 → SLiMM) and build flags

MINIMAL ?= 1
VERSION ?= 0.2.4
V ?= 0

OPT_CFLAGS = -Os -pipe -flto -fdata-sections -ffunction-sections
OPT_LDFLAGS = -Wl,--gc-sections -lm -flto

ifeq ($(V),0)
  Q = @
  E = @printf '%s\n'
else
  Q =
  E = @:
endif

COMMON_SRC = src/main.c src/auth.c src/session.c src/ui.c src/config.c \
             src/vt.c src/drm.c src/renderer.c src/input.c src/ste2.c \
             src/imgscale.c src/exec.c

ifeq ($(MINIMAL),1)
CFLAGS ?= $(OPT_CFLAGS)
CFLAGS += -DSLIMM_MINIMAL -DSLIMM_VERSION=\"$(VERSION)\" $(shell pkg-config --cflags xkbcommon pam libdrm gbm libinput egl glesv2 libudev) \
          -Wall -Wextra -std=c11 -MMD -MP -I.
LDFLAGS += $(shell pkg-config --libs xkbcommon pam libdrm gbm libinput egl glesv2 libudev) $(OPT_LDFLAGS)
SRC = $(COMMON_SRC) src/font_min.c
else
PKGS = xkbcommon pam libdrm gbm libinput egl glesv2 freetype2 fontconfig libudev wayland-client wayland-egl
CFLAGS ?= $(OPT_CFLAGS)
CFLAGS += -DSLIMM_VERSION=\"$(VERSION)\" $(shell pkg-config --cflags $(PKGS)) \
          -Wall -Wextra -std=c11 -MMD -MP -I.
LDFLAGS += $(shell pkg-config --libs $(PKGS)) $(OPT_LDFLAGS)
SRC = $(COMMON_SRC) src/font.c src/theme.c \
      src/wayland.c src/wlr-layer-shell-unstable-v1.c
endif

OBJ = $(SRC:.c=.o)
DEP = $(OBJ:.o=.d)

-include $(DEP)

SLIMC_PKGS = freetype2 fontconfig
SLIMC_CFLAGS = $(shell pkg-config --cflags $(SLIMC_PKGS)) \
               -Wall -Wextra -std=c11 -I.
SLIMC_LDFLAGS = $(shell pkg-config --libs $(SLIMC_PKGS)) -lm

PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin
SYSCONFDIR ?= /etc
SYSTEMD_DIR ?= $(PREFIX)/lib/systemd/system
STE2_PATH ?= $(SYSCONFDIR)/slimm/theme.slimt
LOGO_DIR ?= $(PREFIX)/share/slimm/logos

all: slimm slimc theme.slimt
	$(E) "slimm $(VERSION) ready"

slimm: $(OBJ)
	$(E) "  LINK slimm"
	$(Q)$(CC) $(CFLAGS) -o $@ $(OBJ) $(LDFLAGS)

slimc: src/slimc.c src/config.c src/imgscale.c src/exec.c
	$(E) "  LINK slimc"
	$(Q)$(CC) $(SLIMC_CFLAGS) -o $@ src/slimc.c src/config.c src/imgscale.c src/exec.c $(SLIMC_LDFLAGS)

theme.slimt: theme.toml slimc logos-check
	$(Q)./slimc theme.toml -o $@

logos-check: slimc
	$(Q)test -d logos || { echo "slimm: missing logos/ directory"; exit 1; }
	$(Q)./slimc --os-logo >/dev/null || { \
		echo "slimm: no logos/<id>.png for this OS (check /etc/os-release)"; \
		echo "slimm: supported: $$(ls logos/*.png 2>/dev/null | xargs -n1 basename | sed 's/.png//')"; \
		exit 1; \
	}

%.o: %.c
	$(E) "  CC $<"
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<

install: slimm slimc
	@test -f theme.slimt || { echo "slimm: run 'make' first (theme.slimt missing)"; exit 1; }
	$(E) "  INSTALL"
	$(Q)install -Dm755 slimm $(DESTDIR)$(BINDIR)/slimm
	$(Q)install -Dm755 slimc $(DESTDIR)$(BINDIR)/slimc
	$(Q)install -Dm644 theme.toml $(DESTDIR)$(SYSCONFDIR)/slimm/theme.toml
	$(Q)install -Dm644 theme.slimt $(DESTDIR)$(STE2_PATH)
	$(Q)install -Dm644 pam/slimm $(DESTDIR)$(SYSCONFDIR)/pam.d/slimm
	$(Q)logo=$$(./slimc --os-logo); \
	id=$$(basename "$$logo" .png); \
	install -dm755 $(DESTDIR)$(LOGO_DIR); \
	install -Dm644 "$$logo" $(DESTDIR)$(LOGO_DIR)/$$id.png
	$(Q)sed 's|ExecStart=.*|ExecStart=$(BINDIR)/slimm|' slimm.service > slimm.service.tmp
	$(Q)install -Dm644 slimm.service.tmp $(DESTDIR)$(SYSTEMD_DIR)/slimm.service
	$(Q)rm -f slimm.service.tmp

uninstall:
	$(Q)rm -f $(DESTDIR)$(BINDIR)/slimm
	$(Q)rm -f $(DESTDIR)$(BINDIR)/slimc
	$(Q)rm -f $(DESTDIR)$(SYSCONFDIR)/pam.d/slimm
	$(Q)rm -f $(DESTDIR)$(SYSCONFDIR)/slimm/theme.slimt
	$(Q)rm -f $(DESTDIR)$(SYSCONFDIR)/slimm/theme.toml
	$(Q)rm -f $(DESTDIR)$(SYSTEMD_DIR)/slimm.service

clean:
	$(E) "  CLEAN"
	$(Q)rm -f slimm slimc theme.slimt $(OBJ) $(DEP)

.PHONY: all install uninstall clean dev logos-check

dev:
	$(MAKE) MINIMAL=0 V=$(V) slimm
