# Build winmm.dll (32-bit) from src\ with Zig's bundled clang + MinGW headers.
# Downloads the pinned Zig release into .\tools on first run. Output: .\winmm.dll
$ErrorActionPreference = 'Stop'
$root   = $PSScriptRoot
$zigVer = '0.16.0'
$zigDir = Join-Path $root "tools\zig-x86_64-windows-$zigVer"
$zigZip = "https://ziglang.org/download/$zigVer/zig-x86_64-windows-$zigVer.zip"

if (-not (Test-Path "$zigDir\zig.exe")) {
    New-Item -ItemType Directory -Force (Join-Path $root 'tools') | Out-Null
    $tmp = Join-Path $root "tools\zig-$zigVer.zip"
    Write-Host "Downloading $zigZip"
    Invoke-WebRequest $zigZip -OutFile $tmp
    Expand-Archive $tmp -DestinationPath (Join-Path $root 'tools') -Force
    Remove-Item $tmp
}

# -nostdlib: the DLL is CRT-free (avoid struct initialisers / large stack arrays that pull in memset or __chkstk).
& "$zigDir\zig.exe" cc -target x86-windows-gnu -shared -nostdlib -fno-sanitize=undefined -fno-stack-protector `
    -isystem "$zigDir\lib\libc\include\any-windows-any" -s -o (Join-Path $root 'winmm.dll') `
    "$root\src\scaler.c" "$root\src\stubs.S" "$root\src\winmm.def" -luser32 -lgdi32 -lkernel32
if ($LASTEXITCODE -ne 0) { throw "zig cc failed ($LASTEXITCODE)" }
Write-Host "Built $(Join-Path $root 'winmm.dll')"
