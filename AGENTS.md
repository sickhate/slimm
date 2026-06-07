# SLiMM — Stateless Lightweight Modern Manager

Ultra-lightweight graphical login bootstrapper for Wayland-era Linux.
**Authenticate → Launch Session → Exit.** No resident process after login.

## Mission

SLiMM is **not** a desktop environment, compositor, session manager, lock screen,
notification daemon, graphical toolkit, or scripting platform.

Priority order: **RAM → CPU → startup → dependencies → binary size → visuals.**

## Core Philosophy

### One-shot execution
```
Boot → SLiMM → Authentication → Session Launch → SLiMM Exit
```
Post-login target: **0 MB SLiMM UI.** A minimal root reaper (same 52 KB binary, `waitpid` only) returns the greeter on compositor exit — no systemd `Restart=always`, no helper daemons.

### Stateless + STE2-first
Themes are **compiled** (`slimc`), **mmap'd** at runtime, never parsed.
Production build (`MINIMAL=1`) links **no FreeType, fontconfig, stb_image, or Wayland**.

### Wayland session first
SLiMM launches compositors (Hyprland, Sway, etc.). SLiMM is not a compositor.

## Architecture

```
systemd → SLiMM → DRM/KMS → STE2 (mmap) → PAM → exec compositor → exit
```

Dev-only overlay: `make dev` adds wlroots layer-shell backend + TOML fallback.

Project docs: `README.md` (user-facing), `CHANGELOG.md` (releases), `AGENTS.md` (agent/dev rules).

## Build

```bash
make                    # MINIMAL=1 production binary (~52 KB stripped)
make dev                # full binary with TOML + Wayland dev backend
make slimc              # compile theme.toml → theme.slimt
./slimc theme.toml -o theme.slimt
sudo make install       # installs slimm + slimc + theme.slimt + PAM + systemd
./slimm --ste2 ./theme.slimt   # local test
```

| Target | Links | Use |
|--------|-------|-----|
| `slimm` (default) | DRM, EGL, GLES, PAM, libinput | Production TTY |
| `slimm` (`MINIMAL=0`) | + FreeType, fontconfig, Wayland | Dev / fallback |

Compiler flags (both): `-Os -flto -fdata-sections -ffunction-sections -Wl,--gc-sections`

## STE2 Theme

```
theme.toml → slimc → theme.slimt → mmap() → GPU upload → munmap()
```

| Asset | Compile-time cap | Runtime heap |
|-------|------------------|--------------|
| Font atlas | 512×512 R8 | mmap only during upload |
| Background | 1280×720 max (`bg_max_width/height`) | 0 after upload |
| Logo | 256×256 max | 0 after upload |
| No image | solid `background_color` | **lowest RAM** |

Default `theme.toml` uses **solid color only** (no wallpaper) for minimum footprint.

## Memory Budget (MINIMAL + solid STE2)

| Component | Size |
|-----------|------|
| Binary (stripped) | **~52 KB** |
| Font atlas (GPU) | 256 KB |
| Panel + field FBOs | ~800 KB |
| Background (optional) | 0 – 3.7 MB GPU @ 1280×720 |
| Runtime heap | ~50 KB (config, UI state) |
| **No fontconfig/FreeType** | saves 10–30 MB vs dev build |

**Target**: 5–15 MB RSS static theme on 1080p. **Achievable** with MINIMAL + solid STE2.
Peak startup with wallpaper in STE2: ~+4 MB GPU, no 41 MB stbi peak (compile-time downscale).

## Configuration

| Path | Purpose |
|------|---------|
| `/etc/slimm/theme.slimt` | Production STE2 blob (required for MINIMAL) |
| `/etc/slimm/theme.toml` | Build input for `slimc` only |
| `/etc/pam.d/slimm` | PAM stack |

CLI: `--ste2 PATH` (production), `--config PATH` (dev only).

## Keyboard Map

| Key | Action |
|-----|--------|
| Type | username / password |
| Enter | advance / submit |
| Tab | input ↔ dropdown |
| Left/Right | cycle session |
| Up/Down | dropdown |
| Escape | clear → back → quit |
| F10/F11/F12 | poweroff / reboot / suspend |

## Agent Rules

1. **One-shot UI** — greeter `_exit(0)` after fork; reaper `exec slimm` on logout (not `Restart=always`).
2. **MINIMAL is default** — no runtime font/image parsing in production.
3. **STE2 caps** — respect `STE2_BG_MAX_*` / `STE2_LOGO_MAX` in slimc.
4. **DRM primary** — Wayland backend is dev-only (`make dev`).
5. **No scope creep** — no compositor, networking, scripting, plugins.

## Roadmap

| Item | Status |
|------|--------|
| MINIMAL production build | **Done** |
| STE2-first init (skip TOML) | **Done** |
| Compile-time image downscale | **Done** |
| Rename `.slimt` → `.ste2` | Pending |
| musl static build | Pending |
| Multi-seat | Pending |
| RSS massif on bare TTY | Manual verify |

## Source Layout

```
src/main.c       one-shot loop, STE2-first load, --ste2
src/ste2.c       mmap loader + slim_images_free
src/font_min.c   STE2 glyph lookup (no FreeType)
src/imgscale.c   shared downscale (slimc + dev theme.c)
src/slimc.c      STE2 compiler
src/exec.c       desktop Exec sanitize + execvp + greeter relaunch
src/session.c    setuid compositor fork, root reaper waitpid
```

Dev-only: `font.c`, `theme.c`, `wayland.c`, `wlr-layer-shell-unstable-v1.c`

---

*Maximum simplicity. Maximum efficiency. Minimum resource consumption.*
