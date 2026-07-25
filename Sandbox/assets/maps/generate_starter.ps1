# Generates Sandbox/assets/maps/starter.vox: a green floor with four coloured pillars.
$ErrorActionPreference = "Stop"

$bytes = New-Object System.Collections.Generic.List[byte]

function Add-Int([int]$v) {
    $script:bytes.AddRange([System.BitConverter]::GetBytes([int]$v))
}
function Add-Tag([string]$t) {
    $script:bytes.AddRange([System.Text.Encoding]::ASCII.GetBytes($t))
}

# Build voxel list (vox space, Z up): x,y in 0..15, z up.
$vox = New-Object System.Collections.Generic.List[byte[]]
for ($x = 0; $x -lt 16; $x++) {
    for ($y = 0; $y -lt 16; $y++) {
        $vox.Add([byte[]]@($x, $y, 0, 1)) # floor, colour index 1
    }
}
$pillars = @(
    @(2, 2, 2),    # x, y, colour index
    @(13, 2, 3),
    @(2, 13, 4),
    @(13, 13, 5)
)
foreach ($p in $pillars) {
    for ($z = 1; $z -le 5; $z++) {
        $vox.Add([byte[]]@($p[0], $p[1], $z, $p[2]))
    }
}

# SIZE chunk.
$size = New-Object System.Collections.Generic.List[byte]
$size.AddRange([System.Text.Encoding]::ASCII.GetBytes("SIZE"))
$size.AddRange([System.BitConverter]::GetBytes([int]12))
$size.AddRange([System.BitConverter]::GetBytes([int]0))
$size.AddRange([System.BitConverter]::GetBytes([int]16)) # sx
$size.AddRange([System.BitConverter]::GetBytes([int]16)) # sy
$size.AddRange([System.BitConverter]::GetBytes([int]6))  # sz

# XYZI chunk.
$xyzi = New-Object System.Collections.Generic.List[byte]
$xyzi.AddRange([System.Text.Encoding]::ASCII.GetBytes("XYZI"))
$xyzi.AddRange([System.BitConverter]::GetBytes([int](4 + 4 * $vox.Count)))
$xyzi.AddRange([System.BitConverter]::GetBytes([int]0))
$xyzi.AddRange([System.BitConverter]::GetBytes([int]$vox.Count))
foreach ($v in $vox) { $xyzi.AddRange($v) }

# RGBA chunk: entry j is colour index j+1.
$palette = @{
    1 = @(60, 180, 60)    # green floor
    2 = @(200, 40, 40)    # red
    3 = @(230, 215, 60)   # yellow
    4 = @(60, 110, 220)   # blue
    5 = @(240, 240, 240)  # white
}
$rgba = New-Object System.Collections.Generic.List[byte]
$rgba.AddRange([System.Text.Encoding]::ASCII.GetBytes("RGBA"))
$rgba.AddRange([System.BitConverter]::GetBytes([int](256 * 4)))
$rgba.AddRange([System.BitConverter]::GetBytes([int]0))
for ($j = 0; $j -lt 256; $j++) {
    $index = $j + 1
    if ($palette.ContainsKey($index)) {
        $c = $palette[$index]
        $rgba.Add([byte]$c[0]); $rgba.Add([byte]$c[1]); $rgba.Add([byte]$c[2]); $rgba.Add([byte]255)
    } else {
        $rgba.Add([byte]0); $rgba.Add([byte]0); $rgba.Add([byte]0); $rgba.Add([byte]0)
    }
}

# Header + MAIN + children.
Add-Tag "VOX "
Add-Int 150
Add-Tag "MAIN"
Add-Int 0
Add-Int ($size.Count + $xyzi.Count + $rgba.Count)
$bytes.AddRange($size)
$bytes.AddRange($xyzi)
$bytes.AddRange($rgba)

$outPath = Join-Path $PSScriptRoot "starter.vox"
[System.IO.File]::WriteAllBytes($outPath, $bytes.ToArray())
Write-Host "Wrote $outPath ($($bytes.Count) bytes)"
