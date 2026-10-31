# Builds New Super Mario Bros. Wii for this PC from your own disc, in one go: checks the disc, fetches
# the pinned tools, translates the game's code and compiles it, and puts the game in a folder ready to
# play. BUILDING.md sections 1-4, for players: the release's Windows build kit runs this from
# Build-NSMBW.cmd. Nothing of the game is downloaded; its code comes from your disc.
#
#   build-game.ps1 [-Game <extracted disc folder>] [-Output <folder>] [-Jobs N] [-Compat]
#
#   -Game     New Super Mario Bros. Wii (USA, SMNE01, revision 1) extracted with Dolphin's Extract
#             Entire Disc. Asked for when not given.
#   -Output   where the game goes (default: NSMBW beside this source folder)
#   -Jobs     parallel jobs (default: every CPU thread; translated shards are capped by memory)
#   -Compat   build for older x86-64 CPUs (no AVX2) even on a newer one; picked by itself on an older one
[CmdletBinding()]
param(
    [string]$Game,
    [string]$Output,
    [int]$Jobs = 0,
    [switch]$Compat
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'  # Windows PowerShell's download progress bar slows downloads to a crawl
[Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12

. (Join-Path $PSScriptRoot 'paths.ps1')
$repo = $NsmbwRepo
$parent = Split-Path -Parent $repo
$gameWork = Join-Path $parent 'game'  # recomp.yml reads main.dol and the linked REL from ..\game
$downloads = Join-Path $repo 'Launcher\artifacts\downloads'
if (-not $Output) { $Output = Join-Path $parent 'NSMBW' }
$Output = [IO.Path]::GetFullPath($Output)
if ($Jobs -le 0) { $Jobs = [Environment]::ProcessorCount }
# The toolchain folder paths.ps1 (Get-NsmbwToolchain) will use once it exists.
$toolchain = if ($env:NSMBW_TOOLCHAIN) { $env:NSMBW_TOOLCHAIN }
    elseif (Test-Path -LiteralPath (Join-Path $repo '..\toolchain\CMake\bin\cmake.exe')) { Join-Path $repo '..\toolchain' }
    else { Join-Path $repo 'Launcher\artifacts\portable-tools' }
$toolchain = [IO.Path]::GetFullPath($toolchain)

# A portable Python for the disc scripts when this PC has none (standard library only).
$pythonZip = [pscustomobject]@{
    File = 'python-3.12.10-embed-amd64.zip'
    Uri = 'https://www.python.org/ftp/python/3.12.10/python-3.12.10-embed-amd64.zip'
    Sha256 = '4acbed6dd1c744b0376e3b1cf57ce906f9dc9e95e68824584c8099a63025a3c3'
}

function Step([string]$Text) {
    Write-Host ''
    Write-Host "== $Text" -ForegroundColor Cyan
}

function Fail([string]$Text) {
    Write-Host ''
    Write-Host "The build stopped: $Text" -ForegroundColor Red
    exit 1
}

# Runs a program, its output on the screen; its exit code. Windows PowerShell turns a program's
# error output into errors that 'Stop' would end the script on, so they are only shown here.
function Invoke-Native([string]$Exe, [string[]]$Arguments) {
    $ErrorActionPreference = 'Continue'
    & $Exe @Arguments 2>&1 | ForEach-Object { Write-Host "$_" }
    $LASTEXITCODE
}

function Get-Download($Package) {
    New-Item -ItemType Directory -Force $downloads | Out-Null
    $file = Join-Path $downloads $Package.File
    if ((Test-Path -LiteralPath $file) -and (Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash -eq $Package.Sha256) {
        return $file
    }
    Write-Host "Downloading $($Package.File)..."
    Invoke-WebRequest -UseBasicParsing -Uri $Package.Uri -OutFile "$file.partial"
    if ((Get-FileHash -LiteralPath "$file.partial" -Algorithm SHA256).Hash -ne $Package.Sha256) {
        Remove-Item -LiteralPath "$file.partial" -Force
        Fail "$($Package.File) did not download intact; try again."
    }
    Move-Item -LiteralPath "$file.partial" -Destination $file -Force
    $file
}

# Python 3.8 or newer: the Python launcher, python or python3 on PATH (not the Microsoft Store's
# placeholder), else the portable one in the toolchain folder.
function Get-Python {
    $ErrorActionPreference = 'Continue'
    foreach ($candidate in @(@('py', '-3'), @('python'), @('python3'))) {
        if (-not (Get-Command $candidate[0] -CommandType Application -ErrorAction SilentlyContinue)) { continue }
        $prefix = @($candidate | Select-Object -Skip 1)
        $version = & $candidate[0] @prefix -c 'import sys; print(sys.version_info[0] * 100 + sys.version_info[1])' 2>$null
        if ($LASTEXITCODE -eq 0 -and "$version" -match '^\d+$' -and [int]"$version" -ge 308) { return ,$candidate }
    }
    $ErrorActionPreference = 'Stop'
    $portable = Join-Path $toolchain 'python'
    if (-not (Test-Path -LiteralPath (Join-Path $portable 'python.exe'))) {
        Write-Host 'No Python 3 on this PC: using a portable one for the disc scripts.'
        $zip = Get-Download $pythonZip
        if (Test-Path -LiteralPath $portable) { Remove-Item -LiteralPath $portable -Recurse -Force }
        Expand-Archive -LiteralPath $zip -DestinationPath $portable
    }
    ,@((Join-Path $portable 'python.exe'))
}

function Invoke-Python([string[]]$Arguments) {
    $prefix = @($python | Select-Object -Skip 1)
    Invoke-Native $python[0] ($prefix + $Arguments)
}

Write-Host 'New Super Mario Bros. Wii: building the game for this PC from your own disc.' -ForegroundColor Yellow
Write-Host "Source: $repo"
if ($repo.Length -gt 90) {
    Write-Host 'The folder path is long; if the build fails on a path, move the build kit to a shorter one (like C:\NSMBW-kit).' -ForegroundColor Yellow
}

# ------------------------------------------------------------------ 1. the disc
Step '1/6  Your game'
if (-not $Game) {
    Write-Host 'Pick the folder Dolphin made with Extract Entire Disc from your New Super Mario Bros. Wii'
    Write-Host '(USA, SMNE01, revision 1). It holds the "files" and "sys" folders.'
    try {
        Add-Type -AssemblyName System.Windows.Forms
        $dialog = New-Object System.Windows.Forms.FolderBrowserDialog
        $dialog.Description = 'Your extracted New Super Mario Bros. Wii (USA, revision 1): the folder with "files" and "sys"'
        $dialog.ShowNewFolderButton = $false
        if ($dialog.ShowDialog() -eq [System.Windows.Forms.DialogResult]::OK) { $Game = $dialog.SelectedPath }
    } catch {
        $Game = $null
    }
    if (-not $Game) { $Game = Read-Host 'Extracted game folder' }
}
$Game = "$Game".Trim().Trim('"')
if (-not $Game -or -not (Test-Path -LiteralPath $Game -PathType Container)) { Fail "no such folder: '$Game'" }
$Game = (Resolve-Path -LiteralPath $Game).Path
Write-Host "Game: $Game"

$python = Get-Python
if ((Invoke-Python @((Join-Path $PSScriptRoot 'unpack_game.py'), $Game, $gameWork)) -ne 0) {
    Fail 'that game folder can''t be used (see above). This needs New Super Mario Bros. Wii for the USA (SMNE01), revision 1, extracted with Dolphin''s Extract Entire Disc; Dolphin shows the revision in the game''s Properties, Info tab.'
}
if ((Invoke-Python @((Join-Path $PSScriptRoot 'prelink_rels.py'), $gameWork)) -ne 0) { Fail 'linking the game''s modules failed (see above).' }

# ------------------------------------------------------------------ 2. tools
Step '2/6  Compiler tools (llvm-mingw, CMake, Ninja; downloaded once)'
if (-not (Test-Path -LiteralPath (Join-Path $toolchain 'CMake\bin\cmake.exe'))) {
    & (Join-Path $repo 'Launcher\Prepare-PortableTools.ps1') -Destination $toolchain
}
if ((Get-NsmbwToolchain) -ne $toolchain) { Fail "the toolchain ended up somewhere unexpected ($(Get-NsmbwToolchain))" }
Write-Host "Toolchain: $toolchain"

# The translator runs on .NET 8: the SDK on PATH if it is 8, else a private one in the toolchain folder
# (rebuild-all.ps1 uses that one first).
$privateDotnet = Join-Path $toolchain 'dotnet\dotnet.exe'
$hasDotnet8 = $false
if (-not (Test-Path -LiteralPath $privateDotnet) -and (Get-Command dotnet -CommandType Application -ErrorAction SilentlyContinue)) {
    $ErrorActionPreference = 'Continue'
    $sdks = & dotnet --list-sdks 2>$null
    $ErrorActionPreference = 'Stop'
    $hasDotnet8 = [bool]($sdks | Where-Object { "$_" -match '^8\.' })
}
if (-not (Test-Path -LiteralPath $privateDotnet) -and -not $hasDotnet8) {
    Write-Host 'Installing the .NET 8 SDK for the translator into the toolchain folder (only used by this build)...'
    New-Item -ItemType Directory -Force $downloads | Out-Null
    $installer = Join-Path $downloads 'dotnet-install.ps1'
    Invoke-WebRequest -UseBasicParsing -Uri 'https://dot.net/v1/dotnet-install.ps1' -OutFile $installer
    & $installer -Channel 8.0 -InstallDir (Split-Path -Parent $privateDotnet) -NoPath
    if (-not (Test-Path -LiteralPath $privateDotnet)) { Fail 'the .NET 8 SDK could not be installed.' }
}

# This CPU's x86-64 level, from what the toolchain's compiler finds in it.
$x86Level = 'v3'
if ($Compat) {
    $x86Level = 'v2'
} else {
    $empty = Join-Path $env:TEMP 'nsmbw-cpu-probe.c'
    Set-Content -LiteralPath $empty -Value '' -Encoding ascii
    $ErrorActionPreference = 'Continue'
    $macros = & (Join-Path $toolchain 'llvm-mingw\bin\x86_64-w64-mingw32-clang.exe') -march=native -dM -E $empty 2>$null
    $ErrorActionPreference = 'Stop'
    Remove-Item -LiteralPath $empty -Force -ErrorAction SilentlyContinue
    # x86-64-v3's defining features; every CPU with them (Haswell, Excavator, Zen and newer) has the rest.
    foreach ($feature in '__AVX2__', '__BMI2__', '__FMA__', '__F16C__') {
        if (-not ($macros | Where-Object { "$_" -match "^#define $feature " })) { $x86Level = 'v2' }
    }
}
Write-Host $(if ($x86Level -eq 'v3') { 'CPU: x86-64-v3 (AVX2)' } else { 'CPU: building the compat version for processors without AVX2' })

# ------------------------------------------------------------------ 3. library sources
Step '3/6  Library sources (SDL, Dawn and the rest; downloaded once)'
& (Join-Path $repo 'Launcher\Prepare-Dependencies.ps1') -Destination (Join-Path $repo 'Dependencies')

# ------------------------------------------------------------------ 4. translate and compile
Step "4/6  Translating and compiling the game ($Jobs jobs; the first build takes a long time)"
$memoryGiB = [math]::Floor((Get-CimInstance Win32_ComputerSystem).TotalPhysicalMemory / 1GB)
$shardJobs = [math]::Max(1, [math]::Min($Jobs, [math]::Floor($memoryGiB / 2.5)))  # ~2 GiB per translated shard
$build = Get-NsmbwBuildDirectory 'native-build'
$cache = Join-Path $build 'CMakeCache.txt'
if ((Test-Path -LiteralPath $cache) -and -not (Select-String -LiteralPath $cache -SimpleMatch "MKW_X86_LEVEL:STRING=$x86Level" -Quiet)) {
    # Configured for the other CPU level: let rebuild-all.ps1 configure it again.
    Remove-Item -LiteralPath (Join-Path $build 'build.ninja') -Force -ErrorAction SilentlyContinue
}
& (Join-Path $PSScriptRoot 'rebuild-all.ps1') -Threads $Jobs -Jobs $Jobs -TranslatedJobs $shardJobs -X86Level $x86Level
if ($LASTEXITCODE -ne 0) { Fail "compiling failed; the logs are in $(Join-Path $build 'logs')" }
$exe = Join-Path $build 'WiiCompiled.exe'
if (-not (Test-Path -LiteralPath $exe)) { Fail "the build made no $exe" }

# ------------------------------------------------------------------ 5. the game folder
Step "5/6  Putting the game in $Output"
New-Item -ItemType Directory -Force $Output | Out-Null
Copy-Item -LiteralPath $exe -Destination (Join-Path $Output 'NSMBW.exe') -Force
Get-ChildItem -LiteralPath $build -File | Where-Object { $_.Extension -in '.dll', '.db', '.pem' -or $_.Name -eq 'dsp_coef.bin' } |
    Copy-Item -Destination $Output -Force
Copy-Item -LiteralPath (Join-Path $build 'wii_bootstrap') -Destination $Output -Recurse -Force
# The game looks for its files in a "game" folder beside it: point that at your extracted disc.
$link = Join-Path $Output 'game'
if (-not (Test-Path -LiteralPath $link)) {
    try {
        New-Item -ItemType Junction -Path $link -Target $Game | Out-Null
    } catch {
        Write-Host "(Couldn't link $link to your game folder; the game asks for it at its first start.)"
    }
}

# ------------------------------------------------------------------ 6. done
Step '6/6  Done'
Write-Host "The game is $(Join-Path $Output 'NSMBW.exe')." -ForegroundColor Green
Write-Host 'Keep your extracted game folder: the game reads its levels, music and graphics from it.'
Write-Host 'Run this again after updating the build kit; it reuses the tools and only rebuilds what changed.'
exit 0
