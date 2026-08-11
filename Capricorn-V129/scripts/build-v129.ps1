param(
    [string]$QtRoot = "",
    [string]$ToolchainBin = "",
    [string]$InnoSetupCompiler = "",
    [string]$Configuration = "Release",
    [switch]$NoAutoInstallInno
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$Root = [IO.Path]::GetFullPath((Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)))
$SystemTemp = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
$CacheRoot = Join-Path $SystemTemp ("CapricornV129Build-" + $PID)
$Build = Join-Path $CacheRoot "qt"
$Stage = Join-Path $CacheRoot "stage"
$PublishedStage = Join-Path $Root "Capricorn-V129-Windows-x64"
$ArchiveTemp = Join-Path $CacheRoot "Capricorn-V129-Windows-x64.zip"
$Archive = Join-Path $Root "Capricorn-V129-Windows-x64.zip"
$InstallerScript = Join-Path $Root "installer/Capricorn.iss"
$InstallerOutput = Join-Path $CacheRoot "installer-output"
$SetupTemp = Join-Path $InstallerOutput "Capricorn-V129-Setup-x64.exe"
$Setup = Join-Path $Root "Capricorn-V129-Setup-x64.exe"

$ExpectedProjectName = "Capricorn-V129"
$ExpectedVersion = "129.0.0"
$ProjectNameFile = Join-Path $Root "PROJECT_NAME"
$VersionFile = Join-Path $Root "VERSION"
if (-not (Test-Path -LiteralPath $ProjectNameFile -PathType Leaf) -or
    -not (Test-Path -LiteralPath $VersionFile -PathType Leaf)) {
    throw "Project metadata is incomplete. Expected Capricorn-V129 / 129.0.0."
}
$ActualProjectName = (Get-Content -LiteralPath $ProjectNameFile -Raw).Trim()
$ActualVersion = (Get-Content -LiteralPath $VersionFile -Raw).Trim()
if ($ActualProjectName -ne $ExpectedProjectName -or $ActualVersion -ne $ExpectedVersion) {
    throw "Project metadata mismatch. Expected $ExpectedProjectName $ExpectedVersion; found $ActualProjectName $ActualVersion."
}
$FrontendCMakeText = Get-Content -LiteralPath (Join-Path $Root "frontend-qt/CMakeLists.txt") -Raw
$MainSourceText = Get-Content -LiteralPath (Join-Path $Root "frontend-qt/src/main.cpp") -Raw
if ($FrontendCMakeText -notmatch 'project\(Capricorn-V129 VERSION 129\.0\.0' -or
    $MainSourceText -notmatch 'setApplicationName\(QStringLiteral\("Capricorn-V129"\)\)' -or
    $MainSourceText -notmatch 'setApplicationVersion\(QStringLiteral\("129\.0\.0"\)\)') {
    throw "Project sources are not consistently versioned as Capricorn-V129 129.0.0."
}


# Keep physical Qt resource paths ASCII-only. Some Windows RCC/toolchain paths
# still pass through a local code page; using display names as filenames can
# therefore turn a valid path into mojibake before rcc opens it.
$ResourceRoot = Join-Path $Root 'frontend-qt/resources'
$NonAsciiResources = @()
Get-ChildItem -LiteralPath $ResourceRoot -Recurse -Force | ForEach-Object {
    $relative = $_.FullName.Substring($ResourceRoot.Length).TrimStart([char[]]'\/')
    if ($relative -match '[^\x00-\x7F]') { $NonAsciiResources += $relative }
}
if ($NonAsciiResources.Count -gt 0) {
    throw ("Resource paths must use ASCII characters only. Invalid paths: " + ($NonAsciiResources -join ', '))
}

# Validate every qrc path before invoking CMake/rcc so encoding or stale-path
# mistakes fail early with the exact offending entry.
$QrcPath = Join-Path $Root 'frontend-qt/resources.qrc'
$QrcText = Get-Content -LiteralPath $QrcPath -Raw
$QrcMatches = [regex]::Matches($QrcText, '<file(?:\s+alias="[^"]*")?>([^<]+)</file>')
foreach ($match in $QrcMatches) {
    $entry = $match.Groups[1].Value.Trim()
    if ($entry -match '[^\x00-\x7F]') {
        throw "resources.qrc contains a non-ASCII path: $entry"
    }
    $candidate = [IO.Path]::GetFullPath((Join-Path (Join-Path $Root 'frontend-qt') $entry))
    if (-not (Test-Path -LiteralPath $candidate -PathType Leaf)) {
        throw "resources.qrc references a missing file: $entry"
    }
}

