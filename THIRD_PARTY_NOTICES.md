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
(`api/MagelightUI_API.h`), Copyright (c) 2026 Severause, MIT License. It is used
only by the Magelight build variant and is retained under its upstream terms;
it is not relicensed by Chronicle's GPL notice. Chronicle's release archives do
not redistribute the Magelight runtime.

## Build and link dependencies

- [CommonLibSSE-NG](https://github.com/community-shaders/CommonLibSSE-NG), resolved
  by the pinned vcpkg configuration. The current local vcpkg package is version
  3.6.0 and carries the MIT notice supplied by that package. Newer upstream
  revisions may use the GPL-3.0-or-later license with the documented modding and
  linking exceptions; check the resolved package before changing the baseline.
- [nlohmann/json](https://github.com/nlohmann/json): MIT License.
- [spdlog](https://github.com/gabime/spdlog): MIT License; it uses [fmt](https://github.com/fmtlib/fmt),
  which is also MIT-licensed.
- [rapidcsv](https://github.com/d99dc/rapidcsv): BSD 3-Clause License.
- [xbyak](https://github.com/herumi/xbyak): BSD 3-Clause License.

The exact notices for the resolved packages are available in the vcpkg install
tree under `share/<package>/copyright`. Chronicle's release archive does not
redistribute that build tree or the Skyrim, SKSE, Address Library, or PrismaUI
runtime files; those must be obtained under their own terms.
