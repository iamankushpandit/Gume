# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
#
# Part of Braino! -- https://github.com/iamankushpandit/Gume
# Free software under GPL-3.0-or-later. Reusing any part of this file, in
# any work, must keep this notice, credit iamankushpandit as the
# author, and stay under the same licence with corresponding source
# offered. See LICENSE and NOTICE.md.

<#
Writes the Braino card image to a microSD card on Windows, reads it back, and
keeps Windows off the card until it has been pulled out.

    write-card.ps1                                  # lists the cards it would write to
    write-card.ps1 -Image braino-rg40xxv.img.gz -Disk 2

Run it from PowerShell opened as administrator. It asks you to type the disk
number again before it writes anything.

Why not an ordinary image writer: with some card readers, Windows writes to
the start of a card the moment it notices a partition on it, and a card image
carries its partition table in exactly that place. A writer that lets go of
the card when it finishes hands it straight to Windows, and the card then
boots its loader and cannot find its partitions. This script locks every
volume on the card for the whole write, writes the first 4 MB -- the
partition table and the loader -- last, and keeps the locks until the card is
removed, so nothing can write to it in between.

Writing the image replaces everything on the card, the players' profiles and
scores included. -Backup <file> saves what was on the card first.
#>
param(
    [string] $Image,
    [int] $Disk = -1,
    [string] $Backup
)
$ErrorActionPreference = 'Stop'

# Cards and the readers they sit in: a disk on an SD or MMC bus, or one
# Windows calls removable media. Bus type alone is not enough -- a card
# reader built into a laptop can report itself as SCSI. A fixed disk, the
# disk Windows runs from, or anything bigger than this is not offered: it is
# far more likely to be somebody's data.
$LargestCard = 512GB

function Get-Cards {
    $removable = @(Get-CimInstance Win32_DiskDrive | Where-Object { $_.MediaType -like 'Removable*' } |
        ForEach-Object { [int]$_.Index })
    @(Get-Disk | Where-Object {
        (@('SD', 'MMC') -contains [string]$_.BusType -or $removable -contains [int]$_.Number) -and
        -not $_.IsBoot -and -not $_.IsSystem -and $_.Size -gt 0 -and $_.Size -le $LargestCard
    })
}

function Show-Cards {
    $cards = Get-Cards
    if ($cards.Count -eq 0) {
        Write-Host 'No card found. Put the microSD card in a reader and run this again.'
        return
    }
    Write-Host 'Cards this script will write to:'
    foreach ($c in $cards) {
        Write-Host ('  -Disk {0}   {1}   {2:N1} GB   {3}' -f $c.Number, $c.FriendlyName, ($c.Size / 1GB), $c.BusType)
    }
}

if (-not $Image -or $Disk -lt 0) {
    Show-Cards
    Write-Host ''
    Write-Host 'Then: write-card.ps1 -Image <braino-rg40xxv.img.gz> -Disk <number>'
    exit 0
}
$admin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
    [Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $admin) {
    Write-Host 'Run this from PowerShell opened as administrator: Windows lets only an administrator write a whole disk.'
    exit 1
}

$d = Get-Disk -Number $Disk
if (-not ((Get-Cards) | Where-Object { $_.Number -eq $Disk })) {
    Write-Host ("Disk {0} ({1}, {2:N1} GB, {3}) is not a card this script writes to." -f $Disk, $d.FriendlyName, ($d.Size / 1GB), $d.BusType)
    Show-Cards
    exit 1
}

# A .gz is unpacked next to the image first: the first 4 MB are written last,
# so the image has to be read twice and out of order.
$imagePath = (Resolve-Path $Image).Path
$unpacked = $null
if ($imagePath -like '*.gz') {
    $unpacked = $imagePath.Substring(0, $imagePath.Length - 3)
    if (-not $unpacked.EndsWith('.img')) { $unpacked += '.img' }
    Write-Host "Unpacking $imagePath ..."
    $in = [IO.File]::OpenRead($imagePath)
    $gz = New-Object IO.Compression.GZipStream($in, [IO.Compression.CompressionMode]::Decompress)
    $out = [IO.File]::Create($unpacked)
    $gz.CopyTo($out)
    $out.Close(); $gz.Close(); $in.Close()
    $imagePath = $unpacked
}
$imgLen = (Get-Item $imagePath).Length
if ($imgLen -gt $d.Size) { throw "The image is larger than the card." }
if ($imgLen % 512 -ne 0) { throw "The image is not a whole number of sectors; is it the right file?" }

