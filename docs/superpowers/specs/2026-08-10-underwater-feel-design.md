# Underwater Feel — Design

**Date:** 2026-08-10
**Status:** Approved (pending spec review)

## Goal

Make the river feel like water rather than a blue region you fall through. Three
changes, sharing one foundation:

1. **Looks wet.** Submerging washes the screen blue and hazes distance out.
2. **Cannot be dug.** Water is scenery: not breakable, not placeable.
3. **Can be swum.** Space rises, releasing sinks, movement is damped.

## Context

[Transparency phase 2](2026-08-08-transparency-solidity-design.md) made water
non-solid on 2026-08-08: the player falls through it and stands on the riverbed,
and the editing raycast still stops at water so it can be dug out.

That left water correct but inert. It looks identical whether you are in it or
beside it, it behaves as a material you can mine, and entering it is
indistinguishable from stepping off a ledge.

The engine now names three block properties — *present* (`block != 0`,
palette-blind), *opaque* (alpha >= 1.0), *solid* (alpha >= 1.0). This design adds
no fourth property; it adds a derived question the three cannot answer directly.

## The foundation: `IsBlockFluid`

Every one of the three features asks the same thing, and no existing predicate
says it. Water is *present but not solid* — and nothing else in the engine is:

| | present | solid | fluid |
|---|---|---|---|
| Stone | yes | yes | no |
| Water | yes | no | **yes** |
| Air | no | no | no |

```cpp
//Reports whether a block occupies this cell without stopping movement — water,
//and anything else you can swim through. Air is not fluid: it is not present at
//all, so it falls out of the rule rather than needing a special case.
bool World::IsBlockFluid(int x, int y, int z) const
{
    return IsBlockPresent(x, y, z) && !IsBlockSolid(x, y, z);
}
```

**Derived, not a fourth table.** It composes two existing table lookups. Adding
storage would mean a third array to keep in step with a palette change, for a
question that is one `&&`.

Out of bounds is not fluid, because it is not present — consistent with
`GetBlock` reading as air.

## Eye and body are separate questions

They genuinely differ, and using one for both would be wrong in ordinary play.
Standing on the riverbed the eye sits at y=10.6 with the surface at y=11 — visually
submerged. Wading the shallows puts legs in water with the head in open air.

- **Eye in fluid** drives the tint and the fog. A single-cell query at the camera.
- **Body in fluid** drives the swim physics. A box sweep, so it gets an engine
  helper beside the existing solid one:

```cpp
//Reports whether the box covers any fluid block. The swim rules key off the
//body, not the camera: a player wading with their head clear is still walking.
static bool VoxelCollision::OverlapsFluid(
    const World& world, const glm::vec3& position, const glm::vec3& halfExtents);
```

`OverlapsFluid` reuses the shape of the existing `Overlaps` — the same corner
sweep with `IsBlockFluid` in place of `IsBlockSolid`.

**Any overlap counts**, matching `Overlaps`. So a player with only their feet in
water is swimming, not wading. On the shipped map the distinction is
unobservable — the river is a uniform two blocks deep with sand banks and no
shallows — so the simpler rule wins. If a later map has genuinely shallow water
and ankle-deep sluggishness feels wrong, the fix is to test the box centre
instead, which is a one-line change inside this helper and touches no caller.

Both results are published on `HudState`, matching how the sandbox already hands
the overlay what it needs without either layer knowing about the other:

```cpp
bool EyeInFluid = false;   // tint + fog
bool BodyInFluid = false;  // swim (also drawn on the HUD as a debug readout)
```

## Feature 1 — Looks wet

### Distance haze

The sandbox's world shader gains three uniforms and passes world position
through. Fog is exponential, which needs no far-plane constant and never
saturates abruptly:

```glsl
// vertex — v_WorldPos joins v_Color
v_WorldPos = (u_Transform * vec4(a_Position, 1.0)).xyz;

// fragment
uniform vec3  u_FogColor;
uniform float u_FogDensity;   // 0.0 when dry
uniform vec3  u_CameraPos;

float d = length(v_WorldPos - u_CameraPos);
float f = 1.0 - exp(-u_FogDensity * d);
color = vec4(mix(v_Color.rgb, u_FogColor, f), v_Color.a);
```

`u_Transform` already carries `WorldOffset`, and the camera position the sandbox
passes is the same one `WorldRenderer::Render` uses for its transparency sort, so
both are in one space. That shared space is an existing invariant, not a new one.

**Dry costs one `mix` against zero.** Density 0 makes `f` 0 and the output
identical to today, so there is no branch and no second shader.

Uniforms are set once per frame in `SandboxLayer::OnRender` before
`m_WorldRenderer.Render(...)`. `Shader`'s setters call `Bind()` themselves and
uniforms are program state, so they persist across the per-chunk `Submit` calls
that only set `u_ViewProjection` and `u_Transform`.

