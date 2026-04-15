# Chronicle build script
param(
    [string]$Config = "Debug"
)

$vsBase    = "C:\Program Files\Microsoft Visual Studio\18\Community"
$cmake     = "$vsBase\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
$ninja     = "$vsBase\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
$vcpkgRoot = "$vsBase\VC\vcpkg"
$toolchain = "$vcpkgRoot\scripts\buildsystems\vcpkg.cmake"
$devCmd    = "$vsBase\Common7\Tools\VsDevCmd.bat"
$src       = Split-Path -Parent $MyInvocation.MyCommand.Path
$build     = "$src\out\build\x64-$Config"

Write-Host "[Chronicle] Loading MSVC environment..."

# Capture environment from VsDevCmd
$envLines = cmd /c "`"$devCmd`" -arch=x64 -host_arch=x64 2>nul && set"
foreach ($line in $envLines) {
    if ($line -match '^([^=]+)=(.*)$') {
        [System.Environment]::SetEnvironmentVariable($Matches[1], $Matches[2], 'Process')
    }
}

$env:VCPKG_ROOT = $vcpkgRoot

Write-Host "[Chronicle] Configuring..."
& $cmake `
    -S $src `
    -B $build `
    -G Ninja `
    "-DCMAKE_MAKE_PROGRAM=$ninja" `
    "-DCMAKE_TOOLCHAIN_FILE=$toolchain" `
    -DVCPKG_TARGET_TRIPLET=x64-windows-static `
    "-DCMAKE_BUILD_TYPE=$Config"

if ($LASTEXITCODE -ne 0) {
    Write-Error "[Chronicle] Configure FAILED (exit $LASTEXITCODE)"
    exit $LASTEXITCODE
}

Write-Host "[Chronicle] Building..."
& $cmake --build $build --parallel

if ($LASTEXITCODE -ne 0) {
    Write-Error "[Chronicle] Build FAILED (exit $LASTEXITCODE)"
    exit $LASTEXITCODE
}

$dll = "$build\Chronicle.dll"
if (Test-Path $dll) {
    Write-Host ("[Chronicle] SUCCESS -- " + $dll)
} else {
    Write-Warning ("[Chronicle] Build reported success but DLL not found at " + $dll)
}
