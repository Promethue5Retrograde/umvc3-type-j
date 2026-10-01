param(
    [string]$CompilerPath = 'g++',
    [string]$OutputDir = (Join-Path $PSScriptRoot 'build')
)

$ErrorActionPreference = 'Stop'
$compiler = Get-Command $CompilerPath -CommandType Application -ErrorAction Stop
$source = Join-Path $PSScriptRoot 'ZeroConfirmASI.cpp'
if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
    throw 'ZeroConfirmASI.cpp must be next to build.ps1.'
}
$env:PATH = "$(Split-Path -Parent $compiler.Source);$env:PATH"
& $compiler.Source --version
if ($LASTEXITCODE -ne 0) { throw 'Could not query the compiler version.' }
New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null
$output = Join-Path $OutputDir 'ZeroConfirm.asi'
& $compiler.Source -std=c++17 -O2 -Wall -Wextra -shared -static -static-libgcc -static-libstdc++ -o $output $source -luser32 -lgdi32
if ($LASTEXITCODE -ne 0) { throw "g++ failed with exit code $LASTEXITCODE" }
Get-FileHash -LiteralPath $output -Algorithm SHA256
