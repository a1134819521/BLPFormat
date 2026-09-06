$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$pluginPath = Join-Path $projectRoot 'build\BLPFormat2.8bi'
if (-not (Test-Path -LiteralPath $pluginPath)) { throw '请先完成 Release 构建。' }
$packageRoot = Join-Path $projectRoot 'dist\BLPFormat2-2.0.3-win-x64'
New-Item -ItemType Directory -Force -Path $packageRoot | Out-Null
foreach ($name in @('README.md', 'LICENSE', 'THIRD_PARTY.md')) {
    Copy-Item -LiteralPath (Join-Path $projectRoot $name) -Destination $packageRoot -Force
}
Copy-Item -LiteralPath $pluginPath -Destination $packageRoot -Force
Copy-Item -LiteralPath (Join-Path $projectRoot 'build\BLPFormat2Settings.exe') -Destination $packageRoot -Force
New-Item -ItemType Directory -Force -Path (Join-Path $packageRoot 'licenses'), (Join-Path $packageRoot 'docs') | Out-Null
Copy-Item -LiteralPath (Join-Path $projectRoot 'third_party\jpeg\README') -Destination (Join-Path $packageRoot 'licenses\IJG-JPEG.txt') -Force
Copy-Item -LiteralPath (Join-Path $projectRoot 'resources\stb-LICENSE.txt') -Destination (Join-Path $packageRoot 'licenses\stb-LICENSE.txt') -Force
Copy-Item -LiteralPath (Join-Path $projectRoot 'docs\VALIDATION.md') -Destination (Join-Path $packageRoot 'docs') -Force
$archivePath = Join-Path $projectRoot 'dist\BLPFormat2-2.0.3-win-x64.zip'
Compress-Archive -LiteralPath $packageRoot -DestinationPath $archivePath -Force
Get-Item -LiteralPath $archivePath
