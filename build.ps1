#Requires -Version 5.1
$ErrorActionPreference = 'Stop'

$cc = if ($env:CC) { $env:CC } else { 'gcc' }
$windres = if ($env:WINDRES) { $env:WINDRES } else { 'windres' }
$cflags = @('-std=c11', '-O2', '-Wall', '-Wextra', '-Wno-unused-parameter', '-D_WIN32_WINNT=0x0601')
$build = 'build'

New-Item -ItemType Directory -Force -Path $build | Out-Null

Write-Host 'Generating the application icon...' -ForegroundColor Cyan
& $cc -O2 -o "$build\makeicon.exe" tools\makeicon.c -lm
if ($LASTEXITCODE -ne 0) { throw 'makeicon build failed' }
& "$build\makeicon.exe" "$build\fresh.ico"
Copy-Item "$build\fresh.ico" 'src\fresh.ico' -Force
Copy-Item "$build\fresh.ico" 'installation\fresh.ico' -Force

Write-Host 'Building FreSH...' -ForegroundColor Cyan
& $windres 'src\fresh.rc' -O coff -o "$build\fresh.res"
if ($LASTEXITCODE -ne 0) { throw 'resource build failed' }
& $cc @cflags (Get-ChildItem src\*.c).FullName "$build\fresh.res" -o "$build\FreSH.exe" -ladvapi32
if ($LASTEXITCODE -ne 0) { throw 'FreSH build failed' }

Write-Host 'Building payload generator...' -ForegroundColor Cyan
& $cc -O2 -o "$build\bin2c.exe" tools\bin2c.c
if ($LASTEXITCODE -ne 0) { throw 'bin2c build failed' }

Write-Host 'Embedding FreSH.exe into the installer...' -ForegroundColor Cyan
& "$build\bin2c.exe" "$build\FreSH.exe" 'installation\payload.h' 'FRESH_PAYLOAD'
if ($LASTEXITCODE -ne 0) { throw 'payload generation failed' }

Write-Host 'Building installer...' -ForegroundColor Cyan
& $windres 'installation\setup.rc' -O coff -o "$build\setup.res"
if ($LASTEXITCODE -ne 0) { throw 'installer resource build failed' }
& $cc @cflags (Get-ChildItem installation\*.c).FullName "$build\setup.res" -o "$build\FreSH-Setup.exe" `
    -lole32 -luuid -lshell32 -ladvapi32 -luser32
if ($LASTEXITCODE -ne 0) { throw 'installer build failed' }

Write-Host "Build complete: $build\FreSH.exe, $build\FreSH-Setup.exe" -ForegroundColor Green
