# Maintainer: sickhate <archate@gmail.com>
# Local build: run `makepkg -si` from this directory (no git required).
# AUR publish: use source=("git+https://github.com/sickhate/slimm.git") and drop prepare().

pkgname=slimm
pkgver=0.2.5
pkgrel=7
pkgdesc="Stateless lightweight login bootstrapper — DRM/KMS greeter, STE2 themes, SLiM lineage"
arch=('x86_64')
url="https://github.com/sickhate/slimm"
license=('MIT')
depends=(
    'libxkbcommon'
    'pam'
    'libdrm'
    'libinput'
    'mesa'          # EGL, GLES, GBM
    'systemd-libs'  # libudev
)
makedepends=(
    'base-devel'
    'pkgconf'
    'freetype2'
    'fontconfig'
    'ttf-jetbrains-mono-nerd'
)
optdepends=(
    'systemd: slimm.service on tty1 (enable with systemctl enable slimm)'
    'hyprland: common Wayland session target'
    'sway: alternative Wayland session target'
    'slim2: optional fallback display manager from same author'
)
options=('!debug' '!emptydirs')
backup=(
    'etc/slimm/theme.toml'
    'etc/slimm/theme.slimt'
    'etc/pam.d/slimm'
)
source=()
sha256sums=()

build() {
    cd "$startdir"
    make MINIMAL=1 PREFIX=/usr VERSION="$pkgver" V=0 clean all
}

check() {
    cd "$startdir"
    ./slimm --help >/dev/null
    test -x ./slimm
    test -x ./slimc
    test -f theme.slimt
    ./slimc --os-logo-id >/dev/null
    python3 - <<'PY'
import struct
with open("theme.slimt", "rb") as f:
    h = f.read(192)
logo_w, logo_h = struct.unpack_from("<II", h, 28)
if logo_w == 0 or logo_h == 0:
    raise SystemExit("theme.slimt has no baked logo")
PY
}

package() {
    cd "$startdir"
    make DESTDIR="$pkgdir" PREFIX=/usr SYSCONFDIR=/etc V=0 install
    install -Dm644 LICENSE "$pkgdir/usr/share/licenses/$pkgname/LICENSE"
    install -Dm644 README.md "$pkgdir/usr/share/doc/$pkgname/README.md"
    install -Dm644 CHANGELOG.md "$pkgdir/usr/share/doc/$pkgname/CHANGELOG.md"
}
