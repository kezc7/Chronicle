param([ValidateSet('Debug','Release','RelWithDebInfo')][string]$Config = 'Debug')
$ErrorActionPreference = 'Stop'
$projectRoot = $PSScriptRoot
& "$projectRoot\build.ps1" -Config $Config
if ($LASTEXITCODE -ne 0) { throw 'Build failed; package not created' }
$cmakeText = Get-Content -LiteralPath "$projectRoot\CMakeLists.txt" -Raw
if ($cmakeText -notmatch 'VERSION\s+(\d+\.\d+\.\d+)') { throw 'Cannot find project version' }
$version = $Matches[1]
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$name = "Chronicle-$version-$Config-$stamp"
$stage = Join-Path $projectRoot "out\staging\$name"
New-Item -ItemType Directory -Path "$stage\SKSE\Plugins", "$stage\PrismaUI\views\Chronicle" -Force | Out-Null
Copy-Item -LiteralPath "$projectRoot\out\build\x64-$Config\Chronicle.dll" -Destination "$stage\SKSE\Plugins\Chronicle.dll"
# Explicit file list excludes the obsolete standalone settings page and all preview/test assets.
foreach ($file in 'index.html','script.js','controls.js','bridge.js','styles.css','config.json') {
    Copy-Item -LiteralPath "$projectRoot\Data\PrismaUI\views\Chronicle\$file" -Destination "$stage\PrismaUI\views\Chronicle\$file"
}
Copy-Item -LiteralPath "$projectRoot\README.md", "$projectRoot\LICENSE" -Destination $stage
$zip = Join-Path $projectRoot "out\$name.zip"
# Build the archive directly so entry names always use '/' (Windows PowerShell 5.1 Compress-Archive writes '\').
Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
$archive = [System.IO.Compression.ZipFile]::Open($zip, [System.IO.Compression.ZipArchiveMode]::Create)
try {
    foreach ($file in Get-ChildItem -LiteralPath $stage -File -Recurse) {
        $relative = $file.FullName.Substring($stage.Length + 1).Replace('\','/')
        [System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive, $file.FullName, $relative, [System.IO.Compression.CompressionLevel]::Optimal) | Out-Null
    }
} finally { $archive.Dispose() }
# Read the actual archive and verify the native and UI payload against this build.
$archive = [System.IO.Compression.ZipFile]::OpenRead($zip)
try {
    foreach ($file in Get-ChildItem -LiteralPath $stage -File -Recurse) {
        $relative = $file.FullName.Substring($stage.Length + 1).Replace('\','/')
        $entry = $archive.GetEntry($relative)
        if (!$entry) { throw "Missing archive entry: $relative" }
        $stream = $entry.Open()
        $sha = [System.Security.Cryptography.SHA256]::Create()
        try { $actual = [BitConverter]::ToString($sha.ComputeHash($stream)).Replace('-','') }
        finally { $stream.Dispose(); $sha.Dispose() }
        if ($actual -ne (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash) { throw "Archive content mismatch: $relative" }
    }
} finally { $archive.Dispose() }
Write-Host "[Chronicle] Verified package: $zip"
Get-FileHash -LiteralPath $zip -Algorithm SHA256