# V118 keeps the large avatar pack out of Qt RCC. Embedding the ~180 MB SVG set
# makes rcc generate an enormous qrc_resources.cpp that can exhaust MinGW cc1plus.
if ($QrcText -match 'resources/avatars/') {
    throw "Large avatar SVGs must stay external; resources.qrc must not embed resources/avatars."
}
$QrcPayloadBytes = [int64]0
foreach ($match in $QrcMatches) {
    $entry = $match.Groups[1].Value.Trim()
    $candidate = [IO.Path]::GetFullPath((Join-Path (Join-Path $Root 'frontend-qt') $entry))
    $QrcPayloadBytes += (Get-Item -LiteralPath $candidate).Length
}
if ($QrcPayloadBytes -gt 8MB) {
    throw "Qt RCC payload is unexpectedly large ($QrcPayloadBytes bytes). Keep large binary/SVG assets external."
}

$AvatarSourceRoot = Join-Path $Root 'frontend-qt/resources/avatars'
$AvatarSvgFiles = @(Get-ChildItem -LiteralPath $AvatarSourceRoot -Recurse -File -Filter '*.svg' | Sort-Object FullName)
if ($AvatarSvgFiles.Count -ne 50) {
    throw "Avatar asset validation failed. Expected 50 external SVG frames; found $($AvatarSvgFiles.Count)."
}
foreach ($avatarFile in $AvatarSvgFiles) {
    $relativeAvatar = $avatarFile.FullName.Substring($AvatarSourceRoot.Length).TrimStart([char[]]'\/')
    if ($relativeAvatar -match '[^\x00-\x7F]') {
        throw "Avatar asset paths must be ASCII-only: $relativeAvatar"
    }
}

function Add-PathFront([string]$Directory) {
    if (-not $Directory) { return }
    if (-not (Test-Path -LiteralPath $Directory -PathType Container)) {
        throw "PATH directory does not exist: $Directory"
    }
    $resolved = (Resolve-Path -LiteralPath $Directory).Path
    if (($env:PATH -split ';') -notcontains $resolved) { $env:PATH = "$resolved;$env:PATH" }
}

