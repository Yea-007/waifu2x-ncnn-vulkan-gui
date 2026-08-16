param(
    [Parameter(Mandatory = $true)]
    [string]$UpstreamPath
)

$ErrorActionPreference = "Stop"

$UpstreamPath = (Resolve-Path -LiteralPath $UpstreamPath).Path
$GuiRoot = Split-Path -Parent $MyInvocation.MyCommand.Path

$srcDir = Join-Path $GuiRoot "src"
$patchFile = Join-Path $GuiRoot "patches\waifu2x-gui.patch"
$architectureFile = Join-Path $GuiRoot "GUI_ARCHITECTURE.md"

if (-not (Test-Path -LiteralPath $patchFile)) {
    throw "Patch file not found: $patchFile"
}

Copy-Item -Recurse -Force (Join-Path $srcDir "gui") (Join-Path $UpstreamPath "src\gui")
Copy-Item -Force (Join-Path $srcDir "model_config.h") (Join-Path $UpstreamPath "src\model_config.h")
Copy-Item -Force $architectureFile (Join-Path $UpstreamPath "GUI_ARCHITECTURE.md")

Push-Location $UpstreamPath
try {
    git apply --check $patchFile
    if ($LASTEXITCODE -ne 0) {
        throw "git apply --check failed. The upstream repository may not be at the expected base commit."
    }

    git apply $patchFile
    if ($LASTEXITCODE -ne 0) {
        throw "git apply failed."
    }
}
finally {
    Pop-Location
}

Write-Host ""
Write-Host "GUI files copied and patch applied to: $UpstreamPath"
Write-Host "Next commands:"
Write-Host "  cmake -B build -DBUILD_GUI=ON"
Write-Host "  cmake --build build --config Release"

