# Lists attached game controllers and any virtual/XInput shims present.
Write-Output "=== PnP game controllers present ==="
Get-PnpDevice -PresentOnly -ErrorAction SilentlyContinue |
    Where-Object { $_.FriendlyName -match 'Controller|DualSense|DualShock|Wireless|Gamepad|Sony|PLAYSTATION|Xbox' } |
    Select-Object Status, Class, FriendlyName |
    Format-Table -AutoSize

Write-Output "=== XInput slots (1-4) ==="
$sig = @'
using System;
using System.Runtime.InteropServices;
public class XI {
  [StructLayout(LayoutKind.Sequential)] public struct STATE { public uint dwPacketNumber; public ushort wButtons; public byte bLeftTrigger, bRightTrigger; public short sThumbLX, sThumbLY, sThumbRX, sThumbRY; }
  [DllImport("xinput1_4.dll", EntryPoint="XInputGetState")] public static extern int GetState(uint i, out STATE s);
}
'@
try {
    Add-Type -TypeDefinition $sig -ErrorAction Stop
    for ($i = 0; $i -lt 4; $i++) {
        $s = New-Object XInputPlusXI.STATE 2>$null
        $st = New-Object "XI+STATE"
        $r = [XI]::GetState($i, [ref]$st)
        Write-Output ("  slot {0}: {1}" -f $i, $(if ($r -eq 0) { "CONNECTED buttons=0x{0:X4}" -f $st.wButtons } else { "empty (err $r)" }))
    }
} catch {
    Write-Output ("  xinput probe failed: " + $_.Exception.Message)
}

Write-Output "=== DirectInput enumeration (16 slots) ==="
$di = @'
using System;
using System.Runtime.InteropServices;
public class DI {
  [DllImport("dinput8.dll")] public static extern int DirectInput8Create(IntPtr h, uint ver, ref Guid iid, out IntPtr o, IntPtr u);
}
'@
Write-Output "  (skipped: DirectInput needs more marshalling than is worth it here)"

Write-Output "=== Shim presence ==="
Get-Process -ErrorAction SilentlyContinue | Where-Object { $_.ProcessName -match 'DS4|DualSense|Steam|reWASD|AntiMicro|JoyX|HidHide' } |
    Select-Object ProcessName, Id | Format-Table -AutoSize
Get-PnpDevice -PresentOnly -ErrorAction SilentlyContinue |
    Where-Object { $_.FriendlyName -match 'ViGEm|DS4Windows|HidHide|Virtual Gamepad|Xbox 360' } |
    Select-Object FriendlyName | Format-Table -AutoSize