The transparent pass uses the same shader, so the water surface fogs too. At the
distances water is seen from that is a small effect, and excluding it would need
a second shader.

### Screen wash

A fullscreen quad in `HudLayer`, drawn after the scene and before the text, when
`EyeInFluid`. It reuses the overlay's existing unit quad, ortho camera and
`u_Tint`; the only new asset is a 1x1 white texture, since the shader always
samples one.

The wash also covers the sky, which the fog cannot reach — without it, looking up
from underwater shows an untouched clear colour.

Starting values, tuned by eye during verification:

| | value |
|---|---|
| `UnderwaterTint` | `vec4(0.15, 0.40, 0.70, 0.45)` |
| `FogColor` | `vec3(0.10, 0.30, 0.55)` |
| `FogDensity` | `0.06` submerged, `0.0` dry |

At density 0.06 the fog is roughly half-strength at the 12-block reach distance
and 83% at 30 blocks.

## Feature 2 — Water cannot be dug

`VoxelRaycast::Cast` gains `bool solidOnly = false`; when set it reports only
blocks that stop movement, passing through fluid. The sandbox's edit cast sets it.

**This replaces `skipStartVoxel`, which is removed.** That parameter was added on
2026-08-08 for exactly one problem: the camera sitting inside a water block made
every click hit that block. `solidOnly` fixes the same problem at its cause —
water is not a candidate at all — and does it for aiming as well as for the origin
cell. Keeping both would put two overlapping booleans on one function.

Removing it also restores a behaviour it suppressed: a player somehow embedded in
*solid* terrain can once again break the block they are stuck in, which is a way
out rather than a bug.

The default stays presence, so the engine's documented behaviour and the existing
test that pins it are unchanged.

`PlaceableBlocks` drops water, becoming `{1, 2, 3, 4, 5, 6, 8}` — a block that
cannot be removed must not be placeable. The list is indexed off `KeyCode::D1`,
so the keys shift: **7 now selects `Wood`**, and **8 selects nothing**, because
`PlaceableBlockCount` falls to 7 and the bounds test rejects it. Losing a
selectable key is the intended consequence of removing a block, not an oversight.

## Feature 3 — Swimming

In `SandboxLayer::OnUpdate`, when `BodyInFluid`:

```cpp
m_VerticalVelocity -= WaterGravity * seconds;                    // 6.0, vs 24.0 dry
m_VerticalVelocity = glm::max(m_VerticalVelocity, -SinkSpeed);   // 1.5

if (Input::IsKeyPressed(KeyCode::Space))
    m_VerticalVelocity = SwimUpSpeed;                            // 3.5

walk *= WaterDrag;                                               // 0.6
```

Do nothing and you settle gently onto the bed, so what phase 2 shipped still
holds. Hold Space and you rise; release and you sink again.

**The jump test has to be reordered.** Today it reads
`if (m_Grounded && Input::IsKeyPressed(KeyCode::Space))`. Standing on the
riverbed is grounded *and* submerged, so without reordering a jump would fire
instead of a swim stroke. The swim branch takes Space first and the dry jump
becomes its `else`.

No new key is bound, no drowning, no oxygen, no surface bobbing.

## Testing

Doctest, per project convention. A test installs a palette with an alpha-0.55
entry to get a fluid id; no map asset is needed.

**Engine, unit tested:**
- `IsBlockFluid`: true for a transparent id, false for an opaque one, false for
  air, false out of bounds.
- `IsBlockFluid` tracks a palette replacement, like its two components do.
- `OverlapsFluid`: true for a box inside water, false in air, false in stone,
  true for a box straddling water and air.
- `Cast` with `solidOnly` passes through water and reports the bed behind it.
- `Cast` with `solidOnly` still reports an opaque block normally.
- `Cast` default (unset) still stops at water — the existing case, kept.

**Removed:** the two `skipStartVoxel` cases, along with the parameter. The
existing "A ray stops at water rather than passing through it" case survives but
its comment must be rewritten: it currently justifies itself by water being
placeable with key 7, which this design makes false.

**Not unit tested** — verified by running the sandbox, closing via `WM_CLOSE` so
the log survives, and reading the HUD:
- Submerging washes the screen blue and hazes the far bank out.
- Space rises, releasing settles back onto the bed.
- Left-clicking while wading digs the riverbed, never the water.
- Key 7 selects wood, and no key places water.

Getting into the river needs no keyboard: temporarily move `SpawnPosition` to
`{127.5, 30.0, 112.5}` — the channel runs along z on the mirror plane at x=127 —
and let gravity do it. Revert before committing.

## Out of scope

- Drowning, oxygen, or any damage from being submerged.
- Surface bobbing, buoyant floating, or a dive key.
- Swimming animation, splash particles, or water sound.
- Caustics, refraction, or a moving water surface.
- Per-block light absorption (deeper water reading darker) — deferred since
  phase 1.
- Fluid flow, spreading, or water physics of any kind. Water is static scenery.
- Any fluid other than the map's water.
