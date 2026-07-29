[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^v[0-9]+\.[0-9]+\.[0-9]+$')]
    [string]$Tag,
    [switch]$Publish,
    [switch]$Prerelease
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = [System.IO.Path]::GetFullPath(
    (Join-Path $PSScriptRoot ".."))
$version = $Tag.Substring(1)
$packageName = "QTCAE-$version-windows-x64"
$zipPath = Join-Path $repoRoot "dist\$packageName.zip"
$hashPath = "$zipPath.sha256"

foreach ($path in @($zipPath, $hashPath)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Release file is missing: $path"
    }
}

$git = Get-Command git.exe -ErrorAction Stop
$gh = Get-Command gh.exe -ErrorAction Stop

Push-Location $repoRoot
try {
    $status = & $git.Source status --porcelain
    if ($LASTEXITCODE -ne 0) {
        throw "Unable to read Git status."
    }
    if ($status) {
        throw "The Git working tree is not clean."
    }

    $branch = (& $git.Source branch --show-current).Trim()
    if ($branch -ne "main") {
        throw "A release must be created from main. Current branch: $branch"
    }

    $releaseArguments = @(
        "release", "create", $Tag,
        $zipPath,
        $hashPath,
        "--title", "QTCAE $version",
        "--generate-notes",
        "--verify-tag"
    )
    if ($Prerelease) {
        $releaseArguments += "--prerelease"
    }

    if (-not $Publish) {
        Write-Host "Release checks passed. GitHub was not modified."
        Write-Host "To publish, run:"
        Write-Host ".\scripts\publish-release.ps1 -Tag $Tag -Publish"
        return
    }

    & $gh.Source @releaseArguments
    if ($LASTEXITCODE -ne 0) {
        throw "GitHub Release creation failed."
    }
}
finally {
    Pop-Location
}
