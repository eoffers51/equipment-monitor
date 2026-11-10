# Build and run the host tests on the PC (no board needed).
# Usage:  .\host_test\run_tests.ps1          (add -Clean to rebuild from scratch)
param(
    [switch]$Clean,
    [string]$IdfPath = $(if ($env:IDF_PATH) { $env:IDF_PATH } else { 'C:\esp\v6.0.2\esp-idf' }),
    [string]$MingwBin = 'C:\CTOOLS\mingw32\bin'
)
$ErrorActionPreference = 'Stop'

$src = $PSScriptRoot
$build = Join-Path $src 'build'

# Put MinGW first on PATH: with the ESP-IDF env active, gcc would otherwise pick up the wrong assembler
if (-not (Test-Path (Join-Path $MingwBin 'gcc.exe'))) { throw "gcc.exe not found in $MingwBin (pass -MingwBin)" }
$env:PATH = "$MingwBin;$env:PATH"

if ($Clean -and (Test-Path $build)) { Remove-Item -Recurse -Force $build }

# Configure (first run only), build, then run the tests
if (-not (Test-Path (Join-Path $build 'CMakeCache.txt'))) {
    cmake -S $src -B $build -G Ninja -DCMAKE_C_COMPILER=gcc "-DIDF_PATH=$($IdfPath -replace '\\', '/')"
    if ($LASTEXITCODE) { exit $LASTEXITCODE }
}
cmake --build $build
if ($LASTEXITCODE) { exit $LASTEXITCODE }

# Run the test program directly so every test's PASS/FAIL line is shown
& (Join-Path $build 'host_test.exe')
exit $LASTEXITCODE
