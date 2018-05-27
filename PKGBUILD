# Maintainer: Shenghao Yang <me@shenghaoyang.info>
pkgname=e131_blinkt-git
pkgver=0.0.1.r0.g6c1d8c4
pkgrel=1
pkgdesc="E1.31 to Blinkt! gateway"
arch=('x86_64' 'arm' 'armv6h' 'armv7h' 'aarch64')
url='https://github.com/shenghaoyang/e131_blinkt'
license=('MIT')
depends=('libe131' 'libgpiod-git' 'libconfig' 'docopt')
makedepends=('git')
provides=("${pkgname%-git}")
conflicts=("${pkgname%-git}")
source=("git+${url}.git")
md5sums=('SKIP')

pkgver() {
    cd "$srcdir/${pkgname%-git}"
    git describe --long | sed 's/\([^-]*-g\)/r\1/;s/-/./g'
}

build() {
    cd "$srcdir/${pkgname%-git}"
    scons
}

package() {
    cd "$srcdir/${pkgname%-git}"
    scons --destdir="$pkgdir/" install
}