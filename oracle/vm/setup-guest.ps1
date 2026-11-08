# SPDX-License-Identifier: MIT OR Apache-2.0
#
# Setu oracle guest setup. Runs once, at first logon, from the seed CD (see
# autounattend.xml). It can also be run by hand in an elevated PowerShell:
#   powershell -ExecutionPolicy Bypass -File D:\setup-guest.ps1 -Seed D:\
#
# Turns a fresh Windows 11 install into a measurement device:
#   * OpenSSH server, key-only auth for the "setu" administrator, cmd.exe shell
#   * no automatic updates / sleep, so the snapshot stays stable
#   * a Defender exclusion for the oracle's working directory (C:\setu-oracle)
# It doesn't install any analysis or debugging tools (docs/CLEAN_ROOM.md §3).
#
# Docs: https://learn.microsoft.com/en-us/windows-server/administration/openssh/openssh_install_firstuse
#       https://learn.microsoft.com/en-us/windows-server/administration/openssh/openssh_server_configuration
#       https://learn.microsoft.com/en-us/windows-server/administration/openssh/openssh_keymanagement
param([string]$Seed = "D:\")

$ErrorActionPreference = "Continue"
Start-Transcript -Path C:\setu-setup.log -Append | Out-Null

function Step($msg) { Write-Host "==> $msg" }

# --- OpenSSH server -----------------------------------------------------------
Step "Installing OpenSSH server"
$cap = Get-WindowsCapability -Online -Name "OpenSSH.Server*" | Select-Object -First 1
if ($cap -and $cap.State -ne "Installed") {
    # Downloads from Windows Update; the VM has user-mode NAT networking.
    for ($i = 0; $i -lt 5; $i++) {
        try { Add-WindowsCapability -Online -Name $cap.Name -ErrorAction Stop | Out-Null; break }
        catch { Write-Host "Add-WindowsCapability failed ($_); retrying in 30s"; Start-Sleep 30 }
    }
}
if (-not (Get-Service sshd -ErrorAction SilentlyContinue)) {
    Step "Capability install unavailable: falling back to the Win32-OpenSSH MSI from GitHub"
    $msi = "$env:TEMP\OpenSSH-Win64.msi"
    $rel = Invoke-RestMethod "https://api.github.com/repos/PowerShell/Win32-OpenSSH/releases/latest"
    $url = ($rel.assets | Where-Object name -like "OpenSSH-Win64-v*.msi" | Select-Object -First 1).browser_download_url
    Invoke-WebRequest $url -OutFile $msi
    Start-Process msiexec.exe -Wait -ArgumentList "/i `"$msi`" /qn"
}

Set-Service -Name sshd -StartupType Automatic
Start-Service sshd   # creates C:\ProgramData\ssh and host keys on first start

# cmd.exe as the SSH shell: the harness quotes its commands for cmd.
New-Item -Path "HKLM:\SOFTWARE\OpenSSH" -Force | Out-Null
New-ItemProperty -Path "HKLM:\SOFTWARE\OpenSSH" -Name DefaultShell `
    -Value "C:\Windows\System32\cmd.exe" -PropertyType String -Force | Out-Null

# Key-only auth. For members of Administrators, sshd reads
# administrators_authorized_keys, which must be writable only by Administrators and SYSTEM.
Step "Installing the oracle SSH key"
$keys = "C:\ProgramData\ssh\administrators_authorized_keys"
Copy-Item (Join-Path $Seed "setu_oracle.pub") $keys -Force
icacls.exe $keys /inheritance:r /grant "*S-1-5-32-544:F" /grant "*S-1-5-18:F" | Out-Null

$cfg = "C:\ProgramData\ssh\sshd_config"
$text = Get-Content $cfg -Raw
$text = $text -replace "(?m)^#?\s*PasswordAuthentication\s+\w+", "PasswordAuthentication no"
if ($text -notmatch "(?m)^PasswordAuthentication") { $text += "`r`nPasswordAuthentication no`r`n" }
Set-Content -Path $cfg -Value $text -Encoding ascii
Restart-Service sshd

if (-not (Get-NetFirewallRule -Name "OpenSSH-Server-In-TCP" -ErrorAction SilentlyContinue)) {
    New-NetFirewallRule -Name "OpenSSH-Server-In-TCP" -DisplayName "OpenSSH Server (sshd)" `
        -Enabled True -Direction Inbound -Protocol TCP -Action Allow -LocalPort 22 | Out-Null
}
# The rule the OpenSSH capability creates allows only the Private profile.
# A VM whose NAT network has no internet is "Public", so allow every profile.
Set-NetFirewallRule -Name "OpenSSH-Server-In-TCP" -Profile Any

# --- Stability: keep the snapshot quiet and comparable ----------------------
Step "Disabling automatic updates, sleep and hibernation"
New-Item -Path "HKLM:\SOFTWARE\Policies\Microsoft\Windows\WindowsUpdate\AU" -Force | Out-Null
Set-ItemProperty -Path "HKLM:\SOFTWARE\Policies\Microsoft\Windows\WindowsUpdate\AU" -Name NoAutoUpdate -Value 1 -Type DWord
Set-Service -Name wuauserv -StartupType Disabled -ErrorAction SilentlyContinue
Stop-Service -Name wuauserv -Force -ErrorAction SilentlyContinue
powercfg.exe /change standby-timeout-ac 0
powercfg.exe /change monitor-timeout-ac 0
powercfg.exe /hibernate off

Step "Adding a Defender exclusion for C:\setu-oracle"
New-Item -ItemType Directory -Path C:\setu-oracle -Force | Out-Null
try { Add-MpPreference -ExclusionPath "C:\setu-oracle" -ErrorAction Stop } catch { Write-Host "Defender exclusion failed: $_" }

Set-Content -Path C:\setu-oracle-ready.txt -Value "provisioned by setup-guest.ps1"
Step "Done. sshd status: $((Get-Service sshd).Status)"
Stop-Transcript | Out-Null
