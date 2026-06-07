# SLiMM

**S**tateless **L**ightweight Modern **M**anager — an ultra-lightweight graphical login bootstrapper for Wayland-era Linux.

```
Boot → SLiMM → PAM auth → launch compositor → exit
```

No SLiMM UI process after login. A minimal **root reaper** (same binary, blocked in `waitpid`) returns the greeter when the compositor exits — no systemd restart loop, no helper daemons.

## Session lifecycle

```
Login → slimm UI exits → root reaper waits on compositor
      → logout → reaper exec slimm → login screen
```

- `Restart=on-failure` — avoids fighting the compositor for DRM/TTY on login
- Greeter return via **exec slimm** (zero extra installed scripts)
- During session: one sleeping root reaper (~minimal RSS, shared text with greeter binary)

## Features

- Direct **DRM/KMS** rendering (primary backend)
- **STE2** precompiled themes — mmap at runtime, zero TOML/font/image parsing in production
- **PAM** authentication
- Session discovery from `/usr/share/wayland-sessions/`
- One-shot execution (~52 KB stripped binary)
- GLES3 UI with cached panel/field FBOs

## Requirements

**Runtime** (production `slimm`):

- DRM/KMS, EGL/GLES (Mesa), libinput, libxkbcommon, PAM, systemd

**Build** (`slimc` theme compiler):

- FreeType, fontconfig (compile themes only — not linked into production `slimm`)

## Build

```bash
make                    # slimm + slimc + theme.slimt (host OS logo auto-detected)
make dev                # dev binary with TOML fallback + Wayland overlay backend
./slimc --os-logo-id    # print detected distro id (e.g. arch)
```

Build reads `/etc/os-release` and bakes `logos/<id>.png` into `theme.slimt`. Override with `logo_path` in `theme.toml`.

## Install

```bash
sudo make install       # /usr/local/bin/slimm + /etc/slimm/theme.slimt + PAM + systemd unit
sudo systemctl enable --now slimm.service
```

**Arch Linux:**

```bash
makepkg -si   # from this directory; no git repo required
```

## Configuration

| File | Role |
|------|------|
| `/etc/slimm/theme.slimt` | **Production** — precompiled STE2 theme (required) |
| `/etc/slimm/theme.toml` | Source for `slimc`; edit then recompile |
| `/etc/pam.d/slimm` | PAM authentication stack |

Edit `theme.toml`, then rebuild the runtime blob:

```bash
sudo slimc /etc/slimm/theme.toml -o /etc/slimm/theme.slimt
```

### Low-RAM theme tips

- Use solid `background_color` (default) — no wallpaper texture
- If using images, set `bg_max_width` / `bg_max_height` (default 1280×720)
- Logo is capped at 256×256 at compile time

## Usage

```bash
slimm --help
slimm --ste2 /path/to/theme.slimt    # explicit STE2 path
```

Dev build only:

```bash
slimm --config ./theme.toml          # TOML fallback when STE2 missing
```

## Development overlay (non-production)

`make dev` adds a wlroots layer-shell backend for testing inside Hyprland/Sway:

```toml
# theme.toml
mode = "wayland"
```

## License

MIT — see [LICENSE](LICENSE).
