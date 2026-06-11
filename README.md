# SLiMM

**S**tateless **L**ightweight Modern **M**anager — an ultra-lightweight graphical login bootstrapper for Wayland-era Linux.

```
Boot → SLiMM → PAM auth → launch compositor → exit
```

No SLiMM UI process after login. A minimal **root reaper** (same binary, blocked in `waitpid`) restarts the greeter when the compositor exits.

## Session lifecycle

```
Login → greeter exits → reaper waitpid(compositor)
      → logout → systemctl start slimm.service → login screen
```

- Greeter `_exit(0)` immediately after fork — **0 MB UI RSS** during session
- Reaper calls `setsid()` + ignores `SIGHUP` so it survives greeter exit / unit deactivation
- Reaper stays root, waits on the session process (e.g. `start-hyprland`)
- On compositor exit: **`systemctl start slimm.service`** (tty1, displaces getty, journal); direct `exec slimm` fallback if systemctl fails
- `KillMode=process` — reaper survives greeter exit (not cgroup-killed)
- `Restart=on-failure` — systemd does not respawn greeter during an active session

## Features

- Direct **DRM/KMS** rendering (primary backend)
- **STE2** precompiled themes — mmap at runtime, zero TOML/font/image parsing in production
- **PAM** authentication
- Session discovery from `/usr/share/wayland-sessions/`
- OS logo auto-detected at **build time** from `/etc/os-release`
- One-shot execution (~52 KB stripped `MINIMAL` binary)
- GLES3 UI with cached panel/field FBOs

## Requirements

**Runtime** (production `slimm`):

- DRM/KMS, EGL/GLES (Mesa), libinput, libxkbcommon, PAM, systemd-libs

**Build** (`slimc` theme compiler + `makepkg`):

- base-devel, FreeType, fontconfig, JetBrains Mono Nerd Font (Arch: `ttf-jetbrains-mono-nerd`)

## Build

```bash
make                         # MINIMAL=1: slimm + slimc + theme.slimt
make dev                     # dev binary: TOML + Wayland overlay
make V=1                     # verbose compile
makepkg -si                  # Arch package (from this directory)
```

| Make variable | Default | Meaning |
|---------------|---------|---------|
| `MINIMAL=1` | yes | Production: STE2-only, no FreeType/fontconfig at runtime |
| `VERSION=x.y.z` | `0.2.5` | Footer string `SLiMM x.y.z` |
| `V=0` | yes | Quiet build (PKGBUILD uses this) |

Build auto-detects host OS logo → baked into `theme.slimt`. Override with `logo_path` in `theme.toml`.

## Install

```bash
sudo make install
sudo systemctl enable --now slimm.service
```

Disable another DM first (e.g. `sudo systemctl disable --now slim2`).

## Configuration

| File | Role |
|------|------|
| `/etc/slimm/theme.slimt` | **Production** — precompiled STE2 blob (required) |
| `/etc/slimm/theme.toml` | Source for `slimc`; edit then recompile |
| `/etc/pam.d/slimm` | PAM authentication stack |

After editing `theme.toml`:

```bash
sudo slimc /etc/slimm/theme.toml -o /etc/slimm/theme.slimt
sudo systemctl restart slimm
```

### theme.toml notes

- **No inline comments on value lines** — put `#` comments on their own line (parser strips inline `#` but keep values clean)
- `autologin_user = "name"` — pre-fill username; greeter starts on password field
- `autologin_delay = 0` — wait for password + Enter (non-zero = auto-login without password)

### Login flow

| Step | Action |
|------|--------|
| 1 | Type username (or skip if `autologin_user` set) |
| 2 | **Enter** |
| 3 | Type password (`*` in field) |
| 4 | **Enter** |

### Low-RAM theme tips

- Solid `background_color` only (default) — no wallpaper GPU texture
- `bg_max_width` / `bg_max_height` cap wallpaper at compile time (default 1280×720)

## Usage

```bash
slimm --help
slimm --ste2 /path/to/theme.slimt
```

Dev build only:

```bash
slimm --config ./theme.toml    # TOML fallback
# theme.toml: mode = "wayland"  # overlay inside Hyprland/Sway
```

## Logout

The greeter returns when the **session process exits** (what SLiMM exec'd from the
`.desktop` file — usually `start-hyprland`, not Hyprland alone).

| Hyprland bind | Effect |
|---------------|--------|
| `hl.dsp.exit()` (Super+M) | Exits Hyprland; `start-hyprland` exits on clean shutdown → greeter returns |
| `loginctl terminate-user $USER` | Ends full user session → compositor exits → greeter returns |

If logout shows a black screen or getty instead of SLiMM, check the journal:

```bash
journalctl -t slimm -b -e          # reaper + greeter (syslog)
journalctl -u slimm -b -e        # greeter boot only (systemd unit)
journalctl -b | rg 'session:'      # either source
```

Expect `session: reaper pid … launching …` right after login, then on logout
`session: compositor exited …` and `session: relaunching greeter`.

**After upgrading slimm you must log out and log in again** — the reaper from an
older login does not pick up the new binary.

## Debugging

```bash
journalctl -t slimm -b -e -f
```

After login, confirm the reaper survived:

```bash
pstree -ps "$(pgrep start-hyprland)"
# expect: slimm(reaper) → start-hyprland → Hyprland
```

Look for `login ok for '…', launching '/usr/bin/…'` after auth.

## Development overlay

`make dev` + `mode = "wayland"` in `theme.toml` — test UI inside a running compositor without taking over tty1.

## Related

- [SLiM2](https://github.com/sickhate/slim2) — persistent DRM/KMS greeter (same author)
- Classic [SLiM](https://wiki.archlinux.org/title/SLiM) — historical X11 login manager

## License

MIT — see [LICENSE](LICENSE).