function Assert-WorkspacePath([string]$Path) {
    $full = [IO.Path]::GetFullPath($Path)
    $prefix = $Root.TrimEnd('\') + '\'
    if (-not $full.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to modify a path outside the current project folder: $full"
    }
    return $full
}

function Assert-GeneratedPath([string]$Path) {
    $full = [IO.Path]::GetFullPath($Path)
    $workspacePrefix = $Root.TrimEnd('\') + '\'
    $cachePrefix = [IO.Path]::GetFullPath($CacheRoot).TrimEnd('\') + '\'
    if ($full.Equals([IO.Path]::GetFullPath($CacheRoot), [StringComparison]::OrdinalIgnoreCase) -or
        $full.StartsWith($workspacePrefix, [StringComparison]::OrdinalIgnoreCase) -or
        $full.StartsWith($cachePrefix, [StringComparison]::OrdinalIgnoreCase)) { return $full }
    throw "Refusing to modify an unexpected generated path: $full"
}

function Reset-GeneratedDirectory([string]$Path) {
    $safe = Assert-GeneratedPath $Path
    if (Test-Path -LiteralPath $safe) { Remove-Item -LiteralPath $safe -Recurse -Force }
    New-Item -ItemType Directory -Path $safe | Out-Null
}

function Find-CommandPath([string]$Name, [string[]]$Candidates = @()) {
    foreach ($candidate in $Candidates) {
        if ($candidate -and (Test-Path -LiteralPath $candidate -PathType Leaf)) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }
    $command = Get-Command $Name -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }
    return $null
}

function Find-InnoSetupCompiler {
    if ($InnoSetupCompiler) {
        if (-not (Test-Path -LiteralPath $InnoSetupCompiler -PathType Leaf)) {
            throw "Inno Setup compiler does not exist: $InnoSetupCompiler"
        }
        return (Resolve-Path -LiteralPath $InnoSetupCompiler).Path
    }

    function Get-InnoCandidates {
        $items = @()
        $bases = @($env:ProgramFiles, ${env:ProgramFiles(x86)})
        if ($env:LOCALAPPDATA) { $bases += (Join-Path $env:LOCALAPPDATA 'Programs') }
        foreach ($base in $bases) {
            if (-not $base) { continue }
            $items += (Join-Path $base 'Inno Setup 7/ISCC.exe')
        }
        return $items
    }

    $found = Find-CommandPath 'ISCC.exe' (Get-InnoCandidates)
    if ($found) { return $found }
    if ($NoAutoInstallInno) {
        throw "Inno Setup was not found. Install Inno Setup 7 x64 or pass -InnoSetupCompiler."
    }

    $Winget = Find-CommandPath 'winget.exe'
    if (-not $Winget) {
        throw "Inno Setup was not found and winget is unavailable. Install Inno Setup 7 x64, then rerun the build."
    }

    Write-Host "Inno Setup was not found. Starting the official Inno Setup 7 x64 installer through winget..."
    & $Winget install --id JRSoftware.InnoSetup.7 -e -s winget -i --accept-package-agreements --accept-source-agreements
    if ($LASTEXITCODE -ne 0) {
        throw "Automatic Inno Setup installation failed. Install Inno Setup 7 x64 manually, then rerun the build."
    }

    $found = Find-CommandPath 'ISCC.exe' (Get-InnoCandidates)
    if (-not $found) {
        throw "Inno Setup was installed but ISCC.exe could not be located. Rerun the build or pass -InnoSetupCompiler explicitly."
    }
    return $found
}

function Find-QtRoot {
    if ($QtRoot) { return $QtRoot }
    $candidates = @()
    if ($env:QT_ROOT) { $candidates += $env:QT_ROOT }
    if (Test-Path -LiteralPath "C:\Qt") {
        $versions = Get-ChildItem -LiteralPath "C:\Qt" -Directory -ErrorAction SilentlyContinue |
            Where-Object { $_.Name -match '^6\.\d+' } | Sort-Object Name -Descending
        foreach ($version in $versions) {
            foreach ($kit in @('mingw_64', 'msvc2022_64', 'msvc2019_64')) {
                $candidates += (Join-Path $version.FullName $kit)
            }
        }
    }
    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath (Join-Path $candidate 'bin/qmake.exe') -PathType Leaf) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }
    throw "Qt 6.8+ was not found. Install it with Qt Maintenance Tool or pass -QtRoot."
}

$QtRoot = Find-QtRoot
$QtRoot = (Resolve-Path -LiteralPath $QtRoot).Path
$QMake = Join-Path $QtRoot "bin/qmake.exe"
$QtVersionText = (& $QMake -query QT_VERSION).Trim()
$QtVersion = [Version]$QtVersionText
if ($QtVersion -lt [Version]"6.8.0") { throw "Qt 6.8+ is required; found $QtVersionText" }
$QtSpec = (& $QMake -query QMAKE_XSPEC).Trim()
Add-PathFront (Join-Path $QtRoot "bin")

$QtInstallRoot = Split-Path -Parent (Split-Path -Parent $QtRoot)
$QtTools = Join-Path $QtInstallRoot "Tools"
$IsMingw = $QtSpec -match 'mingw|g\+\+'
if ($IsMingw) {
    if (-not $ToolchainBin) {
        $toolchains = @(Get-ChildItem -LiteralPath $QtTools -Directory -Filter 'mingw*' -ErrorAction SilentlyContinue |
            Sort-Object Name -Descending | ForEach-Object { Join-Path $_.FullName 'bin' } |
            Where-Object { (Test-Path -LiteralPath (Join-Path $_ 'g++.exe')) -and
                           (Test-Path -LiteralPath (Join-Path $_ 'mingw32-make.exe')) })
        if ($toolchains.Count -eq 0) { throw "The matching Qt MinGW toolchain was not found under $QtTools" }
        $ToolchainBin = $toolchains[0]
    }
    Add-PathFront $ToolchainBin
}

