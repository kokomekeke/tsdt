$Env:LOCAL_ARTIFACTS="C:\temp\scg-build"
$Env:REMOTE_ARTIFACTS="\\10.1.1.142\LargeShare\temp\pipelines\scg"

New-Item -ItemType Directory -Path $Env:LOCAL_ARTIFACTS -Force
New-Item -ItemType Directory -Path $Env:REMOTE_ARTIFACTS -Force


$vsPath = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" `
    -latest `
    -products * `
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
    -property installationPath

$vsDevCmd = Join-Path $vsPath "Common7\Tools\VsDevCmd.bat"

Write-Host "Detected VS installation: $vsPath"
Write-Host "VsDevCmd path: $vsDevCmd"

if (!(Test-Path $vsDevCmd)) {
    Write-Host "VsDevCmd not found in detected installation"
    exit 1
}

& "$vsDevCmd"

python tools\package.py `
  --type=release `
  --platform=windows `
  --outdir="$Env:LOCAL_ARTIFACTS"

Move-Item -Path "$Env:LOCAL_ARTIFACTS\*" -Destination $Env:REMOTE_ARTIFACTS -Force
