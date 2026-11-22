$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$vswhere = 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) {
    $vswhere = 'C:\Program Files\Microsoft Visual Studio\Installer\vswhere.exe'
}
if (-not (Test-Path -LiteralPath $vswhere)) { throw 'Visual Studio Installer / vswhere.exe was not found.' }
$vs = & $vswhere -latest -products '*' -property installationPath
if (-not $vs) { throw 'Visual Studio was not found.' }
$vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat'
if (-not (Test-Path -LiteralPath $vcvars)) { throw 'The Visual C++ x64 build tools are missing. Install the Desktop development with C++ workload.' }
$outDir = Join-Path $root 'build'
New-Item -ItemType Directory -Path $outDir -Force | Out-Null
$source = Join-Path $root 'HaulETA.cpp'
$resourceScript = Join-Path $root 'HaulETA.rc'
$resource = Join-Path $outDir 'HaulETA.res'
$object = Join-Path $outDir 'HaulETA.obj'
$include = Join-Path $root 'scs_sdk_include'
$output = Join-Path $outDir 'HaulETA.dll'
foreach ($asset in 'carbon_788x104.bmp', 'metal_76x64.bmp', 'metal_120x42.bmp', 'diffuser_816x132.bmp', 'metal_62x62.bmp', 'paper_788x104.bmp') {
    if (-not (Test-Path -LiteralPath (Join-Path $root "assets\$asset"))) { throw "Missing assets\$asset - run Prepare-Textures.ps1 first." }
}
# The textures in assets\ are compiled into the DLL as bitmap resources.
$rc = 'rc.exe /nologo /fo "{0}" "{1}"' -f $resource, $resourceScript
$compile = 'cl.exe /nologo /LD /EHsc /std:c++17 /utf-8 /O2 /DUNICODE /D_UNICODE /I "{0}" /Fo"{1}" "{2}" "{3}" user32.lib gdi32.lib comctl32.lib /link /OUT:"{4}" /IMPLIB:"{5}"' -f $include, $object, $source, $resource, $output, (Join-Path $outDir 'HaulETA.lib')
# vcvars64.bat looks for vswhere.exe on PATH, so put the installer folder there first.
$env:PATH = (Split-Path -Parent $vswhere) + ';' + $env:PATH
$cmd = 'call "{0}" >nul && cd /d "{1}" && {2} && {3}' -f $vcvars, $root, $rc, $compile
& $env:ComSpec /d /s /c $cmd
if ($LASTEXITCODE -ne 0) { throw "Build failed with exit code $LASTEXITCODE" }
Write-Host "Built HaulETA Simple v1.6: $output"
