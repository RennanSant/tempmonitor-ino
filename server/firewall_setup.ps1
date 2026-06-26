$ErrorActionPreference = "Stop"

$rules = @(
    @{
        DisplayName = "TempMonitor Flask TCP 5000"
        Direction = "Inbound"
        Action = "Allow"
        Protocol = "TCP"
        LocalPort = 5000
        Profile = "Any"
    },
    @{
        DisplayName = "TempMonitor ICMPv4 Ping"
        Direction = "Inbound"
        Action = "Allow"
        Protocol = "ICMPv4"
        IcmpType = 8
        Profile = "Any"
    }
)

foreach ($ruleSpec in $rules) {
    $existingRule = Get-NetFirewallRule -DisplayName $ruleSpec.DisplayName -ErrorAction SilentlyContinue

    if ($existingRule) {
        Set-NetFirewallRule -DisplayName $ruleSpec.DisplayName -Enabled True -Profile Any -Action Allow
    } else {
        New-NetFirewallRule @ruleSpec | Out-Null
    }
}

Get-NetFirewallRule -DisplayName "TempMonitor Flask TCP 5000", "TempMonitor ICMPv4 Ping" |
    Select-Object DisplayName, Enabled, Profile, Direction, Action |
    Format-Table -AutoSize