Write-Host ''
Write-Host ("About to ERASE disk {0}: {1}, {2:N1} GB." -f $Disk, $d.FriendlyName, ($d.Size / 1GB))
Write-Host 'Everything on it will be replaced by Braino.'
$typed = Read-Host 'Type the disk number again to go ahead'
if ($typed -ne [string]$Disk) { Write-Host 'Nothing written.'; exit 1 }
if ($d.IsReadOnly) { Set-Disk -Number $Disk -IsReadOnly $false }

Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;
public static class BrainoRawDisk {
    [DllImport("kernel32.dll", SetLastError=true, CharSet=CharSet.Unicode)]
    public static extern SafeFileHandle CreateFile(string name, uint access, uint share, IntPtr sec, uint disp, uint flags, IntPtr tmpl);
    [DllImport("kernel32.dll", SetLastError=true)]
    public static extern bool WriteFile(SafeFileHandle h, IntPtr buf, uint n, out uint done, IntPtr ov);
    [DllImport("kernel32.dll", SetLastError=true)]
    public static extern bool ReadFile(SafeFileHandle h, IntPtr buf, uint n, out uint done, IntPtr ov);
    [DllImport("kernel32.dll", SetLastError=true)]
    public static extern bool SetFilePointerEx(SafeFileHandle h, long dist, out long newPos, uint method);
    [DllImport("kernel32.dll", SetLastError=true)]
    public static extern bool FlushFileBuffers(SafeFileHandle h);
    [DllImport("kernel32.dll", SetLastError=true)]
    public static extern bool DeviceIoControl(SafeFileHandle h, uint code, IntPtr inBuf, uint inLen, IntPtr outBuf, uint outLen, out uint ret, IntPtr ov);
    [DllImport("msvcrt.dll", CallingConvention=CallingConvention.Cdecl)]
    public static extern int memcmp(byte[] a, byte[] b, UIntPtr n);
}
'@
$GENERIC_READ = [uint32]2147483648; $GENERIC_WRITE = [uint32]1073741824
$SHARE_RW = [uint32]3; $OPEN_EXISTING = [uint32]3
$NO_BUFFERING = [uint32]0x20000000; $WRITE_THROUGH = [uint32]2147483648
$FSCTL_LOCK_VOLUME = [uint32]0x00090018; $FSCTL_DISMOUNT_VOLUME = [uint32]0x00090020
$dev = "\\.\PhysicalDrive$Disk"
$chunk = 4MB

# FILE_FLAG_NO_BUFFERING needs a sector-aligned buffer.
$raw = [Runtime.InteropServices.Marshal]::AllocHGlobal($chunk + 4096)
$buf = [IntPtr](($raw.ToInt64() + 4095) -band (-bnot [int64]4095))
$src = New-Object byte[] $chunk
$back = New-Object byte[] $chunk

