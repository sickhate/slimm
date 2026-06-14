# SLiMM

**S**tateless **L**ightweight Modern **M**anager — an ultra-lightweight graphical login bootstrapper for Wayland-era Linux.

```
Boot → SLiMM → PAM auth → exec compositor (in place) → logout → systemd respawns SLiMM
```

No SLiMM process after login. The greeter authenticates then **`exec()`s the compositor in place** — the main PID *becomes* the compositor, so nothing lingers. systemd is the supervisor: `slimm.service` is `Restart=always`, so when the compositor exits (logout) a fresh greeter starts.

## Session lifecycle

```
Login → greeter exec()s compositor (no slimm process left)
      → logout (compositor exits) → systemd Restart=always → login screen
```

- Greeter `exec()`s the compositor in place — the main PID becomes the compositor, so **0 MB slimm RSS** during the session (the point of a stateless greeter)
- `exec` closes slimm's DRM/render/input fds (an explicit `exec_close_inherited_fds()` guards it regardless of `O_CLOEXEC`), so no leftover DRM client stalls the compositor's first modeset — this is what fixed the NVIDIA post-login freeze
- `setsid()` + ignore `SIGHUP` so VT hangups don't disturb the compositor
- **systemd is the supervisor:** `slimm.service` `Restart=always` starts a fresh greeter when the compositor exits
- `StartLimitBurst=5 / 30s` crash-loop guard — falls back to a usable tty instead of flickering forever
- `RestartPreventExitStatus=42` — Escape exits the greeter (status `42`) to a console and systemd does **not** respawn it

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

The slimm.service main PID *is* the compositor (SLiMM exec'd it from the
`.desktop` file — usually `start-hyprland`, not Hyprland alone). When that exits,
the unit's `Restart=always` starts a fresh greeter.

| Hyprland bind | Effect |
|---------------|--------|
| `hl.dsp.exit()` (Super+M) | Exits Hyprland; `start-hyprland` exits on clean shutdown → unit restarts → greeter returns |
| `loginctl terminate-user $USER` | Ends full user session → compositor exits → unit restarts → greeter returns |

If logout shows a black screen or getty instead of SLiMM, check the journal:

```bash
journalctl -t slimm -b -e          # greeter (syslog)
journalctl -u slimm -b -e          # systemd unit (start/restart)
journalctl -b | rg 'session:'      # either source
```

Expect `session: pid … exec '…' as <user>` right after login; on logout, systemd
restarts the unit and a fresh greeter comes up (`RestartSec=1`).

## Debugging

```bash
journalctl -t slimm -b -e -f
```

After login, confirm no slimm process lingers (the PID became the compositor):

```bash
pgrep -a slimm                     # expect: no output during an active session
pstree -ps "$(pgrep -x Hyprland | head -1)"
# expect: systemd → start-hyprland → Hyprland  (no slimm in the chain)
```

Look for `login ok for '…', launching '/usr/bin/…'` after auth.

## Development overlay

`make dev` + `mode = "wayland"` in `theme.toml` — test UI inside a running compositor without taking over tty1.

## Related

- [SLiM2](https://github.com/sickhate/slim2) — persistent DRM/KMS greeter (same author)
- Classic [SLiM](https://wiki.archlinux.org/title/SLiM) — historical X11 login manager

## License

MIT — see [LICENSE](LICENSE).
