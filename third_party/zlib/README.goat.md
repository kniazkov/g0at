# zlib 1.3.2 + classic MiniZip / поставка Goat

## Provenance / происхождение

Official repository: https://github.com/madler/zlib
Release: https://github.com/madler/zlib/releases/tag/v1.3.2
Pinned commit: `da607da739fa6047df13e66a2af6b8bec7c2a498` (tag `v1.3.2`).
The source delivery was a checkout of this exact tag. Its reproducible tar form:

```sh
git archive --format=tar da607da739fa6047df13e66a2af6b8bec7c2a498 > git-archive-v1.3.2.tar
sha256sum git-archive-v1.3.2.tar
```

SHA-256: `9a0323b8dcde2417150eeaa8ccccae0cb0d7f62b72c3dedafb4cd63df7ba0b45`.
This is the Git archive checksum, not the checksum of a GitHub release asset.
`UPSTREAM.sha256` also records the original bytes of every copied upstream file.
`LOCAL.patch` records the sole source change. The original upstream copyright
and license notices remain in all copied files.

Исходная поставка — checkout точного тега; выше зафиксированы commit и SHA-256
его воспроизводимого tar. Это не checksum автоматически созданного ZIP GitHub.
`UPSTREAM.sha256` фиксирует также каждый скопированный исходный файл до правки;
`LOCAL.patch` содержит единственную локальную правку исходников.

## Contents and local changes / состав и изменения

28 C/header files (1,227,516 bytes including the small local fix):

- zlib: adler32, crc32 (with generated table), deflate, inflate, inffast,
  inftrees, trees, zutil; their headers, zlib.h, zconf.h, gzguts.h.
- contrib/minizip: zip, unzip, ioapi and headers; crypt.h, ints.h, skipset.h.
- LICENSE, MiniZip64_info.txt and LICENSE.Info-Zip are retained upstream notices.
- LICENSE.PCG supplies the Apache-2.0 license text for the attributed PCG fragment
  in upstream skipset.h (canonical license: https://www.apache.org/licenses/LICENSE-2.0).
- Goat adds this README, CMakeLists.txt, checksums and LOCAL.patch.

Only `contrib/minizip/zip.c` changes: check the allocation of `central_header`
before writing through it, returning `ZIP_INTERNALERROR` on failure. No codec
algorithm, compression setting or wire-format behavior is changed.

Единственная правка исходников — проверка результата выделения `central_header`
в zip.c перед разыменованием. Возвращается `ZIP_INTERNALERROR`; алгоритмы кодека
и байты успешной записи не меняются. Полный текст изменения — в LOCAL.patch.

CMake compiles eight zlib C files and three MiniZip C files as static targets
`goat_zlib` and `goat_minizip`. Settings are target-local. No upstream build
scripts, downloads, assembly/CPU dispatch, gzip filesystem API, BZip2, crypto or
system zlib are used. MiniZip builds with NOCRYPT, NOUNCRYPT and USE_FILE32API;
its OS stream callbacks are never used by the Goat wrapper, which supplies
bounded memory callbacks with 64-bit positions on both 32/64-bit platforms.

CMake собирает статические цели из поставляемых файлов без сети, внешнего
архиватора и системной zlib. Настройки ограничены сторонними целями. Обёртка
использует собственные ограниченные memory callbacks, а не файловые функции
MiniZip; 64-битные позиции не зависят от размера Windows long.

## Choice / выбор

| Candidate | License, size and dependencies | Suitability |
| --- | --- | --- |
| [miniz 3.1.2](https://github.com/richgel999/miniz/tree/3.1.2) | MIT; 9 C/header files, 427,392 bytes; self-contained | Compact stored/Deflate reader/writer, but a different Deflate implementation; would change frozen profile-1 bytes. Convenience stat fields truncate long names. |
| [classic MiniZip + zlib 1.3.2](https://github.com/madler/zlib/tree/v1.3.2/contrib/minizip) | zlib license plus retained notices above; selected subset 28 C/header files, about 1.17 MiB; no external dependency after vendoring | Stored/Deflate, memory I/O callbacks, controlled raw Deflate/window/memory/strategy, caller dates/extras/comments/attributes; matches the frozen profile. Selected. |
| [minizip-ng](https://github.com/zlib-ng/minizip-ng) | zlib license; broader multi-file library, configurable compression/crypto dependencies | Portable and capable, but more optional infrastructure than this two-method in-memory API needs. |
| [libzip](https://libzip.org/) | BSD-3-Clause; broader multi-file archive editing library, zlib for Deflate | Rich modification/source API; larger dependency/build surface than needed. |

All four support portable ZIP reading/writing, but none replaces the strict
Goat container policy. Exact measured file counts above apply to the pinned
miniz release and our zlib subset, not installed binary sizes. MiniZip's automatic
flags/text hints are overridden in fixed-size header fields by the Goat wrapper;
all original bytes and lengths remain available on reading.

Выбран classic MiniZip + zlib 1.3.2: он соответствует уже закреплённому профилю
#244 и эталонному empty.gbin, поддерживает stored/Deflate и явные параметры.
miniz компактнее, но выдаёт другой поток Deflate; minizip-ng и libzip предлагают
более широкий API и больше инфраструктуры. Ни одна библиотека сама по себе
не является валидатором формата Goat. Проверка #182 остаётся отдельной задачей.