$CMake = Find-CommandPath 'cmake.exe' @((Join-Path $QtTools 'CMake_64/bin/cmake.exe'))
if (-not $CMake) { throw "CMake 3.24+ was not found." }
Add-PathFront (Split-Path -Parent $CMake)
$CMakeVersionLine = (& $CMake --version | Select-Object -First 1)
if ($CMakeVersionLine -notmatch '(\d+\.\d+\.\d+)') { throw "Unable to detect the CMake version." }
$CMakeVersionText = $matches[1]
if ([Version]$CMakeVersionText -lt [Version]'3.24.0') { throw "CMake 3.24+ is required; found $CMakeVersionText" }
$Go = Find-CommandPath 'go.exe'
if (-not $Go) { throw "Go 1.23+ was not found in PATH." }
if (-not (Test-Path -LiteralPath $InstallerScript -PathType Leaf)) { throw "Installer script is missing: $InstallerScript" }
$ISCC = Find-InnoSetupCompiler

Write-Host "Building Capricorn-V129"
Write-Host "Qt:      $QtVersionText ($QtSpec)"
Write-Host "CMake:   $CMakeVersionText"
Write-Host "Go:      $(& $Go version)"
Write-Host "Inno:    $ISCC"

# Build the standalone Go Core.
Push-Location (Join-Path $Root 'core-go')
try {
    $env:GOOS = 'windows'; $env:GOARCH = 'amd64'; $env:CGO_ENABLED = '0'
    & $Go build -trimpath -ldflags '-s -w -H=windowsgui' -o 'CapricornCore-V129.exe' .
    if ($LASTEXITCODE -ne 0) { throw "Go Core build failed" }
} finally { Pop-Location }

Reset-GeneratedDirectory $Build
Reset-GeneratedDirectory $Stage
Reset-GeneratedDirectory $InstallerOutput
if (Test-Path -LiteralPath $Archive) { Remove-Item -LiteralPath (Assert-WorkspacePath $Archive) -Force }
if (Test-Path -LiteralPath $Setup) { Remove-Item -LiteralPath (Assert-WorkspacePath $Setup) -Force }

$Generator = if ($IsMingw) { 'MinGW Makefiles' } else { 'Visual Studio 17 2022' }
$configure = @('-S', $Root, '-B', $Build, '-G', $Generator, "-DCMAKE_BUILD_TYPE=$Configuration", '-DCMAKE_INSTALL_BINDIR=.')
if (-not $IsMingw) { $configure += @('-A', 'x64') }
$QtCMake = Join-Path $QtRoot 'bin/qt-cmake.bat'
if (Test-Path -LiteralPath $QtCMake) { & $QtCMake @configure } else { & $CMake @configure "-DCMAKE_PREFIX_PATH=$QtRoot" }
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed" }
& $CMake --build $Build --config $Configuration --parallel 1
if ($LASTEXITCODE -ne 0) { throw "CMake build failed" }
& $CMake --install $Build --config $Configuration --prefix $Stage
if ($LASTEXITCODE -ne 0) { throw "CMake install/deploy failed" }

$required = @('Capricorn-V129.exe','Qt6Core.dll','Qt6Gui.dll','Qt6Widgets.dll','Qt6Network.dll',
    'Qt6Svg.dll','Qt6Sql.dll','qt.conf','plugins/platforms/qwindows.dll',
    'plugins/sqldrivers/qsqlite.dll','core/CapricornCore-V129.exe')
foreach ($relative in $required) {
    if (-not (Test-Path -LiteralPath (Join-Path $Stage $relative) -PathType Leaf)) {
        throw "Release validation failed; missing $relative"
    }
}

