# Generates GameApp/assets/models/player.vox: a blocky placeholder player.
#
# Facing: the model faces +x, matching Heading.h's convention that yaw 0
# faces +x and yaw grows toward +z. The face marker on the head is the
# asymmetric front-facing detail, planted at max vox X (Cubit's forward +x
# after axis conversion below) so the model cannot silently be backwards.
$ErrorActionPreference = "Stop"

$bytes = New-Object System.Collections.Generic.List[byte]

function Add-Int([int]$v) {
    $script:bytes.AddRange([System.BitConverter]::GetBytes([int]$v))
}
function Add-Tag([string]$t) {
    $script:bytes.AddRange([System.Text.Encoding]::ASCII.GetBytes($t))
}

# Build voxel list (vox space, Z up). VoxLoader converts vox (x, y, z) to
# Cubit (x, z, y), so vox X stays Cubit's forward/back depth, vox Z becomes
# Cubit's up, and vox Y becomes Cubit's left/right width. sx=4 (depth),
# sy=6 (width), sz=18 (height) therefore lands as a Cubit-space model that is
# 4 deep, 18 tall and 6 wide - matching the brief's "6 wide, 4 deep, 18 tall".
$vox = New-Object System.Collections.Generic.List[byte[]]

# Legs: narrow, planted under the torso's edges with a gap between them.
# x 1..2 centres the 2-deep legs in the 4-deep body. z 0..6 is 7 tall.
foreach ($legY in 1, 4) {
    for ($x = 1; $x -le 2; $x++) {
        for ($z = 0; $z -le 6; $z++) {
            $vox.Add([byte[]]@($x, $legY, $z, 1)) # colour 1: pants
        }
    }
}

# Torso: full depth, centred width (y 1..4), z 7..13 is 7 tall.
for ($x = 0; $x -le 3; $x++) {
    for ($y = 1; $y -le 4; $y++) {
        for ($z = 7; $z -le 13; $z++) {
            $vox.Add([byte[]]@($x, $y, $z, 2)) # colour 2: torso
        }
    }
}

# Arms: one column either side of the torso, same height range.
foreach ($armY in 0, 5) {
    for ($x = 0; $x -le 3; $x++) {
        for ($z = 7; $z -le 13; $z++) {
            $vox.Add([byte[]]@($x, $armY, $z, 3)) # colour 3: skin
        }
    }
}

# Head: z 14..17 is 4 tall, same width as the torso. The face patch at the
# maximum x (the front, once loaded) is carved out of the plain skin block
# and given its own colour so the model reads as facing one way, not the
# other - a mirror-image head would look identical from behind otherwise.
$facePatch = @()
for ($y = 2; $y -le 3; $y++) {
    for ($z = 15; $z -le 16; $z++) {
        $facePatch += , @(3, $y, $z)
    }
}
for ($x = 0; $x -le 3; $x++) {
    for ($y = 1; $y -le 4; $y++) {
        for ($z = 14; $z -le 17; $z++) {
            $isFace = $false
            foreach ($f in $facePatch) {
                if ($f[0] -eq $x -and $f[1] -eq $y -and $f[2] -eq $z) { $isFace = $true; break }
            }
            if (-not $isFace) {
                $vox.Add([byte[]]@($x, $y, $z, 3)) # colour 3: skin
            }
        }
    }
}
foreach ($f in $facePatch) {
    $vox.Add([byte[]]@($f[0], $f[1], $f[2], 4)) # colour 4: face marker
}

# SIZE chunk.
$size = New-Object System.Collections.Generic.List[byte]
$size.AddRange([System.Text.Encoding]::ASCII.GetBytes("SIZE"))
$size.AddRange([System.BitConverter]::GetBytes([int]12))
$size.AddRange([System.BitConverter]::GetBytes([int]0))
$size.AddRange([System.BitConverter]::GetBytes([int]4))  # sx (depth)
$size.AddRange([System.BitConverter]::GetBytes([int]6))  # sy (width)
$size.AddRange([System.BitConverter]::GetBytes([int]18)) # sz (height)

# XYZI chunk.
$xyzi = New-Object System.Collections.Generic.List[byte]
$xyzi.AddRange([System.Text.Encoding]::ASCII.GetBytes("XYZI"))
$xyzi.AddRange([System.BitConverter]::GetBytes([int](4 + 4 * $vox.Count)))
$xyzi.AddRange([System.BitConverter]::GetBytes([int]0))
$xyzi.AddRange([System.BitConverter]::GetBytes([int]$vox.Count))
foreach ($v in $vox) { $xyzi.AddRange($v) }

# RGBA chunk: entry j is colour index j+1.
$palette = @{
    1 = @(45, 45, 95)     # pants
    2 = @(60, 140, 70)    # torso
    3 = @(225, 180, 140)  # skin
    4 = @(250, 230, 60)   # face marker
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

$outPath = Join-Path $PSScriptRoot "player.vox"
[System.IO.File]::WriteAllBytes($outPath, $bytes.ToArray())
Write-Host "Wrote $outPath ($($bytes.Count) bytes)"
