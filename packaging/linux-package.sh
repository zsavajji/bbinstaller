#!/bin/sh
# Build, test and package against the distro's own Qt 6. Run inside the target distro
# (CI uses its container image): packaging/linux-package.sh <dist-tag>, e.g. ubuntu24.04
set -eu
tag=$1

if command -v apt-get >/dev/null; then
    gen=DEB
    export DEBIAN_FRONTEND=noninteractive
    apt-get update -q
    apt-get install -y -q --no-install-recommends cmake g++ make file dpkg-dev zlib1g-dev libgl-dev qt6-base-dev
elif command -v dnf >/dev/null; then
    gen=RPM
    dnf install -y -q cmake gcc-c++ make rpm-build zlib-devel qt6-qtbase-devel
else
    echo "unsupported distro" >&2
    exit 1
fi

cmake -S . -B build-pkg -DCMAKE_BUILD_TYPE=Release
cmake --build build-pkg --parallel
QT_QPA_PLATFORM=offscreen ctest --test-dir build-pkg --output-on-failure

version=$(sed -n 's/^project(bbinstaller VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)
arch=$(uname -m)
(cd build-pkg && cpack -G "$gen" -D CPACK_PACKAGE_FILE_NAME="bbinstaller-$version-$tag-$arch" \
    -D CPACK_DEBIAN_FILE_NAME="bbinstaller-$version-$tag-$arch.deb" \
    -D CPACK_RPM_FILE_NAME="bbinstaller-$version-$tag-$arch.rpm")
mkdir -p dist
mv build-pkg/*.deb build-pkg/*.rpm dist/ 2>/dev/null || true
ls -l dist
