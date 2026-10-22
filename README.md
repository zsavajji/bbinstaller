# BB Installer

Install `.bar` apps on a BlackBerry 10 phone over USB, no developer mode needed.
A small Qt 6 app for Windows, macOS (Apple silicon) and Linux. The protocol is ported from
[Sachesi](https://github.com/xsacha/Sachesi)'s `InstallNet`.

1. Plug in the phone with a USB cable. The OS brings up the phone's USB network link
   (macOS: CDC-NCM, Linux: `rndis_host`/`cdc_ncm`, Windows: the BlackBerry USB/RNDIS driver).
2. Drop `.bar` files on the window, or click it to choose them. OS/radio bars are refused; use Sachesi for those.
3. Enter the device password if one is set, then press **Install**.

A wrong password is never retried automatically: too many wrong attempts wipe the phone.

## Downloads

Every push builds a Linux AppImage, a macOS arm64 `.dmg` and a Windows x64 `.zip` with
GitHub Actions (see the run's *Artifacts*). Pushing a `v*` tag publishes them as a release.

The window uses each platform's own style and follows its light/dark setting.

The macOS build is ad-hoc signed, so the first launch needs right-click → **Open**.

## Building

Needs CMake 3.21+, Qt 6.2+ (Widgets, Network; Test for the tests) and zlib.
On Windows, Qt's bundled zlib is used.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build
```

The phone only speaks TLS 1.0 with a self-signed certificate. The OpenSSL TLS backend is used
whenever its libraries are present (on Windows they ship next to the exe). On macOS without
OpenSSL, Qt's native Secure Transport backend is used.
