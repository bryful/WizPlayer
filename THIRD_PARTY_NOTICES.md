# Third-party licenses and GBWiz licensing

GBWiz original code, documentation, and original assets are licensed under the
MIT License in LICENSE. This grant does not relicense third-party code, third-party
assets, or third-party portions of modified files.

## Gambatte — GPL-2.0-only

- Upstream: https://github.com/libretro/gambatte-libretro
- Imported revision: 7809bb68ee96c3e6c74e9d9c4c8bb09aa7c6801d
- License: third_party/gambatte/COPYING
- Original copyright notices and component-specific notices remain in their files.

GBWiz links Gambatte into its Windows and Android applications. Distribution of
these combined applications must satisfy GPL version 2, including the applicable
corresponding-source requirements. The MIT license for GBWiz's original portions
is retained; it does not make Gambatte or the combined application MIT-only.

GBWiz modifications to Gambatte are listed in doc/gambatte-changes.md and marked
in the modified files. Those files remain subject to their existing GPLv2 terms.

## SDL2 — zlib

- Upstream: https://github.com/libsdl-org/SDL
- Version: 2.32.10, official Visual C++ development package
- License and copyright: third_party/SDL2-2.32.10/LICENSE.txt
- The imported SDL2 files have not been modified by GBWiz.

## blipper — MIT

- Included through Gambatte under third_party/gambatte/libgambatte/libretro/
- Copyright (c) 2013 Hans-Kristian Arntzen
- License text: the header of blipper.c, also copied to blipper-LICENSE.txt in releases

## MAME Turbo File device — BSD-3-Clause

- Copyright (c) kmg
- C adapter imported from Wiiz. Original source is MAME's `src/devices/bus/nes_ctrl/turbofile.cpp` at revision `cd680acf012140761f9dee37dadd4c11a1207fbd`.
- License: `third_party/mame-turbofile/LICENSE`

## binjnes — MIT

- Upstream: https://github.com/binji/binjnes
- Upstream revision: e2f5871a28ff189daa82b70be9324a42e2aaf9fd
- Imported from NesDQ commit `ccedbd0`, including observer changes originating in Wiiz.
- WizPlayer uses the cartridge header and omits the CRC database. The imported palette has its own MIT-0 notice.
- License texts: [binjnes](third_party/binjnes/LICENSE), [palette](third_party/binjnes/PALETTE-LICENSE.txt).
- The Windows FC adapter uses the core bridge from NesDQ under the [NesDQ MIT license](src/nes/LICENSE).
- The Wizardry FC map and inventory access code incorporates portions of Wiiz under the [Wiiz MIT license](src/nes/Wiiz-LICENSE).

## Snes9x (Android and Windows releases)

- Source: https://github.com/snes9xgit/snes9x
- Revision used for Android 0.1.0: `1bcc369e89f08243e0a462882fb1f3e42e51de3a`.
- See the Snes9x source tree's `LICENSE` for its non-commercial terms and component notices.
- The local build replaces runtime ROM CRC and SHA calculations in the core's cartridge metadata.
- The Wizardry V map and movement code incorporates portions of WiizV under the [WiizV MIT license](src/sfc/WiizV-LICENSE).

Other imported files retain their own notices, including Gradle wrapper and
libretro-common files. LICENSE does not override those notices.

## Corresponding source for WizPlayer 0.1.0

- The Android release includes `WizPlayer-0.1.0-Android-source.zip` alongside the APK.
- Android release build instructions are in `doc/android-release.md` in that archive.
- The Windows release includes `WizPlayer-0.1.0-Windows-source.zip` alongside the executable archive. Its build instructions are in `doc/windows-release.md`.

Each source archive includes the application source for its release, modified
Gambatte source, Snes9x source with its license, the seven embedded public-domain
IPS fixes, and the build scripts. ROMs and private signing materials are excluded.
