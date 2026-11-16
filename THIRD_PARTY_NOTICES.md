# Third-party notices

Chronicle's original native code, web UI, tests, build scripts, and documentation
are licensed under the GNU General Public License, version 3 or any later version.
See `LICENSE`.

The following third-party components are used by the build or distribution. Their
notices and license terms remain separate from Chronicle's license.

## PrismaUI API header

`src/PrismaUI_API.h` is copied from the [PrismaUI SKSE example plugin](https://github.com/PrismaUI-SKSE/example-skse-plugin).
The upstream header includes this permission notice: “For modders: Copy this file
into your own project if you wish to use this API.” The header is retained as a
separately identified interface artifact and is not relicensed by Chronicle's GPL
notice.

## Magelight UI API header

`src/MagelightUI_API.h` is copied from [Magelight UI](https://github.com/Severause/MagelightUI)
(`api/MagelightUI_API.h`, release 1.7.1), Copyright (c) 2026 Severause, MIT License. It is used
only by the Magelight build variant and is retained under its upstream terms;
it is not relicensed by Chronicle's GPL notice. Chronicle's release archives do
not redistribute the Magelight runtime.

## DovahHub API header

`src/DovahHub_API.h` is copied unchanged from [DovahHub](https://github.com/kezc7/DovahHub)
(`src/api/DovahHub_API.h`), by the same author and under the same GNU GPL, version 3.
Chronicle reaches DovahHub.dll through it at runtime only when DovahHub is installed.

## DovahMenu API header

`src/DovahMenu_API.h` is copied unchanged from [DovahMenu](https://github.com/kezc7/DovahMenu)
(`include/DovahMenu_API.h`), by the same author and under the same GNU GPL, version 3.
Chronicle reaches DovahMenu.dll through it at runtime only when DovahMenu is installed.

## Build and link dependencies

- CommonLibSSE-NG: see the next section.
- [nlohmann/json](https://github.com/nlohmann/json): MIT License.
- [spdlog](https://github.com/gabime/spdlog): MIT License; it uses [fmt](https://github.com/fmtlib/fmt),
  which is also MIT-licensed.
- [rapidcsv](https://github.com/d99dc/rapidcsv): BSD 3-Clause License.
- [xbyak](https://github.com/herumi/xbyak): BSD 3-Clause License.

## CommonLibSSE-NG

Both builds (PrismaUI and Magelight) link CommonLibSSE-NG 3.6.0, built by vcpkg from
the colorglass registry pinned in `vcpkg-configuration.json` (baseline
`eb2f2bd143b8f7c52b57ef5a822163b56f7be7d6`). The source it builds is commit
`d51fa0938bf53f829ff83a144079136f0d4a2f18` ("Bump version numbers for 3.6.0 release.",
20 Nov 2022) of [CommonLibSSE-NG](https://github.com/alandtse/CommonLibSSE-NG). That
commit predates upstream's relicensing to GPL-3.0-or-later with its modding and linking
exceptions (commit `12f8518`, 25 Jul 2026), so the code Chronicle links is under the
MIT License, as it stands at that commit:

```text
MIT License

Copyright (c) 2018 Ryan-rsm-McKenzie

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

A change to the vcpkg baseline can move CommonLibSSE-NG to a newer commit under other
terms; this notice is checked again whenever the baseline changes.

## Theme kit, fonts and the build tree

The settings window uses the Dovah suite theme kit (`theme/dovah-theme.css`, from
DovahHub) with the [Barlow Condensed](https://github.com/jpt/barlow) font, which ships
beside it under the SIL Open Font License 1.1 (`theme/fonts/OFL-BarlowCondensed.txt`).

The exact notices for the resolved packages are available in the vcpkg install
tree under `share/<package>/copyright`. Chronicle's release archive does not
redistribute that build tree or the Skyrim, SKSE, Address Library, or PrismaUI
runtime files; those must be obtained under their own terms.
