# Changelog

All notable changes to SLiMM are documented here.

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
- Session: root reaper `waitpid` + `exec slimm` on compositor exit (no systemd helper)
- PAM: skip `auth_close()` on successful login handoff (session stays valid for child)
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
- `config_init_defaults()`, `config_load_sessions()` — skip full TOML parse when STE2 loads
- `slim_images_free()` — shared GL texture cleanup
- README.md, CHANGELOG.md

### Changed

- **Binary size**: ~108 KB → **~52 KB** stripped (MINIMAL)
- **Init order**: STE2 first; TOML/font/runtime images only in dev build
- Default `theme.toml`: solid-color background (lowest RAM)
- `PKGBUILD`: `MINIMAL=1` build; runtime deps trimmed (font libs → makedepends only)
- `AGENTS.md`: aligned with stateless one-shot architecture spec
- `slimm.service`: `Restart=on-failure` (reaper execs greeter on logout)
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
