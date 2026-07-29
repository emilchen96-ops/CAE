[CmdletBinding()]
param(
    [string]$Preset = "windows-msvc-release",
    [string]$Configuration = "Release",
    [string]$Version = "",
    [string]$Qt6Dir = $env:QTCAE_QT6_DIR,
    [string]$OpenCascadeDir = $env:QTCAE_OCCT_DIR,
    [string]$VtkDir = $env:QTCAE_VTK_DIR,
    [string]$GmshDir = $env:QTCAE_GMSH_DIR,
    [switch]$SkipConfigure,
    [switch]$SkipBuild,
    [switch]$SkipTests
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = [System.IO.Path]::GetFullPath(
    (Join-Path $PSScriptRoot ".."))
$distRoot = [System.IO.Path]::GetFullPath(
    (Join-Path $repoRoot "dist"))

function Invoke-Checked {
    param(
        [Parameter(Mandatory = $true)]
        [string]$FilePath,
        [Parameter(ValueFromRemainingArguments = $true)]
        [string[]]$Arguments
    )

    Write-Host "> $FilePath $($Arguments -join ' ')"
    & $FilePath @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed with exit code $LASTEXITCODE."
    }
}

function Invoke-SmokeTest {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Executable
    )

    Write-Host "> $Executable --smoke-test"
    $process = Start-Process `
        -FilePath $Executable `
        -ArgumentList "--smoke-test" `
        -WindowStyle Hidden `
        -PassThru `
        -Wait
    if ($process.ExitCode -ne 0) {
        throw "Workbench smoke test failed with exit code $($process.ExitCode)."
    }
}

function Resolve-Tool {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Name,
        [string[]]$Candidates = @()
    )

    $command = Get-Command $Name -ErrorAction SilentlyContinue
    if ($null -ne $command) {
        return $command.Source
    }

    foreach ($candidate in $Candidates) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            return $candidate
        }
    }

    throw "Required tool was not found: $Name"
}

function Test-ReleaseRuntimeDependencies {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Directory,
        [Parameter(Mandatory = $true)]
        [string]$BuildDirectory
    )

    $cachePath = Join-Path $BuildDirectory "CMakeCache.txt"
    if (-not (Test-Path -LiteralPath $cachePath -PathType Leaf)) {
        throw "CMakeCache.txt is missing; unable to locate dumpbin.exe."
    }

    $cacheContent = Get-Content -LiteralPath $cachePath -Encoding utf8
    $linkerEntry = $cacheContent |
        Where-Object { $_ -match '^CMAKE_LINKER(?::FILEPATH)?=(.+)$' } |
        Select-Object -First 1
    $dumpbinCandidates = @()
    if ($null -ne $linkerEntry -and
        $linkerEntry -match '^CMAKE_LINKER(?::FILEPATH)?=(.+)$') {
        $linkerPath = $Matches[1]
        $dumpbinCandidates += Join-Path `
            (Split-Path $linkerPath -Parent) "dumpbin.exe"
    }
    $dumpbin = Resolve-Tool "dumpbin.exe" $dumpbinCandidates

    $debugRuntimePattern =
        '(?i)\b(?:msvcp\d+d|vcruntime\d+(?:_\d+)?d|concrt\d+d|ucrtbased)\.dll\b'
    $invalidDependencies = [System.Collections.Generic.List[string]]::new()
    $peFiles = Get-ChildItem -LiteralPath $Directory -Recurse -File |
        Where-Object { $_.Extension -in @(".exe", ".dll") }

    foreach ($peFile in $peFiles) {
        $dependencyOutput = & $dumpbin /nologo /dependents `
            $peFile.FullName 2>&1 | Out-String
        if ($LASTEXITCODE -ne 0) {
            throw "dumpbin.exe failed while inspecting $($peFile.FullName)."
        }

        $matches = [regex]::Matches(
            $dependencyOutput,
            $debugRuntimePattern)
        foreach ($match in $matches) {
            $invalidDependencies.Add(
                "$($peFile.FullName) -> $($match.Value)")
        }
    }

    if ($invalidDependencies.Count -gt 0) {
        throw "Debug CRT dependencies were found:`n$(
            $invalidDependencies -join [Environment]::NewLine)"
    }

    Write-Host "Release runtime dependency audit passed for $($peFiles.Count) files."
}

$cmake = Resolve-Tool "cmake.exe" @(
    "C:\Program Files\CMake\bin\cmake.exe",
    "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe",
    "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
)
$ctest = Join-Path (Split-Path $cmake -Parent) "ctest.exe"

$presetDocument = Get-Content `
    -LiteralPath (Join-Path $repoRoot "CMakePresets.json") `
    -Raw -Encoding utf8 | ConvertFrom-Json
$configurePreset = $presetDocument.configurePresets |
    Where-Object { $_.name -eq $Preset } |
    Select-Object -First 1
if ($null -eq $configurePreset) {
    throw "Configure preset was not found: $Preset"
}

