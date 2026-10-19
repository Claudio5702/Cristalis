# Builds "dist\Crystal Defenders - Setup.exe" from the built game (build\, see build.ps1), after the installer
# of Kahna (Bahamut Lagoon Recomp).
#
# The installer holds the program, the FFmpeg DLLs and the read-me files. It never holds the game's
# data, saves or settings: those come from the player's own copy when it is installed. The program
# itself contains the game's code translated to C++, so the installer stays for personal use.
param([string]$Output = (Join-Path (Split-Path (Split-Path $PSScriptRoot)) 'dist'))
$ErrorActionPreference = 'Stop'
$recomp = Split-Path $PSScriptRoot -Parent
$build = Join-Path (Split-Path $recomp) 'build'
$ffmpeg = Join-Path (Split-Path $recomp) 'tools\ffmpeg\ffmpeg-n8.1-latest-win64-lgpl-shared-8.1'
New-Item -ItemType Directory -Force $Output | Out-Null

$stub = Join-Path $Output 'setup_stub.exe'
$res = Join-Path $Output 'setup_res.o'
& windres (Join-Path $PSScriptRoot 'setup\setup.rc') -O coff -o $res --include-dir (Join-Path $recomp 'resources')
if ($LASTEXITCODE -ne 0) { throw "resource build failed" }
# -mwindows: no console window; the program attaches to a terminal itself when started from one
& clang -std=c11 -O2 -Wall (Join-Path $PSScriptRoot 'setup\setup.c') $res -o $stub -mwindows -static -lole32 -luuid -lshell32 -luser32 -lgdi32 -ladvapi32
if ($LASTEXITCODE -ne 0) { throw "setup build failed" }
[IO.File]::Delete($res)

# name inside the installation folder -> source file
$files = [ordered]@{
    'crystal_defenders.exe' = Join-Path $build 'crystal_defenders.exe'
    'avcodec-62.dll' = Join-Path $build 'avcodec-62.dll'
    'avutil-60.dll' = Join-Path $build 'avutil-60.dll'
    'swresample-6.dll' = Join-Path $build 'swresample-6.dll'
    'FFmpeg-LICENSE.txt' = Join-Path $ffmpeg 'LICENSE.txt'
    'LICENSE.txt' = Join-Path (Split-Path (Split-Path $PSScriptRoot)) 'LICENSE' # this project's (MIT)
    'LISEZMOI.txt' = Join-Path $PSScriptRoot 'setup\LISEZMOI.txt'
    'README.txt' = Join-Path $PSScriptRoot 'setup\README.txt'
}

# Files are compressed with the Windows Compression API (LZMS); setup.c undoes it with the same API.
Add-Type @'
using System; using System.Runtime.InteropServices;
public static class CdPack {
  [DllImport("cabinet.dll", SetLastError = true)] static extern bool CreateCompressor(uint algorithm, IntPtr routines, out IntPtr handle);
  [DllImport("cabinet.dll", SetLastError = true)] static extern bool Compress(IntPtr handle, byte[] data, UIntPtr size, byte[] buffer, UIntPtr bufferSize, out UIntPtr used);
  [DllImport("cabinet.dll")] static extern bool CloseCompressor(IntPtr handle);
  public static byte[] Pack(byte[] data) {
    IntPtr h;
    if (!CreateCompressor(5, IntPtr.Zero, out h)) return null;
    byte[] buffer = new byte[data.Length + 65536];
    UIntPtr used;
    bool ok = Compress(h, data, (UIntPtr)data.Length, buffer, (UIntPtr)buffer.Length, out used);
    CloseCompressor(h);
    if (!ok || (ulong)used >= (ulong)data.Length) return null;
    Array.Resize(ref buffer, (int)used);
    return buffer;
  }
}
'@

$out = Join-Path $Output 'Cristalis-Setup.exe'
Copy-Item -LiteralPath $stub -Destination $out -Force
$fs = [IO.File]::Open($out, 'Append', 'Write')
try {
    $bw = New-Object IO.BinaryWriter($fs)
    $table = New-Object IO.MemoryStream
    $tw = New-Object IO.BinaryWriter($table)
    $count = 0
    foreach ($name in $files.Keys) {
        $src = $files[$name]
        if (-not (Test-Path -LiteralPath $src)) { throw "missing: $src" }
        $data = [IO.File]::ReadAllBytes($src)
        $offset = $fs.Position
        $packed = [CdPack]::Pack($data)   # null when it would not get smaller: stored as it is
        if ($packed) { $bw.Write($packed) } else { $bw.Write($data) }
        $nb = New-Object byte[] 260
        $enc = [Text.Encoding]::ASCII.GetBytes($name)
        [Array]::Copy($enc, $nb, $enc.Length)
        $tw.Write($nb)
        $tw.Write([uint32]0)          # struct padding before the 64-bit fields
        $tw.Write([uint64]$data.Length)
        $tw.Write([uint64]$offset)
        $count++
    }
    $tableOffset = $fs.Position
    $bw.Write($table.ToArray())
    $bw.Write([uint64]$tableOffset)
    $bw.Write([uint32]$count)
    $bw.Write([Text.Encoding]::ASCII.GetBytes('CDSETUP1'))
    $bw.Flush()
} finally { $fs.Close() }
[IO.File]::Delete($stub)
'{0}  ({1:N0} octets, {2} fichiers)' -f $out, (Get-Item -LiteralPath $out).Length, $count
