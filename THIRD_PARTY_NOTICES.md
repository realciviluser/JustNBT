# Third-party software in JustNBT

JustNBT itself is licensed under the GNU General Public License v3.0 (see `LICENSE`).
It is built with the components below. Their full license texts are in the `LICENSES`
folder, which is shipped with every download of JustNBT.

| Component | Version | License | Used for | Text |
|---|---|---|---|---|
| [Qt](https://www.qt.io/) | 6.8.3 | LGPL-3.0-only | the whole user interface | `LICENSES/LGPL-3.0.txt`, `LICENSE` |
| [LevelDB](https://github.com/google/leveldb) | 1.23, modified | BSD-3-Clause | reading and writing Bedrock worlds | `LICENSES/LevelDB-BSD-3-Clause.txt` |
| [libdeflate](https://github.com/ebiggers/libdeflate) | 1.24 | MIT | gzip, zlib and deflate | `LICENSES/libdeflate-MIT.txt` |
| [LZ4](https://github.com/lz4/lz4) | 1.10.0 (lib only) | BSD-2-Clause | region files compressed with LZ4 | `LICENSES/LZ4-BSD-2-Clause.txt` |
| MinGW-w64 GCC runtime (Windows download only) | GCC 13.1 | GPL-3.0 with the GCC Runtime Library Exception 3.1 | `libstdc++-6.dll`, `libgcc_s_seh-1.dll` | `LICENSE`, `LICENSES/GCC-exception-3.1.txt` |
| MinGW-w64 winpthreads (Windows download only) | — | MIT and BSD-3-Clause | `libwinpthread-1.dll` | `LICENSES/winpthreads.txt` |

## Qt

JustNBT uses the Qt libraries under the GNU Lesser General Public License v3.0. Qt is linked
dynamically: in the Windows download the Qt libraries are the `Qt6*.dll` files and the
plugin folders next to `JustNBT.exe`, in the Linux AppImage they are inside the image
(`usr/lib`, `usr/plugins`). You may replace them with your own build of the same Qt
version, or a compatible one.

The source code of Qt 6.8.3 is available from The Qt Company at
<https://download.qt.io/archive/qt/6.8/6.8.3/single/>. The Qt libraries shipped with JustNBT
are unmodified builds from qt.io.

Qt itself contains third-party code (for example FreeType, HarfBuzz, libpng, PCRE2 and zlib);
their licenses are listed in the Qt documentation under "Licenses Used in Qt":
<https://doc.qt.io/qt-6/licenses-used-in-qt.html>.

## LevelDB

JustNBT includes a modified copy of LevelDB in `third_party/leveldb`. The changes teach it the
compression formats Minecraft Bedrock uses and are described in `third_party/README.md`;
every changed place is marked with a `JustNBT` comment.

## Minecraft

JustNBT is not an official Minecraft product and is not approved by or associated with
Mojang or Microsoft. It contains no files of the game: item names and pictures are read
at run time from a Minecraft Java installation on the user's own computer, and only when
one is present.
