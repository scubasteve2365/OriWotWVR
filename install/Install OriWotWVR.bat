@echo off
rem Ori and the Will of the Wisps VR installer. Double click it, or use: "Install OriWotWVR.bat" install   or   "Install OriWotWVR.bat" uninstall
rem It only touches the game folder. No game files are changed, and uninstall puts the game folder back as it was.
setlocal
set "OWVR_HERE=%~dp0"
set "OWVR_ARG=%~1"
set "OWVR_SELF=%~f0"
powershell -NoProfile -ExecutionPolicy Bypass -Command "$t = Get-Content -LiteralPath $env:OWVR_SELF -Raw; $s = $t.Substring($t.IndexOf('#PS' + 'START') + 8); Invoke-Expression $s"
set "OWVR_RESULT=%ERRORLEVEL%"
if "%OWVR_ARG%"=="" pause
exit /b %OWVR_RESULT%

#PSSTART
$ErrorActionPreference = 'Stop'
$here = $env:OWVR_HERE
$files = Join-Path $here 'files'
$arg = $env:OWVR_ARG
$exe = 'oriwotw.exe'

function Find-Game {
    $roots = New-Object System.Collections.ArrayList
    try {
        $steam = (Get-ItemProperty 'HKCU:\Software\Valve\Steam').SteamPath
        if ($steam) {
            $steam = $steam -replace '/', '\'
            [void]$roots.Add($steam)
            $vdf = Join-Path $steam 'steamapps\libraryfolders.vdf'
            if (Test-Path -LiteralPath $vdf) {
                foreach ($m in [regex]::Matches((Get-Content -LiteralPath $vdf -Raw), '"path"\s+"([^"]+)"')) {
                    [void]$roots.Add(($m.Groups[1].Value -replace '\\\\', '\'))
                }
            }
        }
    } catch { }
    foreach ($r in $roots) {
        $g = Join-Path $r 'steamapps\common\Ori and the Will of the Wisps'
        if (Test-Path -LiteralPath (Join-Path $g $exe)) { return $g }
    }
    return $null
}

function Fail($text) {
    Write-Host ''
    Write-Host "Ori VR: $text" -ForegroundColor Red
    exit 1
}

# Our winhttp.dll carries the text OriWotWVRNative (it loads that file). Any other winhttp.dll belongs to another mod.
function Is-Ours($path) {
    if (-not (Test-Path -LiteralPath $path)) { return $false }
    $bytes = [System.IO.File]::ReadAllBytes($path)
    $text = [System.Text.Encoding]::Unicode.GetString($bytes)
    return $text.Contains('OriWotWVRNative')
}

function Same-File($a, $b) {
    return (Get-FileHash -LiteralPath $a -Algorithm SHA256).Hash -eq (Get-FileHash -LiteralPath $b -Algorithm SHA256).Hash
}

function Install-Mod($game) {
    $proxy = Join-Path $game 'winhttp.dll'
    $chain = Join-Path $game 'winhttp_chain.dll'
    if ((Test-Path -LiteralPath $proxy) -and -not (Is-Ours $proxy)) {
        # Another mod's winhttp.dll (for example the randomizer's): keep it, and ours loads it first.
        if (Test-Path -LiteralPath $chain) { Fail 'winhttp.dll from another mod is in the game folder, and winhttp_chain.dll is too. Move one of them away, then run this again.' }
        Rename-Item -LiteralPath $proxy -NewName 'winhttp_chain.dll'
        Write-Host 'Another mod''s winhttp.dll was found. It was renamed to winhttp_chain.dll and still loads with this mod.' -ForegroundColor Yellow
    }
    Copy-Item -LiteralPath (Join-Path $files 'winhttp.dll') -Destination $game -Force
    Copy-Item -LiteralPath (Join-Path $files 'OriWotWVR') -Destination $game -Recurse -Force

    foreach ($f in Get-ChildItem -LiteralPath $files -Recurse -File) {
        $rel = $f.FullName.Substring($files.Length).TrimStart('\')
        $dst = Join-Path $game $rel
        if (-not (Test-Path -LiteralPath $dst)) { Fail "$rel did not copy." }
        if (-not (Same-File $f.FullName $dst)) { Fail "$rel did not copy correctly." }
        # Files from a downloaded zip carry a "came from the internet" mark, which can stop the DLLs loading.
        Unblock-File -LiteralPath $dst
    }
    Write-Host ''
    Write-Host 'Ori and the Will of the Wisps VR is installed.' -ForegroundColor Green
    Write-Host 'Start SteamVR or connect Virtual Desktop, then start the game from Steam. VR starts by itself.'
    Write-Host 'Hold both sticks in, or press Insert, to open the VR settings menu.'
    $runtime = $null
    try { $runtime = (Get-ItemProperty 'HKLM:\SOFTWARE\Khronos\OpenXR\1' -ErrorAction Stop).ActiveRuntime } catch { }
    if (-not $runtime) {
        Write-Host ''
        Write-Host 'Warning: no OpenXR runtime is set on this PC.' -ForegroundColor Yellow
        Write-Host 'In SteamVR settings, under OpenXR, set SteamVR as the OpenXR runtime, or use Virtual Desktop. If SteamVR is installed the mod uses it anyway.'
    } else {
        Write-Host "OpenXR runtime: $runtime"
    }
    $vc = $false
    try { $vc = (Get-ItemProperty 'HKLM:\SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\X64' -ErrorAction Stop).Installed -eq 1 } catch { }
    if (-not $vc) {
        Write-Host ''
        Write-Host 'Note: the Microsoft Visual C++ 2015-2022 runtime (x64) was not found. The mod does not need it, but the game does; Steam normally installs it.' -ForegroundColor Yellow
    }
    $sac = $null
    try { $sac = (Get-MpComputerStatus -ErrorAction Stop).SmartAppControlState } catch { }
    if ($sac -and $sac -ne 'Off') {
        Write-Host ''
        Write-Host "Warning: Windows Smart App Control is $sac on this PC." -ForegroundColor Yellow
        Write-Host 'It can block mod DLLs that are not signed, and this mod is not signed. If the game runs flat, check this first. See the README.'
    }
}

function Uninstall-Mod($game) {
    $proxy = Join-Path $game 'winhttp.dll'
    if (Is-Ours $proxy) { Remove-Item -LiteralPath $proxy -Force }
    $chain = Join-Path $game 'winhttp_chain.dll'
    if ((Test-Path -LiteralPath $chain) -and -not (Test-Path -LiteralPath $proxy)) {
        Rename-Item -LiteralPath $chain -NewName 'winhttp.dll'
        Write-Host 'The other mod''s winhttp.dll was put back.'
    }
    $mod = Join-Path $game 'OriWotWVR'
    if (Test-Path -LiteralPath $mod) { Remove-Item -LiteralPath $mod -Recurse -Force }
    foreach ($n in @('OriWotWVR.log', 'OriWotWVR_proxy.log', 'vrlab_command.txt')) {
        $p = Join-Path $game $n
        if (Test-Path -LiteralPath $p) { Remove-Item -LiteralPath $p -Force }
    }
    Write-Host ''
    Write-Host 'Ori and the Will of the Wisps VR is uninstalled. The game folder is back to how it was.' -ForegroundColor Green
    Write-Host 'The game''s own settings are kept. The mod had turned off HDR, dynamic resolution and motion blur; turn them back on in the game''s options if you want them.'
}

if (-not (Test-Path -LiteralPath (Join-Path $files 'winhttp.dll'))) { Fail 'the files folder is missing. Unzip everything before running this.' }
if (Get-Process -Name oriwotw -ErrorAction SilentlyContinue) { Fail 'close the game first, then run this again.' }

$game = Find-Game
if (-not $game) {
    Write-Host 'Could not find Ori and the Will of the Wisps automatically.'
    if ($arg) { Fail 'game folder not found.' }
    $game = Read-Host 'Paste the game folder (the one holding oriwotw.exe)'
    $game = $game.Trim('"', ' ')
    if (-not (Test-Path -LiteralPath (Join-Path $game $exe))) { Fail "$exe is not in that folder." }
}
Write-Host "Game folder: $game"

$choice = $arg
if (-not $choice) {
    Write-Host ''
    Write-Host '  1  Install or update Ori and the Will of the Wisps VR'
    Write-Host '  2  Uninstall'
    $pick = Read-Host 'Type 1 or 2 and press Enter'
    if ($pick -eq '1') { $choice = 'install' }
    elseif ($pick -eq '2') { $choice = 'uninstall' }
    else { Fail 'nothing done.' }
}
if ($choice -eq 'install') { Install-Mod $game }
elseif ($choice -eq 'uninstall') { Uninstall-Mod $game }
else { Fail "unknown choice $choice" }
exit 0
