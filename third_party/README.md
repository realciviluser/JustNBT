# Third-party code

## leveldb

Google LevelDB 1.23 (BSD-3-Clause, see `leveldb/LICENSE`), vendored with small
changes so it can read and write Minecraft Bedrock worlds. Every change is marked
with a `JustNBT` comment:

- `include/leveldb/options.h` — compression types 2 (zlib) and 4 (raw deflate) used by Mojang.
- `util/mojang_zlib.{h,cc}` — those codecs, implemented with libdeflate.
- `table/format.cc`, `table/table_builder.cc` — read/write blocks with them.
- `util/env_windows.cc` — wide-char Win32 API, so paths with non-ASCII characters work;
  `std::memory_order_relaxed` spelling fixed for C++20 (as in upstream later; same in `util/env_posix.cc`).
- `CMakeLists.txt` — the new source file; RTTI is no longer switched off, because JustNBT
  derives its own `leveldb::Logger` and GNU ld on Linux needs the base class's type info.

Benchmarks, git submodules and `.git` were not copied; tests are kept but not built.
