# BB Installer

Install `.bar` apps on a BlackBerry 10 phone over USB, no developer mode needed.
A small Qt 6 app for Windows, macOS (Apple silicon and Intel) and Linux. The protocol is ported from
[Sachesi](https://github.com/xsacha/Sachesi)'s `InstallNet`.

1. Plug in the phone with a USB cable. The OS brings up the phone's USB network link
   (macOS: CDC-NCM, Linux: `rndis_host`/`cdc_ncm`, Windows: the BlackBerry USB/RNDIS driver).
2. Drop `.bar` files on the window, or click it to choose them. OS/radio bars are refused; use Sachesi for those.
3. Enter the device password if one is set, then press **Install**.

A wrong password is never retried automatically: too many wrong attempts wipe the phone.

## Downloads

Every push builds these with GitHub Actions (see the run's *Artifacts*). A push to `main` or `qt6`
publishes them as release `v<version>` the first time that version appears: bump `VERSION` in
`CMakeLists.txt` to release again. Pushing a `v*` tag also publishes a release.

| Platform | Package | Look |
| --- | --- | --- |
| Windows x64 | `.zip` (OpenSSL included) | Windows style |
| macOS arm64 (Apple silicon) | `.dmg` | macOS style |
| macOS x64 (Intel) | `.dmg` | macOS style |
| Linux x86_64 | AppImage | Fusion |

The AppImage brings its own Qt and always uses Qt's Fusion style.
All of them follow the system light/dark setting.

The macOS build needs macOS 13 or later and is ad-hoc signed, so the first launch needs right-click → **Open**.

## Building

Needs CMake 3.21+, Qt 6.2+ (Widgets, Network; Test for the tests) and zlib.
On Windows, Qt's bundled zlib is used.
For the native look on macOS 26 and later, use Qt 6.10+ and the newest SDK: macOS picks its controls'
look from the SDK an app links against, and older Qt doesn't draw their progress bars. Add
`-DCMAKE_OSX_DEPLOYMENT_TARGET=13.0` to keep the result running on older macOS.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build
```

`-DBBINSTALLER_FUSION=ON` forces the Fusion style (used for the AppImage).
`packaging/linux-package.sh <tag>` builds a `.deb` or `.rpm` against the distro's own Qt, so the desktop's
theme applies. Run it inside a Debian/Ubuntu or Fedora system; CI doesn't build these.

The phone only speaks TLS 1.0 with a self-signed certificate. The OpenSSL TLS backend is used
whenever its libraries are present (on Windows they ship next to the exe). On macOS without
OpenSSL, Qt's native Secure Transport backend is used.
