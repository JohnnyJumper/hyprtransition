# Maintainer: Johnny <jeyhunt@gmail.com>
pkgname=hyprtransition-git
pkgver=r1
pkgrel=1
pkgdesc="Shader-driven workspace transitions for Hyprland (tear, burn, tiles, or your own GLSL)"
arch=('x86_64' 'aarch64')
url="https://github.com/jeyhunt/hyprtransition"
license=('MIT')
depends=('wayland' 'libglvnd' 'grim' 'lua' 'hyprland')
makedepends=('git' 'wayland-protocols' 'pkgconf')
provides=('hyprtransition')
conflicts=('hyprtransition')
source=("git+$url.git")
sha256sums=('SKIP')

pkgver() {
    cd "${pkgname%-git}"
    printf "r%s.%s" "$(git rev-list --count HEAD)" "$(git rev-parse --short HEAD)"
}

build() {
    cd "${pkgname%-git}"
    make PREFIX=/usr
}

package() {
    cd "${pkgname%-git}"
    make PREFIX=/usr DESTDIR="$pkgdir" install
    install -Dm644 LICENSE "$pkgdir/usr/share/licenses/$pkgname/LICENSE"
}
