# Changelog

All notable changes to SLiMM are documented here.

## [0.2.5] — 2026-06-11

Reworked session launch so the greeter returns after logout **with no slimm
process running during the session** — and fixed an NVIDIA freeze on the way.

### Changed

- **No reaper — the greeter `exec()`s the compositor in place.** Previously the
  greeter forked a reaper that `waitpid`'d the compositor and relaunched the
  greeter on its exit, so a slimm process lingered for the entire session. Now
  `do_login` (and autologin) authenticate, release DRM/VT, drop privileges, and
  `exec` the compositor directly: the main PID *becomes* the compositor, so
  **zero slimm process runs during the session** (the point of a stateless
  greeter). (`main.c`, `session.c`)
- **systemd is the supervisor.** `slimm.service` is now `Restart=always` (+ a
  `StartLimit` crash-guard that falls back to a tty), so when the compositor
  exits (e.g. Hyprland Super+M) systemd starts a fresh greeter — replacing the
  old in-process `systemctl start` self-relaunch and the `KillMode=process`
  reaper protection.

### Fixed

- **Frozen display after login (NVIDIA).** The old reaper inherited slimm's
  `O_CLOEXEC` DRM/render/input fds across the `fork` and held them open for the
  whole session, leaving a leftover DRM client on `card1`; the compositor's first
  atomic modeset then hung (main thread stuck in `ep_poll`, display frozen — found
  via a live `freeze-capture` over SSH). `exec`ing the compositor closes those fds
  inherently, and an explicit `exec_close_inherited_fds()` guards it regardless of
  `O_CLOEXEC`.

### Removed

- The reaper (`session_launch_child`), `exec_relaunch_slimm()` and `attach_tty1()`
  — superseded by the `exec` + systemd `Restart=always` model.

## [0.2.4-3] — 2026-06-09

### Reverted

