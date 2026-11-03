# race.ps1 - starts go.sh in the ZeekTest WSL Linux and keeps the PC awake while it runs (no settings are changed;
# the keep-awake request ends when this window closes). Michael Hugh Harrington, 2026. MIT License.
Add-Type -Name P -Namespace W -MemberDefinition '[DllImport("kernel32.dll")] public static extern uint SetThreadExecutionState(uint f);'
[W.P]::SetThreadExecutionState([uint32]"0x80000001") | Out-Null
Write-Host "Mahoney test sets race is running. Progress: progress.txt in this folder."
& "$env:SystemRoot\System32\wsl.exe" -d ZeekTest -u root --cd "$PSScriptRoot" -e bash go.sh
[W.P]::SetThreadExecutionState([uint32]"0x80000000") | Out-Null
