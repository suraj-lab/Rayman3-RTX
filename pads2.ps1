$src = @'
using System;
using System.Runtime.InteropServices;
public static class XI {
    [StructLayout(LayoutKind.Sequential)]
    public struct STATE {
        public uint packet;
        public ushort buttons;
        public byte lt, rt;
        public short lx, ly, rx, ry;
    }
    [DllImport("xinput1_4.dll")] public static extern int XInputGetState(uint i, out STATE s);
    [DllImport("xinput1_4.dll")] public static extern int XInputGetCapabilities(uint i, uint flags, out CAPS c);
    [StructLayout(LayoutKind.Sequential)]
    public struct CAPS { public ushort type; public ushort sub; public uint flags; public GAMEPAD gp; public VIB vib; }
    [StructLayout(LayoutKind.Sequential)]
    public struct GAMEPAD { public ushort buttons; public byte lt, rt; public short lx, ly, rx, ry; }
    [StructLayout(LayoutKind.Sequential)]
    public struct VIB { public ushort l, r; }
}
'@
Add-Type -TypeDefinition $src

$found = $false
for ($i = 0; $i -lt 4; $i++) {
    $s = New-Object 'XI+STATE'
    $r = [XI]::XInputGetState([uint32]$i, [ref]$s)
    if ($r -eq 0) {
        $found = $true
        Write-Output ("  XInput slot {0}: CONNECTED  buttons=0x{1:X4}  LX={2} LY={3} RX={4} RY={5}" -f $i, $s.buttons, $s.lx, $s.ly, $s.rx, $s.ry)
    } else {
        Write-Output ("  XInput slot {0}: empty (err {1})" -f $i, $r)
    }
}
if (-not $found) { Write-Output "  => no XInput device present" }

Write-Output ""
Write-Output "=== raw HID game controllers (DirectInput side) ==="
Get-PnpDevice -PresentOnly -Class HIDClass -ErrorAction SilentlyContinue |
    Where-Object { $_.FriendlyName -match 'game controller|DualSense|DualShock|Wireless Controller' } |
    Select-Object Status, FriendlyName |
    Format-Table -AutoSize