$buildDirTemplate = [string]$configurePreset.binaryDir
$buildDir = $buildDirTemplate.Replace('${sourceDir}', $repoRoot)
$buildDir = [System.IO.Path]::GetFullPath($buildDir)

if ([string]::IsNullOrWhiteSpace($Version)) {
    $rootCMake = Get-Content `
        -LiteralPath (Join-Path $repoRoot "CMakeLists.txt") `
        -Raw -Encoding utf8
    $versionMatch = [regex]::Match(
        $rootCMake,
        'project\s*\(\s*EmilCAE\s+VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)')
    if (-not $versionMatch.Success) {
        throw "Unable to read the project version from CMakeLists.txt."
    }
    $Version = $versionMatch.Groups[1].Value
}

$packageName = "QTCAE-$Version-windows-x64"
$stageRoot = [System.IO.Path]::GetFullPath(
    (Join-Path $distRoot ".staging-$PID-$([guid]::NewGuid().ToString('N'))"))
$stageDir = [System.IO.Path]::GetFullPath(
    (Join-Path $stageRoot $packageName))
$zipPath = [System.IO.Path]::GetFullPath(
    (Join-Path $distRoot "$packageName.zip"))
$hashPath = "$zipPath.sha256"

if (-not $stageDir.StartsWith(
        $distRoot + [System.IO.Path]::DirectorySeparatorChar,
        [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "The staging directory is outside the repository dist directory."
}

Push-Location $repoRoot
try {
    if (-not $SkipConfigure) {
        $configureArguments = @(
            "--preset", $Preset,
            "-UGMSH_INCLUDE_DIR",
            "-UGMSH_LIBRARY",
            "-UGMSH_RUNTIME_DLL"
        )
        $dependencyPaths = @{
            "Qt6_DIR" = $Qt6Dir
            "OpenCASCADE_DIR" = $OpenCascadeDir
            "VTK_DIR" = $VtkDir
            "Gmsh_DIR" = $GmshDir
        }
        foreach ($entry in $dependencyPaths.GetEnumerator()) {
            if (-not [string]::IsNullOrWhiteSpace([string]$entry.Value)) {
                $configureArguments += "-D$($entry.Key)=$($entry.Value)"
            }
        }
        Invoke-Checked $cmake @configureArguments
    }
    if (-not $SkipBuild) {
        Invoke-Checked $cmake "--build" "--preset" $Preset
    }
    if (-not $SkipTests) {
        Invoke-Checked $ctest "--preset" $Preset "--output-on-failure"
    }

    if (Test-Path -LiteralPath $stageDir) {
        Remove-Item -LiteralPath $stageDir -Recurse -Force
    }
    New-Item -ItemType Directory -Path $stageDir | Out-Null

    Invoke-Checked $cmake `
        "--install" $buildDir `
        "--config" $Configuration `
        "--prefix" $stageDir

    $workbench = Join-Path $stageDir "QTCAE.Workbench.exe"
    if (-not (Test-Path -LiteralPath $workbench -PathType Leaf)) {
        throw "QTCAE.Workbench.exe is missing from the staging directory."
    }

    $debugDlls = @(
        Get-ChildItem -LiteralPath $stageDir -Recurse -File |
            Where-Object {
            $_.Name -match '^Qt6.*d\.dll$' -or
            $_.Name -match '^vtk.*d\.dll$' -or
            $_.Name -match '^msvcp[0-9]+d\.dll$' -or
            $_.Name -match '^vcruntime[0-9_]+d\.dll$' -or
            $_.Name -ieq 'ucrtbased.dll'
        }
    )
    if ($debugDlls.Count -gt 0) {
        $names = ($debugDlls.FullName -join [Environment]::NewLine)
        throw "Debug DLLs were found in the staging directory:`n$names"
    }

    Test-ReleaseRuntimeDependencies `
        -Directory $stageDir `
        -BuildDirectory $buildDir

    $platformPlugin = Join-Path $stageDir "plugins\platforms\qwindows.dll"
    if (-not (Test-Path -LiteralPath $platformPlugin -PathType Leaf)) {
        throw "The Qt platform plugin plugins/platforms/qwindows.dll is missing."
    }

    Invoke-SmokeTest $workbench

    if (Test-Path -LiteralPath $zipPath) {
        Remove-Item -LiteralPath $zipPath -Force
    }
    if (Test-Path -LiteralPath $hashPath) {
        Remove-Item -LiteralPath $hashPath -Force
    }

    Compress-Archive -LiteralPath $stageDir -DestinationPath $zipPath
    $hash = Get-FileHash -LiteralPath $zipPath -Algorithm SHA256
    "$($hash.Hash.ToLowerInvariant())  $([IO.Path]::GetFileName($zipPath))" |
        Set-Content -LiteralPath $hashPath -Encoding ascii

    Write-Host ""
    Write-Host "Release package created:"
    Write-Host "  $zipPath"
    Write-Host "  $hashPath"
}
finally {
    Pop-Location
    if (Test-Path -LiteralPath $stageRoot) {
        Remove-Item -LiteralPath $stageRoot -Recurse -Force `
            -ErrorAction SilentlyContinue
    }
}
