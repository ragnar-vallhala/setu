# ICU in the personality

Windows 10 1703 and later ship ICU, the International Components for
Unicode, as system DLLs that any program may use
([Microsoft's ICU page](https://learn.microsoft.com/windows/win32/intl/international-components-for-unicode--icu-)):
`icu.dll` (Windows 10 1903 and later), and the older `icuuc.dll`
("common") and `icuin.dll` ("i18n"). Programs built against them, such as
Qt 6 (KeePassXC) and Windows Terminal, do not start without them.

`tools/build-icu.sh` builds them from the upstream ICU source release
(Unicode License) with the mingw-w64 toolchain, and installs them into the
personality's PE folders (`lib/wine/x86_64-windows`, `lib/wine/i386-windows`),
marked as Wine builtins so that wineboot copies them into each prefix's
`system32` and `syswow64`. `tools/build-personality.sh` runs it after
building the fork; it does nothing when its stamp matches.

| File | What |
|---|---|
| `exports.txt` | the C functions Microsoft's page lists (its api_name list) |
| `mkdefs.py` | writes the `.def` files: what each DLL exports |
| `icudata.c` | `icu.dll`'s entry point: the 32-bit DLL's data |
| `forward.c` | the entry point of `icuuc.dll` and `icuin.dll` |

## What matches Windows (oracle test `icu/icu_basic`)

* **Version.** Windows 11 26H2 reports ICU 72.1 (`u_getVersion`), with
  CLDR 42. Setu ships upstream ICU 72.1, the same release. Windows' copy
  has newer Unicode data (Unicode and UCA 15.1; upstream 72.1 has 15.0):
  that difference stays, since only upstream source is used.
* **Exports.** Only the C API, under plain names: Windows' ICU has no C++
  API and no version suffix (`u_getVersion`, never `u_getVersion_72`).
  ICU is built with renaming off, as static libraries without
  `dllexport`, and `icu.dll` is linked with a `.def` file listing the
  documented names that ICU 72 defines (all 1053).
* **The older DLLs.** `icuuc.dll` has the "common" half, `icuin.dll` the
  "i18n" half, each only the functions from before Windows stopped adding
  to them (Microsoft: no new APIs after 1903). The oracle shows functions
  from ICU 59 and older in them and none from ICU 61 and later (there are
  none from 60), so `mkdefs.py` cuts at ICU 59, using the release each
  function first appeared in according to ICU's own header annotations.
  Their exports forward to `icu.dll`: a program using both gets one ICU,
  one copy of the data and one set of caches.

## Data

ICU's data (locales, collation, break iteration, time zones, converters)
is the release's prebuilt `icudt72l.dat`, unfiltered: 31,251,968 bytes.
It is the same for 32-bit and 64-bit code, so there is one copy: in the
64-bit `icu.dll`, in a section named `.icudata`, under the symbol ICU's
static data packaging uses. The 32-bit `icu.dll` maps the 64-bit DLL from
`%windir%\Sysnative` as a plain file in its `DllMain` (no code of it runs)
and hands that section to ICU with `udata_setCommonData`.

## Known differences

* Unicode / UCA version 15.0 instead of Windows' 15.1 (see above).
* `uloc_getDefault`: Windows appends the user's regional settings as
  keywords (`en_US@calendar=gregorian;currency=usd;fw=sun;hours=h12;measure=ussystem`);
  upstream ICU returns `en_US`.
* English time formats: upstream ICU 72 (CLDR 42) puts U+202F NARROW
  NO-BREAK SPACE before AM/PM (`3:04 PM` with a narrow space); Windows'
  ICU has a plain space there. Matching it would mean patching the
  locale data and building it from source instead of using the release's
  prebuilt data.

## Updating ICU

Change `ICU_VERSION`, `ICU_TAR`, `ICU_URL`, `ICU_SHA256` and `ICU_SIZE` in
`tools/build-icu.sh` (check the SHA-512 against the release's
`SHASUM512.txt`), update NOTICE, and run the oracle test.