- **Reverted the 0.2.4 "session launch timing" rework — it regressed login (compositor
  never came up).** The reaper restructure (authenticate in the child, then block in
  `session_wait_user_ready()` for up to 15s waiting on `/run/user/$UID/systemd/private`
  before exec'ing the compositor) left users authenticated but with no desktop. Restored
  the 0.2.3 launch path: authenticate in the parent, fork a child that **immediately**
  execs the compositor, parent `_exit(0)` keeps PAM alive for the child. `session.c`,
  `session.h`, `main.c` reverted to their 0.2.3 state. `KillMode=process` (from -2) is
  kept as a safety net so the launch child can never be cgroup-killed.

## [0.2.4-2] — 2026-06-09

### Fixed

- **Compositor failed to launch after login (intermittent → near-always).** `slimm.service`
  set no `KillMode`, so it defaulted to `control-group`: when the greeter's main process
  `_exit(0)`s right after forking the reaper, systemd considers the unit done and SIGTERMs
  the **entire cgroup** — killing the reaper before it can exec the compositor. The 0.2.4
  `session_wait_user_ready()` 15s wait widened that window so the reaper lost the race almost
  every boot (auth succeeded, then no desktop). Added **`KillMode=process`** so systemd kills
  only the (already-exited) main PID and leaves the reaper + compositor alive.

## [0.2.4] — 2026-06-09

### Changed

- **Session launch**: greeter forks a root **reaper** that owns PAM for the full compositor lifetime (no early logind session close)
- Reaper waits up to 15s for `/run/user/$UID/bus` and `systemd/private` before execing the compositor
- Sets `XDG_SESSION_CLASS`, `XDG_SESSION_DESKTOP`, and `XDG_RUNTIME_DIR` (overwrite) for Wayland sessions

### Fixed

- Hyprland safe-mode / crash-on-first-boot when compositor started before user systemd was ready
- `Session 1 logged out` immediately after login (PAM session died with greeter `_exit`)
- `xdg-desktop-portal-hyprland` failing on boot (`Couldn't connect to a wayland compositor`)

## [0.2.3] — 2026-06-08

### Added

- README: origins & lineage (SLiM → SLiM2 → SLiMM), build flags, testing guide
- `slimm` / `slimc` stderr logging with `fflush` for journal debugging
- `EVIOCGRAB` on libinput devices — stop TTY echo stealing keystrokes
- `vt_console_shield()` — `KDSKBMODE K_OFF` on active VT + `/dev/tty0`
- TOML parser: strip inline `#` comments from values (respecting quotes)
- `autologin_user` in `theme.toml` — pre-fill username, start on password field

### Changed

- UI: show **Enter password** / **Enter username** when Enter pressed too early
- PKGBUILD installs `CHANGELOG.md`; `pkgdesc` updated

### Fixed

- Password visible on TTY scrollback below greeter UI
- Login never reaching PAM when keyboard went to Linux console instead of libinput
- `autologin_user` broken by inline `#` comment on same line in `theme.toml`

## [0.2.2] — 2026-06-07

### Added

- **OS logo auto-detect** at build: reads `/etc/os-release` (`ID`, then `ID_LIKE`) → `logos/<id>.png` baked into `theme.slimt`
- Bundled distro logos under `logos/` (arch, debian, fedora, ubuntu, …)
- `slimc --os-logo` / `--os-logo-id` for build scripts
- Version footer: `SLiMM <version>` bottom-right of screen
- `renderer_draw_texture_rt()` — correct FBO blit orientation

### Changed

- **PKGBUILD**: in-tree `$startdir` build (no git required); `options=('!debug')`; installs host logo only
- Dropdown menu: panel-matched corner radius, padded items, rounded hover rows
- Logo layout: centered on panel, offset below top padding
- Input field: `field_bg` / `field_text` colors, left-aligned text
- `slimm.service`: stderr/stdout to journal (keeps TTY clean)
- `numlock = true` default in `theme.toml`

### Fixed

- Field text mirrored/garbled (FBO texture V-flip)
- Password echoing on TTY below greeter (`ECHO`/`ICANON` disabled on VT)
- `config_pick_logo` newline trim on `ID=` from os-release
- Auth failure clears field cache; panel title no longer overlaps input

## [0.2.1] — 2026-06-05

### Added

- `exec.c` — desktop Exec sanitization, direct `execvp`, greeter relaunch via `exec`
- `vt_get_active_nr()` — dynamic `XDG_VTNR`
- STE2 blob bounds validation (offsets, sizes, atlas dimensions)

### Changed

- `Restart=on-failure` — login no longer respawns greeter while compositor runs
- Session: root reaper `waitpid` + `systemctl start slimm.service` on compositor exit (direct `exec` fallback)
- `.desktop` Exec lines sanitized at scan time and launch time

### Fixed

- Production blocker: `Restart=always` + `_exit(0)` grabbing DRM during user session

## [0.2.0] — 2026-06-05

### Added

- **MINIMAL production build** (`MINIMAL=1`, default) — STE2-only runtime, no FreeType/fontconfig/stb_image/Wayland
- `font_min.c` — lightweight glyph lookup for STE2 fonts
- `imgscale.c` — shared compile-time image downscale for `slimc` and dev `theme.c`
- `--ste2 PATH` CLI option
- `make dev` target for full dev binary with TOML + Wayland backends
- `theme.slimt` built and installed by `make install`
- `bg_max_width` / `bg_max_height` in `theme.toml` (default 1280×720)
- README.md, CHANGELOG.md

### Changed

- **Binary size**: ~108 KB → **~52 KB** stripped (MINIMAL)
- **Init order**: STE2 first; TOML/font/runtime images only in dev build
- Default `theme.toml`: solid-color background (lowest RAM)
- `PKGBUILD`: `MINIMAL=1` build; runtime deps trimmed (font libs → makedepends only)
- `slimm.service`: `Restart=on-failure` (reaper starts greeter via systemctl on logout)
- Session launch: parent `_exit(0)` after fork (stateless, 0 MB post-login)

### Fixed

- `output` use-after-free after `drm_init_gbm()`
- `drm_fd` leak on DRM init failure paths
- PAM double-close after `fork()`
- Wayland `keyboard_modifiers` null `xkb` dereference
- Removed erroneous `WAYLAND_DISPLAY` auto-detection when `mode = "drm"`

## [0.1.0] — 2026-06-04

### Added

- Initial SLiMM login manager
- DRM/KMS + GBM + EGL + GLES3 renderer
- TOML theme config + optional STE2 (`theme.slimt`) loader
- PAM authentication, session launcher, libinput keyboard
- Panel FBO + field cache FBO UI
- `slimc` STE2 compiler
- Wayland wlr-layer-shell dev backend
- systemd service unit
