# Removes iwser for the current user: shortcuts, registry entry, and app folder.
$appdir = Join-Path $env:LOCALAPPDATA 'Programs\iwser'
foreach ($dir in @([Environment]::GetFolderPath('Desktop'), [Environment]::GetFolderPath('Programs'))) {
    $p = Join-Path $dir 'iwser.lnk'
    if (Test-Path $p) { Remove-Item $p -Force -ErrorAction SilentlyContinue }
}
Remove-Item 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\iwser' -Recurse -Force -ErrorAction SilentlyContinue
if (Test-Path $appdir) { Remove-Item $appdir -Recurse -Force -ErrorAction SilentlyContinue }
Write-Host 'iwser uninstalled.' -ForegroundColor Green
