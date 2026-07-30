# Regenerate the VS solution and build. Usage: .\build.ps1 [Debug|Release]
param([string]$Config = "Debug")
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
Set-Location $root

& "$root\tools\premake5.exe" vs2026
$sln = if (Test-Path "$root\build\TrinityCoreStudio.slnx") { "$root\build\TrinityCoreStudio.slnx" } else { "$root\build\TrinityCoreStudio.sln" }
if (-not (Test-Path $sln)) { throw "Solution not generated" }

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -property installationPath
$msbuild = Join-Path $vs 'MSBuild\Current\Bin\MSBuild.exe'

& $msbuild $sln /p:Configuration=$Config /p:Platform=x64 /m /nologo /clp:Summary /v:minimal
if ($LASTEXITCODE -ne 0) { throw "Build failed ($LASTEXITCODE)" }
Write-Host "`nBuild OK -> $root\bin\$Config\TrinityCoreStudio.exe" -ForegroundColor Green