function Win32Err { [Runtime.InteropServices.Marshal]::GetLastWin32Error() }
function Open-Dev($access, $flags) {
    $h = [BrainoRawDisk]::CreateFile($dev, $access, $SHARE_RW, [IntPtr]::Zero, $OPEN_EXISTING, $flags, [IntPtr]::Zero)
    if ($h.IsInvalid) { throw "Cannot open the card (Windows error $(Win32Err))." }
    $h
}
function Seek-Dev($h, [int64]$off) {
    $np = [int64]0
    if (-not [BrainoRawDisk]::SetFilePointerEx($h, $off, [ref]$np, 0)) { throw "Seek to $off failed (Windows error $(Win32Err))." }
}
function Read-Src($fs, [int64]$off) {
    $fs.Position = $off; $n = 0
    while ($n -lt $chunk) { $r = $fs.Read($src, $n, $chunk - $n); if ($r -le 0) { break }; $n += $r }
    $n
}
function Write-Chunk($h, [int64]$off, [int]$n) {
    Seek-Dev $h $off
    [Runtime.InteropServices.Marshal]::Copy($src, 0, $buf, $n)
    $done = [uint32]0
    if (-not [BrainoRawDisk]::WriteFile($h, $buf, [uint32]$n, [ref]$done, [IntPtr]::Zero) -or $done -ne $n) {
        throw "Writing at $off failed (Windows error $(Win32Err))."
    }
}
function Test-Chunk($h, [int64]$off, [int]$n) {
    Seek-Dev $h $off
    $done = [uint32]0
    if (-not [BrainoRawDisk]::ReadFile($h, $buf, [uint32]$n, [ref]$done, [IntPtr]::Zero) -or $done -ne $n) {
        throw "Reading at $off failed (Windows error $(Win32Err))."
    }
    [Runtime.InteropServices.Marshal]::Copy($buf, $back, 0, $n)
    if ([BrainoRawDisk]::memcmp($src, $back, [UIntPtr][uint64]$n) -ne 0) { throw "MISMATCH: the card does not hold what was written at $off." }
}

