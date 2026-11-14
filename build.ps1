param([string]$Zig)
$ErrorActionPreference = 'Stop'
if (-not $Zig) { $Zig = Join-Path $PSScriptRoot '..\..\work\toolchains\zig-x86_64-windows-0.15.2\zig.exe' }
$Zig = (Resolve-Path -LiteralPath $Zig).Path
$env:ZIG_GLOBAL_CACHE_DIR = Join-Path $PSScriptRoot '..\..\work\toolchains\zig-cache'
$dist = Join-Path $PSScriptRoot 'dist'
$testBin = Join-Path $PSScriptRoot 'tests\bin'
New-Item -ItemType Directory -Force -Path $dist,$testBin | Out-Null
Push-Location (Join-Path $PSScriptRoot 'src')
try {
    & $Zig rc /fo app.res app.rc
    if ($LASTEXITCODE) { throw 'Resource compilation failed.' }
    & $Zig c++ -target x86_64-windows-gnu -std=c++17 -O2 -static -s -Wall -Wextra -Werror -Wno-unused-command-line-argument -municode '-Wl,--subsystem,windows' app.cpp decoder.cpp app.res -lcomdlg32 -lole32 -luuid -lcomctl32 -lshell32 -lgdi32 -luser32 -o (Join-Path $dist 'RfeDecoder.exe')
    if ($LASTEXITCODE) { throw 'App compilation failed.' }
    & $Zig c++ -target x86_64-windows-gnu -std=c++17 -O2 -static -s -Wall -Wextra -Werror (Join-Path $PSScriptRoot 'tests\driver.cpp') decoder.cpp -I . -o (Join-Path $testBin 'decoder_test_driver.exe')
    if ($LASTEXITCODE) { throw 'Test driver compilation failed.' }
} finally { Pop-Location }
Get-FileHash -LiteralPath (Join-Path $dist 'RfeDecoder.exe') -Algorithm SHA256
