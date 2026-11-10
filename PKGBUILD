# Maintainer: rakhenmanoa
pkgname=adreno-smi
pkgver=0.1
pkgrel=1
pkgdesc="Moniteur GPU Adreno en ligne de commande façon nvidia-smi (fréquence, température, charge, processus)"
arch=('aarch64' 'x86_64')
url="https://github.com/rakhenmanoa/u-gputop"
license=('0BSD')
depends=('glibc')
makedepends=('git' 'gcc')
# Le clone ne doit pas s'appeler comme le binaire compilé : makepkg télécharge les
# sources dans le dossier courant, où un "adreno-smi" peut déjà exister.
_src="$pkgname-src"
source=("$_src::git+$url.git")
sha256sums=('SKIP')

pkgver() {
  cd "$srcdir/$_src"
  printf "0.1.r%s.%s" "$(git rev-list --count HEAD)" "$(git rev-parse --short HEAD)"
}

build() {
  cd "$srcdir/$_src"
  ${CC:-cc} $CFLAGS $CPPFLAGS $LDFLAGS -o adreno-smi adreno-smi.c
}

package() {
  cd "$srcdir/$_src"
  install -Dm755 adreno-smi "$pkgdir/usr/bin/adreno-smi"
  install -Dm644 README.md "$pkgdir/usr/share/doc/$pkgname/README.md"
  install -Dm644 LICENSE "$pkgdir/usr/share/licenses/$pkgname/LICENSE"
}
