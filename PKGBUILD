# Maintainer: rakhenmanoa
pkgname=adreno-smi
pkgver=0.1
pkgrel=1
pkgdesc="Moniteur GPU Adreno en ligne de commande façon nvidia-smi (fréquence, température, charge, processus)"
arch=('aarch64' 'x86_64')
url="https://github.com/rakhenmanoa/u-gputop"
license=('unknown')
depends=('glibc')
makedepends=('git' 'gcc')
source=("$pkgname::git+$url.git")
sha256sums=('SKIP')

pkgver() {
  cd "$srcdir/$pkgname"
  printf "0.1.r%s.%s" "$(git rev-list --count HEAD)" "$(git rev-parse --short HEAD)"
}

build() {
  cd "$srcdir/$pkgname"
  ${CC:-cc} $CFLAGS $CPPFLAGS $LDFLAGS -o adreno-smi adreno-smi.c
}

package() {
  cd "$srcdir/$pkgname"
  install -Dm755 adreno-smi "$pkgdir/usr/bin/adreno-smi"
  install -Dm644 README.md "$pkgdir/usr/share/doc/$pkgname/README.md"
}
