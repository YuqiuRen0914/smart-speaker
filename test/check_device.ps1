[CmdletBinding()]
param([Parameter(Mandatory)][System.Net.IPAddress]$DeviceIp)
$ErrorActionPreference = 'Stop'
if ($DeviceIp.AddressFamily -ne [System.Net.Sockets.AddressFamily]::InterNetwork) {
    throw 'Use the device IPv4 address shown on its screen.'
}
$base = 'http://' + $DeviceIp.IPAddressToString
function Read-DeviceApi([string]$Path) {
    $result = Invoke-RestMethod -Uri ($base + $Path) -TimeoutSec 10
    if ($result.ok -ne $true) { throw "Device API failed: $Path" }
    return $result.data
}
$page = Invoke-WebRequest -Uri $base -TimeoutSec 10
if ($page.Content -notmatch '/api/v1/modules/' -or $page.Content -notmatch 'firmwareFile') {
    throw 'The device is serving an old or incomplete maintenance page.'
}
$null = Read-DeviceApi '/api/v1/device'
$capabilities = Read-DeviceApi '/api/v1/capabilities'
$ota = Read-DeviceApi '/api/v1/modules/ota/status.get'
$speaker = Read-DeviceApi '/api/v1/modules/smart_speaker/status.get'
if ($ota.project_name -ne 'lunaredu-smart-speaker') { throw 'Connected device is not a smart speaker.' }
foreach ($moduleId in @('ota', 'smart_speaker')) {
    $module = @($capabilities.modules | Where-Object id -eq $moduleId)
    if ($module.Count -ne 1) { throw "Missing module: $moduleId" }
    if ($moduleId -eq 'ota' -and -not @($module[0].operations | Where-Object { $_.id -eq 'firmware.upload' -and $_.method -eq 'PUT' }).Count) {
        throw 'Standard OTA upload operation is missing.'
    }
}
if (-not $ota.rollback_enabled) { throw 'Rollback is disabled; install the new bootloader before closing the enclosure.' }
if ($ota.pending_verify) { throw 'The new firmware is still pending verification. Wait and run this check again.' }
[pscustomobject]@{
    Address = $base
    Project = $ota.project_name
    Version = $ota.version
    Rollback = $ota.rollback_enabled
    BootConfirmed = $ota.boot_confirmed
    RunningSlot = $ota.running_label
    NextSlot = $ota.next_label
    AudioReady = $speaker.audio_ready
    Result = 'Read-only checks passed. A real upload/reboot test is still required.'
}
