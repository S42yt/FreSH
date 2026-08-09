#Requires -Version 5.1
$ErrorActionPreference = 'Stop'

$cc = if ($env:CC) { $env:CC } else { 'gcc' }
$cflags = @('-std=c11', '-O2', '-Wall', '-Wextra', '-Wno-unused-parameter', '-D_WIN32_WINNT=0x0601')
$build = 'build'

New-Item -ItemType Directory -Force -Path $build | Out-Null

Write-Host 'Building FreSH...' -ForegroundColor Cyan
& $cc @cflags (Get-ChildItem src\*.c).FullName -o "$build\FreSH.exe" -ladvapi32
if ($LASTEXITCODE -ne 0) { throw 'FreSH build failed' }

Write-Host 'Building payload generator...' -ForegroundColor Cyan
& $cc -O2 -o "$build\bin2c.exe" tools\bin2c.c
if ($LASTEXITCODE -ne 0) { throw 'bin2c build failed' }

Write-Host 'Embedding FreSH.exe into the installer...' -ForegroundColor Cyan
& "$build\bin2c.exe" "$build\FreSH.exe" 'installation\payload.h' 'FRESH_PAYLOAD'
if ($LASTEXITCODE -ne 0) { throw 'payload generation failed' }

Write-Host 'Building installer...' -ForegroundColor Cyan
& $cc @cflags (Get-ChildItem installation\*.c).FullName -o "$build\FreSH-Setup.exe" `
    -lole32 -luuid -lshell32 -ladvapi32 -luser32
if ($LASTEXITCODE -ne 0) { throw 'installer build failed' }

Write-Host "Build complete: $build\FreSH.exe, $build\FreSH-Setup.exe" -ForegroundColor Green
