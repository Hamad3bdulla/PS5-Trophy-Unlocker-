param(
    [switch]$InstallPythonWithWinget,
    [switch]$Force
)

$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
$PackageRoot = Split-Path -Parent $Root
$Requirements = Join-Path $Root "requirements.txt"
$Deps = Join-Path $Root "pydeps"

function Find-Python {
    $local = Join-Path $Root "python\python.exe"
    if (Test-Path -LiteralPath $local) {
        return @{ Exe = $local; Args = @() }
    }

    $bundled = Join-Path $env:USERPROFILE ".cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe"
    if (Test-Path -LiteralPath $bundled) {
        return @{ Exe = $bundled; Args = @() }
    }

    $python = Get-Command python.exe -ErrorAction SilentlyContinue
    if ($python) {
        return @{ Exe = $python.Source; Args = @() }
    }

    $py = Get-Command py.exe -ErrorAction SilentlyContinue
    if ($py) {
        return @{ Exe = $py.Source; Args = @("-3") }
    }

    return $null
}

if (-not (Test-Path -LiteralPath $Requirements)) {
    throw "requirements.txt not found: $Requirements"
}

$Python = Find-Python

if (-not $Python -and $InstallPythonWithWinget) {
    $winget = Get-Command winget.exe -ErrorAction SilentlyContinue
    if (-not $winget) {
        throw "winget not found. Install Python 3 manually from https://www.python.org/downloads/"
    }

    Write-Host "[python] Installing Python 3 via winget..."
    winget install --id Python.Python.3.12 --source winget --accept-package-agreements --accept-source-agreements
    $Python = Find-Python
}

if (-not $Python) {
    Write-Host ""
    Write-Host "Python 3 not found."
    Write-Host "Option 1: install Python 3 from https://www.python.org/downloads/"
    Write-Host "Option 2: rerun this script with:"
    Write-Host '  powershell -NoProfile -ExecutionPolicy Bypass -File ".\install_dependencies.ps1" -InstallPythonWithWinget'
    exit 1
}

Write-Host "[python] $($Python.Exe) $($Python.Args -join ' ')"
& $Python.Exe @($Python.Args) --version
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

if ($Force -and (Test-Path -LiteralPath $Deps)) {
    Write-Host "[deps] Removing existing pydeps..."
    Remove-Item -LiteralPath $Deps -Recurse -Force
}

New-Item -ItemType Directory -Force -Path $Deps | Out-Null

Write-Host "[pip] Upgrade pip..."
& $Python.Exe @($Python.Args) -m pip install --upgrade pip
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "[deps] Installing into: $Deps"
& $Python.Exe @($Python.Args) -m pip install --upgrade --target $Deps -r $Requirements
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host ""
Write-Host "[OK] Dependencies installed."
Write-Host "You can launch:"
Write-Host ("  " + (Join-Path $PackageRoot "LANCER_UNLOCKER.bat"))
Write-Host 'or:'
Write-Host '  powershell -NoProfile -ExecutionPolicy Bypass -File ".\_support\run_unlocker_core.ps1" -PS5 192.168.1.94 -Mode all'
