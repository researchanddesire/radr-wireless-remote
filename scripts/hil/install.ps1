param([string]$Python = 'python', [string]$Destination = 'C:\rad-hil')
$ErrorActionPreference = 'Stop'
$source = (Resolve-Path $PSScriptRoot).Path
$target = [IO.Path]::GetFullPath($Destination)
if ($target -match '(^|[\\/])_work([\\/]|$)' -or $target.TrimEnd('\') -eq [IO.Path]::GetPathRoot($target)) {
    throw 'Harness installation must be outside runner work directories and drive roots.'
}
$maintainer = [Security.Principal.WindowsIdentity]::GetCurrent().User.Value
$harness = Join-Path $target 'harness'
$venv = Join-Path $target 'venv'
New-Item -ItemType Directory -Force -Path $harness,(Join-Path $target 'locks') | Out-Null
# Protect the parent too: the service must not replace the entire installation.
& icacls $target /inheritance:r /grant:r "*${maintainer}:(OI)(CI)F" '*S-1-5-32-544:(OI)(CI)F' '*S-1-5-18:(OI)(CI)F' '*S-1-5-20:(OI)(CI)RX' /Q
if ($LASTEXITCODE -ne 0) { throw 'Failed to protect the installation root.' }
Get-ChildItem -LiteralPath $source -File | Where-Object { $_.Extension -eq '.py' -or $_.Name -eq 'requirements.txt' } |
    Copy-Item -Destination $harness -Force
if (-not (Test-Path (Join-Path $venv 'Scripts\python.exe'))) {
    & $Python -m venv $venv
    if ($LASTEXITCODE -ne 0) { throw 'Could not create the isolated Python environment.' }
}
& (Join-Path $venv 'Scripts\python.exe') -m pip install -r (Join-Path $harness 'requirements.txt')
if ($LASTEXITCODE -ne 0) { throw 'Dependency installation failed.' }
$hashes = @{}
Get-ChildItem -LiteralPath $harness -File | ForEach-Object { $hashes[$_.Name] = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash }
$hashes | ConvertTo-Json | Set-Content -Encoding UTF8 (Join-Path $target 'installed-hashes.json')
# Built-in Administrators/SYSTEM own writes; NETWORK SERVICE only reads code.
foreach ($path in @($harness, $venv)) {
    & icacls $path /inheritance:r /grant:r "*${maintainer}:(OI)(CI)F" '*S-1-5-32-544:(OI)(CI)F' '*S-1-5-18:(OI)(CI)F' '*S-1-5-20:(OI)(CI)RX' /Q
    if ($LASTEXITCODE -ne 0) { throw 'Failed to restrict installation directory.' }
    & icacls (Join-Path $path '*') /reset /T /Q
    if ($LASTEXITCODE -ne 0) { throw 'Failed to restrict installed code permissions.' }
}
& icacls (Join-Path $target 'locks') /grant '*S-1-5-20:(OI)(CI)M' /Q
if ($LASTEXITCODE -ne 0) { throw 'Failed to configure fixture lock permissions.' }
Write-Output 'Reviewed harness installed. Enroll fixtures and migrate the worker before enabling checks.'
