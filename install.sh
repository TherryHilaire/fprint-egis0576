#!/usr/bin/env bash
#
# Builds and installs libfprint with the egis0576 driver.
# Usage: ./install.sh [path-to-libfprint-checkout]
#
# If no checkout path is given, upstream libfprint is cloned shallowly
# next to this script.

set -euo pipefail

REPO_DIR="$(cd "$(dirname "$0")" && pwd)"
LIBFPRINT_DIR="${1:-$REPO_DIR/libfprint}"

say() { printf '\n==> %s\n' "$*"; }

# ---------------------------------------------------------------- deps
for tool in meson ninja git gcc pkg-config; do
  command -v "$tool" >/dev/null || {
    echo "Missing '$tool'. On Arch: sudo pacman -S meson ninja git base-devel"
    exit 1
  }
done

missing_pkg=()
for pkg in glib-2.0 gio-2.0 gobject-2.0 gusb pixman-1 libusb-1.0; do
  pkg-config --exists "$pkg" || missing_pkg+=("$pkg")
done
if [ ${#missing_pkg[@]} -gt 0 ]; then
  echo "Missing libraries: ${missing_pkg[*]}"
  echo "On Arch: sudo pacman -S glib2 libusb gusb pixman"
  exit 1
fi

# ------------------------------------------------- libfprint checkout
if [ ! -d "$LIBFPRINT_DIR" ]; then
  say "Cloning upstream libfprint (shallow)"
  git clone --depth 1 https://gitlab.freedesktop.org/libfprint/libfprint.git \
    "$LIBFPRINT_DIR"
fi

cd "$LIBFPRINT_DIR"

# ------------------------------------------------------ driver files
say "Installing egis0576 driver files"
cp "$REPO_DIR/driver/egis0576.c"  libfprint/drivers/
cp "$REPO_DIR/driver/egis0576.h"  libfprint/drivers/

# --------------------------------------------------- meson registers
grep -q "egis0576" libfprint/meson.build || \
  sed -i "s|'egis0570' : files('drivers/egis0570.c'),|&\n    'egis0576' : files('drivers/egis0576.c'),|" \
    libfprint/meson.build

grep -q "'egis0576'" meson.build || \
  sed -i "s|'egis0570': {},|&\n    'egis0576': {},|" meson.build

# Upstream bug (fixed by our series): dict iteration fails when the
# virtual-image tests are skipped, i.e. with -Dintrospection=false.
sed -i 's|foreach driver_test: drivers_tests$|foreach driver_test: drivers_tests.keys()|' \
  tests/meson.build

# ------------------------------------------------------------ build
say "Configuring and building"
meson setup build -Ddrivers=default -Dintrospection=false -Ddoc=false 2>/dev/null ||
  meson configure build -Ddrivers=default -Dintrospection=false -Ddoc=false
ninja -C build

# ------------------------------------------------------------ install
cat <<EOF

Build complete. Finish installation with:

  cd $LIBFPRINT_DIR && sudo ninja -C build install && sudo ldconfig

If your distribution does not search /usr/local/lib, also run:

  echo "/usr/local/lib" | sudo tee /etc/ld.so.conf.d/99-local.conf

Then restart fprintd and enroll:

  sudo systemctl restart fprintd.service
  fprintd-enroll
  fprintd-verify
EOF