$volumes = @()
try {
    if ($Backup) {
        Write-Host "Saving the card's first $([math]::Round($imgLen / 1MB)) MB to $Backup ..."
        $out = [IO.File]::Create($Backup)
        $h = Open-Dev $GENERIC_READ $NO_BUFFERING
        for ($off = [int64]0; $off -lt $imgLen; $off += $chunk) {
            $want = [int][math]::Min([int64]$chunk, [int64]($imgLen - $off))
            $done = [uint32]0
            if (-not [BrainoRawDisk]::ReadFile($h, $buf, [uint32]$want, [ref]$done, [IntPtr]::Zero) -or $done -ne $want) { throw "Reading the card for the backup failed (Windows error $(Win32Err))." }
            [Runtime.InteropServices.Marshal]::Copy($buf, $src, 0, $want)
            $out.Write($src, 0, $want)
        }
        $h.Close(); $out.Close()
    }

    # Lock and dismount every volume on the card, and hold the locks to the end.
    foreach ($p in @(Get-Partition -DiskNumber $Disk -ErrorAction SilentlyContinue)) {
        $path = $null
        if ($p.DriveLetter -and $p.DriveLetter -ne [char]0) { $path = "\\.\$($p.DriveLetter):" }
        else { $path = @($p.AccessPaths | Where-Object { $_ -like '\\?\Volume*' })[0]; if ($path) { $path = $path.TrimEnd('\') } }
        if (-not $path) { continue }
        $vh = [BrainoRawDisk]::CreateFile($path, ($GENERIC_READ -bor $GENERIC_WRITE), $SHARE_RW, [IntPtr]::Zero, $OPEN_EXISTING, 0, [IntPtr]::Zero)
        if ($vh.IsInvalid) { throw "Cannot open $path on the card (Windows error $(Win32Err)). Close anything using the card." }
        $ret = [uint32]0
        if (-not [BrainoRawDisk]::DeviceIoControl($vh, $FSCTL_LOCK_VOLUME, [IntPtr]::Zero, 0, [IntPtr]::Zero, 0, [ref]$ret, [IntPtr]::Zero)) { throw "Cannot lock $path (Windows error $(Win32Err)). Close any window showing the card." }
        if (-not [BrainoRawDisk]::DeviceIoControl($vh, $FSCTL_DISMOUNT_VOLUME, [IntPtr]::Zero, 0, [IntPtr]::Zero, 0, [ref]$ret, [IntPtr]::Zero)) { throw "Cannot dismount $path (Windows error $(Win32Err))." }
        $volumes += $vh
    }

    $fs = [IO.File]::OpenRead($imagePath)

    # 1. Everything but the first chunk.
    Write-Host 'Writing ...'
    $h = Open-Dev ($GENERIC_READ -bor $GENERIC_WRITE) ($NO_BUFFERING -bor $WRITE_THROUGH)
    for ($off = [int64]$chunk; $off -lt $imgLen; $off += $chunk) {
        Write-Progress -Activity 'Writing the card' -PercentComplete ([int](100 * $off / $imgLen))
        Write-Chunk $h $off (Read-Src $fs $off)
    }
    [void][BrainoRawDisk]::FlushFileBuffers($h); $h.Close()

    # 2. Read it back through a fresh handle, past any cache.
    Write-Host 'Checking ...'
    $h = Open-Dev $GENERIC_READ $NO_BUFFERING
    for ($off = [int64]$chunk; $off -lt $imgLen; $off += $chunk) {
        Write-Progress -Activity 'Checking the card' -PercentComplete ([int](100 * $off / $imgLen))
        Test-Chunk $h $off (Read-Src $fs $off)
    }
    $h.Close()
    Write-Progress -Activity 'Checking the card' -Completed

    # 3. Zero the card's last MB, where a card that once held a GPT keeps a
    #    second copy of it that some systems would believe.
    $tail = [int64]$d.Size - 1MB
    if ($tail -ge $imgLen -and $tail % 4096 -eq 0) {
        [Array]::Clear($src, 0, $chunk)
        $h = Open-Dev ($GENERIC_READ -bor $GENERIC_WRITE) ($NO_BUFFERING -bor $WRITE_THROUGH)
        Write-Chunk $h $tail ([int]1MB)
        [void][BrainoRawDisk]::FlushFileBuffers($h); $h.Close()
    }

    # 4. The first chunk -- partition table and loader -- last.
    $n = Read-Src $fs 0
    $h = Open-Dev ($GENERIC_READ -bor $GENERIC_WRITE) ($NO_BUFFERING -bor $WRITE_THROUGH)
    Write-Chunk $h 0 $n
    [void][BrainoRawDisk]::FlushFileBuffers($h); $h.Close()
    $h = Open-Dev $GENERIC_READ $NO_BUFFERING
    Test-Chunk $h 0 $n
    $h.Close()
    $first = New-Object byte[] $chunk; [Array]::Copy($src, $first, $chunk)
    $fs.Close()

    # 5. Keep the locks, and keep checking the first chunk, until the card is
    #    taken out. Letting go is what gives Windows the chance to write to it.
    Write-Host ''
    Write-Host 'Written and checked. TAKE THE CARD OUT NOW -- do not eject it first.' -ForegroundColor Green
    $changed = $false
    $gone = $false
    while (-not ($changed -or $gone)) {
        $h = [BrainoRawDisk]::CreateFile($dev, $GENERIC_READ, $SHARE_RW, [IntPtr]::Zero, $OPEN_EXISTING, $NO_BUFFERING, [IntPtr]::Zero)
        if ($h.IsInvalid) { break }
        [Array]::Copy($first, $src, $chunk)
        # A reader with no card in it still opens, and then fails to read.
        try { Test-Chunk $h 0 $n }
        catch { if ($_.Exception.Message -like 'MISMATCH*') { $changed = $true } else { $gone = $true } }
        $h.Close()
        if (-not ($changed -or $gone)) { Start-Sleep -Seconds 1 }
    }
    if ($changed) {
        Write-Host 'The start of the card changed while it was locked. Write it again.' -ForegroundColor Red
        exit 1
    }
    Write-Host 'Card removed. Put it in the console and switch it on.' -ForegroundColor Green
}
catch {
    Write-Host $_.Exception.Message -ForegroundColor Red
    Write-Host 'The card is not ready; write it again.' -ForegroundColor Red
    exit 1
}
finally {
    foreach ($vh in $volumes) { $vh.Close() }
    [Runtime.InteropServices.Marshal]::FreeHGlobal($raw)
    if ($unpacked -and (Test-Path $unpacked)) { Remove-Item $unpacked }
}