$AvatarReleaseEntries = @()
foreach ($avatarFile in $AvatarSvgFiles) {
    $relativeAvatar = $avatarFile.FullName.Substring($AvatarSourceRoot.Length).TrimStart([char[]]'\/')
    $releaseRelative = ('assets/avatars/' + $relativeAvatar.Replace('\','/'))
    $releasePath = Join-Path $Stage ($releaseRelative.Replace('/','\'))
    if (-not (Test-Path -LiteralPath $releasePath -PathType Leaf)) {
        throw "Release validation failed; missing external avatar asset $releaseRelative"
    }
    $AvatarReleaseEntries += $releaseRelative
}

Compress-Archive -Path (Join-Path $Stage '*') -DestinationPath $ArchiveTemp -CompressionLevel Optimal
Add-Type -AssemblyName System.IO.Compression.FileSystem
$zip = [IO.Compression.ZipFile]::OpenRead($ArchiveTemp)
try {
    $entries = @($zip.Entries | ForEach-Object { $_.FullName.Replace('\','/') })
    foreach ($relative in $required) {
        if ($entries -notcontains $relative.Replace('\','/')) { throw "Archive validation failed; missing $relative" }
    }
    foreach ($relative in $AvatarReleaseEntries) {
        if ($entries -notcontains $relative) { throw "Archive validation failed; missing external avatar asset $relative" }
    }
} finally { $zip.Dispose() }
Copy-Item -LiteralPath $ArchiveTemp -Destination $Archive -Force

# Build the installer from the exact same validated staging directory used by the
# portable ZIP. This prevents the two release formats from drifting apart.
$oldProject = $env:CAPRICORN_INSTALL_PROJECT
$oldVersion = $env:CAPRICORN_INSTALL_VERSION
$oldSource = $env:CAPRICORN_INSTALL_SOURCE
$oldOutput = $env:CAPRICORN_INSTALL_OUTPUT
$oldIcon = $env:CAPRICORN_INSTALL_ICON
$oldExe = $env:CAPRICORN_INSTALL_EXE
try {
    $env:CAPRICORN_INSTALL_PROJECT = $ExpectedProjectName
    $env:CAPRICORN_INSTALL_VERSION = $ExpectedVersion
    $env:CAPRICORN_INSTALL_SOURCE = $Stage
    $env:CAPRICORN_INSTALL_OUTPUT = $InstallerOutput
    $env:CAPRICORN_INSTALL_ICON = Join-Path $Root 'frontend-qt/resources/icon.ico'
    $env:CAPRICORN_INSTALL_EXE = $ExpectedProjectName + '.exe'
    & $ISCC /Qp $InstallerScript
    if ($LASTEXITCODE -ne 0) { throw "Inno Setup compilation failed" }
} finally {
    $env:CAPRICORN_INSTALL_PROJECT = $oldProject
    $env:CAPRICORN_INSTALL_VERSION = $oldVersion
    $env:CAPRICORN_INSTALL_SOURCE = $oldSource
    $env:CAPRICORN_INSTALL_OUTPUT = $oldOutput
    $env:CAPRICORN_INSTALL_ICON = $oldIcon
    $env:CAPRICORN_INSTALL_EXE = $oldExe
}
if (-not (Test-Path -LiteralPath $SetupTemp -PathType Leaf)) {
    throw "Installer validation failed; expected output was not created: $SetupTemp"
}
if ((Get-Item -LiteralPath $SetupTemp).Length -lt 1MB) {
    throw "Installer validation failed; generated Setup.exe is unexpectedly small."
}
Copy-Item -LiteralPath $SetupTemp -Destination $Setup -Force

# Publishing an already-extracted directory is convenient on ordinary paths,
# but Windows' legacy path ceiling can make deeply nested Qt plugin paths
# unusable. The verified ZIP remains available in every case.
$publishedExecutable = $null
if ($Root.Length -le 150) {
    if (Test-Path -LiteralPath $PublishedStage) {
        Remove-Item -LiteralPath (Assert-WorkspacePath $PublishedStage) -Recurse -Force
    }
    Copy-Item -LiteralPath $Stage -Destination $PublishedStage -Recurse
    $publishedExecutable = Join-Path $PublishedStage 'Capricorn-V129.exe'
} else {
    Write-Warning "The source path is very deep; skipping the expanded runtime directory. Extract the verified runtime ZIP to a shorter location."
}

Write-Host ""
if ($publishedExecutable) { Write-Host "V129 executable: $publishedExecutable" }
Write-Host "V129 runtime ZIP: $Archive"
Write-Host "V129 installer:   $Setup"
Write-Host "ZIP SHA-256:      $((Get-FileHash -LiteralPath $Archive -Algorithm SHA256).Hash.ToLowerInvariant())"
Write-Host "Setup SHA-256:    $((Get-FileHash -LiteralPath $Setup -Algorithm SHA256).Hash.ToLowerInvariant())"
if (Test-Path -LiteralPath $CacheRoot) {
    Remove-Item -LiteralPath (Assert-GeneratedPath $CacheRoot) -Recurse -Force
}
