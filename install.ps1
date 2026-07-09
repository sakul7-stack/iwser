# Installs iwser for the current user (no admin required):
#   - copies iwser.exe to %LOCALAPPDATA%\Programs\iwser
#   - creates Start Menu + Desktop shortcuts
#   - registers an Add/Remove Programs entry
$ErrorActionPreference = 'Stop'
$root   = if ($PSScriptRoot) { $PSScriptRoot } else { Split-Path -Parent $MyInvocation.MyCommand.Path }
$src    = Join-Path $root 'build\iwser.exe'
$appdir = Join-Path $env:LOCALAPPDATA 'Programs\iwser'

if (-not (Test-Path $src)) {
    Write-Host "build\iwser.exe not found - run build.bat first." -ForegroundColor Red
    exit 1
}

New-Item -ItemType Directory -Force -Path $appdir | Out-Null
Copy-Item $src (Join-Path $appdir 'iwser.exe') -Force
foreach ($f in 'uninstall.bat','uninstall.ps1') {
    $p = Join-Path $root $f
    if (Test-Path $p) { Copy-Item $p (Join-Path $appdir $f) -Force }
}
$exe = Join-Path $appdir 'iwser.exe'

# Shortcuts (Desktop + Start Menu\Programs)
$sh = New-Object -ComObject WScript.Shell
foreach ($dir in @([Environment]::GetFolderPath('Desktop'), [Environment]::GetFolderPath('Programs'))) {
    $lnk = $sh.CreateShortcut((Join-Path $dir 'iwser.lnk'))
    $lnk.TargetPath       = $exe
    $lnk.WorkingDirectory = $appdir
    $lnk.IconLocation     = "$exe,0"
    $lnk.Description       = 'iwser browser'
    $lnk.Save()
}

# Add/Remove Programs entry (per-user)
$key = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\iwser'
New-Item -Path $key -Force | Out-Null
Set-ItemProperty $key DisplayName     'iwser'
Set-ItemProperty $key DisplayVersion  '1.0'
Set-ItemProperty $key Publisher        'iwser'
Set-ItemProperty $key DisplayIcon      $exe
Set-ItemProperty $key InstallLocation  $appdir
Set-ItemProperty $key UninstallString  "`"$appdir\uninstall.bat`""
Set-ItemProperty $key NoModify 1 -Type DWord
Set-ItemProperty $key NoRepair 1 -Type DWord

Write-Host "iwser installed to $appdir" -ForegroundColor Green
Write-Host "Launch 'iwser' from the Start menu or the desktop shortcut."
