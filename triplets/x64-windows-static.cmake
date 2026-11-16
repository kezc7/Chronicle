# vcpkg's x64-windows-static, with the toolset pinned: the dependencies are built with
# v143 (Visual Studio 2022), the compiler build.ps1 loads for Chronicle. build.ps1 also
# names the Visual Studio install to take it from, because v143 alone can match an older
# 14.3x compiler inside a newer install. A library built by a newer compiler than the
# linker can't be linked.
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE static)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_PLATFORM_TOOLSET v143)
