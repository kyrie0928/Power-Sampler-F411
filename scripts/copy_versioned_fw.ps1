param(
    [string]$HeaderPath = "Core/Inc/fw_version.h",
    [Parameter(Mandatory = $true)][string]$OutDir,
    [Parameter(Mandatory = $true)][string]$BaseName
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path -LiteralPath $HeaderPath)) {
    Write-Error "Version header not found: $HeaderPath"
    exit 1
}

$content = Get-Content -Raw -LiteralPath $HeaderPath
$majorMatch = [regex]::Match($content, '(?m)^\s*#define\s+FW_VERSION_MAJOR\s+(\d+)\b')
$minorMatch = [regex]::Match($content, '(?m)^\s*#define\s+FW_VERSION_MINOR\s+(\d+)\b')
$patchMatch = [regex]::Match($content, '(?m)^\s*#define\s+FW_VERSION_PATCH\s+(\d+)\b')

if (-not ($majorMatch.Success -and $minorMatch.Success -and $patchMatch.Success)) {
    Write-Error "Failed to parse FW_VERSION_MAJOR/MINOR/PATCH from $HeaderPath"
    exit 1
}

$ver = "$($majorMatch.Groups[1].Value).$($minorMatch.Groups[1].Value).$($patchMatch.Groups[1].Value)"

if (-not (Test-Path -LiteralPath $OutDir)) {
    Write-Error "Output directory not found: $OutDir"
    exit 1
}

foreach ($ext in @("hex", "bin")) {
    $src = Join-Path $OutDir "$BaseName.$ext"
    if (-not (Test-Path -LiteralPath $src)) {
        Write-Error "Expected firmware artifact not found: $src"
        exit 1
    }

    Get-ChildItem -LiteralPath $OutDir -Filter "${BaseName}_*.*.*.$ext" -ErrorAction SilentlyContinue | Remove-Item -Force
    $dst = Join-Path $OutDir "${BaseName}_${ver}.$ext"
    Copy-Item -LiteralPath $src -Destination $dst -Force
}

Write-Host "[FW-VERSION] Generated ${BaseName}_${ver}.hex & ${BaseName}_${ver}.bin in $OutDir"
