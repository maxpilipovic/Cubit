# Design Notes

Rationale from commit messages, moved here so the git log stays to one line per
commit. Entries are oldest first and keyed by date and subject rather than commit
hash, because the hashes changed when the messages were rewritten.

## Render a meshed voxel chunk in the sandbox

*18 July 2026*

Replace the hardcoded cube with a chunk built from test terrain and
meshed by ChunkMesher, which had no caller until now. The test chunk
meshes 1510 solid blocks into 1122 exposed faces instead of 9060,
matching an independently computed face count.

Enable backface culling now that a mesh depends on consistent winding.

## Add a doctest suite for voxel chunk logic

*18 July 2026*

Cover Chunk storage and ChunkMesher, the engine code that is pure enough
to test without a GPU or window. Rendering stays verified by running the
sandbox.

ChunkMesher is checked against an independent face-counting oracle rather
than against itself, plus exact counts for small cases. The fully-solid
chunk case pins the current out-of-bounds-is-air border rule at 1536
faces so neighbour-aware meshing has to change it deliberately.

Verified the suite fails when the mesher skips a culling check and when
chunk index math aliases positions.

## Add voxel raycasting

*19 July 2026*

Walk a ray voxel by voxel with grid traversal and report the first solid
block it enters, along with the face it entered through. Terrain editing
needs the face normal to know where a placed block goes.

Positions outside the chunk read as air, matching Chunk::GetBlock, so a
ray can start outside the chunk and travel into it.

Verified the tests fail when the entry face normal is inverted.

## Break and place blocks in the sandbox

*19 July 2026*

Cast a ray from the camera on mouse press and edit the block it finds:
left click clears it, right click fills the empty block against the face
that was hit. Each edit rebuilds the chunk mesh and its GPU buffers.

Recreating the buffers per edit keeps the change small. A single chunk
remesh is cheap, and sizing buffers up front would commit to a buffer API
before multi-chunk streaming exists to shape it.

Move the chunk closer to the camera so terrain starts within reach.

Placing against the chunk's outer border targets a position outside the
chunk and is rejected, which is expected while the world is one chunk.
Verified by digging a block and filling it back in: the mesh goes from
1122 to 1126 faces and back to 1122.

## Add box collision against voxel terrain

*19 July 2026*

Move an axis-aligned box through a chunk and stop it against solid blocks.
Each axis resolves separately, so a box blocked on one axis keeps sliding
along the others instead of sticking to walls.

Moves are split into steps of at most a quarter block. This stops a fast
box from skipping over a block entirely, and it is also what keeps the
snap-back correct: pushing a box out of a block assumes it penetrated by
less than one block.

Report which axes were blocked, and whether downward motion was stopped,
so a character controller can tell when it is standing on ground.

Verified the tests fail when stepping is removed.

## Walk on the terrain instead of flying through it

*19 July 2026*

Give the sandbox a player box that falls under gravity, stands on solid
blocks, jumps, and slides along walls. The camera now follows the player
at eye height rather than moving freely.

The camera controller gains SetPosition so a caller that owns movement can
drive the view. Player state stays in the sandbox: there is no game loop
yet to shape an engine-side character controller, and guessing at that
interface now would be premature.

Walking off the chunk returns the player to spawn, since a single chunk is
not a closed world.

Verified in the sandbox: the player falls from spawn and lands at y=5.9,
which is the terrain surface plus half the player's height.

## Add assertions, flush logs, and run tests on build

*19 July 2026*

Flush every log message. std::cout is fully buffered when redirected, so
until now a crash or a killed process discarded its entire log, which is
exactly when the log matters most.

Add CB_ASSERT and CB_CORE_ASSERT for internal expectations that indicate a
bug rather than bad input. They compile out when CB_DEBUG is not set, so
they document invariants without becoming load-bearing. Use them where
Chunk and ChunkMesher already assume a caller has done the checking.

Run Tests.exe as a post-build step. The suite already returned non-zero on
failure but nothing invoked it, so it only protected what someone
remembered to check.

Verified a failing test now fails the build, and that an assertion reports
its message, condition, and location before stopping the process.

## Draw a crosshair through a screen-space overlay

*19 July 2026*

Add Texture2D, alpha blending, and a depth-test toggle, then use them for a
sandbox overlay layer that draws a crosshair at the centre of the screen.
Aiming was previously guesswork, since edits target whatever the view ray
hits with nothing on screen to aim with.

Textures sample unfiltered. Crosshairs, glyphs, and block textures are all
pixel art, and filtering would blur them.

The overlay sizes itself from the framebuffer rather than window
coordinates, because that is what the viewport is set from and the two can
differ on scaled displays.

Expose Application::GetWindow so a layer can read that size.

Texture creation from image files is deliberately absent. There is no image
asset to load yet, and the crosshair is generated in code, so a loader
would be untested and unused until block textures need one.

## Show player and mesh state in the debug HUD

*19 July 2026*

Draw position, grounded state, mesh face count, and frame rate in the
overlay. These values were only observable by reading log lines, which made
every gameplay change slower to check than it needed to be.

Text uses a 5x7 bitmap font defined in code rather than a font asset. The
readout needs about two dozen glyphs, and loading a real font would mean
inventing an asset directory and load-path convention as a side effect of a
HUD change. The glyphs are meant to be replaced once block textures force
that decision properly.

The gameplay layer publishes to a shared HudState that the overlay reads,
so neither layer needs to know about the other.

Glyph rows are written into the atlas bottom-up, since texture row zero is
the bottom of a quad and the text would otherwise render upside down.

## Restore move operations on Texture2D

*19 July 2026*

Keeps it consistent with the other OpenGL resource wrappers and allows a texture to live in a container, which block textures will want.

## Give blocks colours

*19 July 2026*

Replace the six hardcoded face colours in the mesher with the colour of the
block being meshed, multiplied by a per-face brightness so a solid colour
still reads as a cube.

Blocks carry a colour rather than a material because that is what the game
needs. Voxel map formats store colour per voxel and team-coloured terrain is
the point, so there is nothing here to texture. The named entries are a
placeholder for a data-driven palette once maps load from a file.

The sandbox bands its test terrain by depth and binds the number keys to the
colour used when placing.

Chunk storage is unchanged at one byte per block, and BlockType::Solid still
means the same thing, so the existing tests were unaffected.

## Add a fixed grid of chunks

*19 July 2026*

Introduce World, which holds a grid of chunks and addresses blocks in world
coordinates, converting to a chunk and an offset inside it.

The grid is fixed at construction. Maps are loaded whole rather than
generated as the player moves, so there is no streaming, no loading and
unloading, and no background generation to design around.

Bounds are checked before converting to chunk coordinates, so the division
never sees a negative value and cannot truncate the wrong way.

Nothing uses World yet. The sandbox still renders a single chunk, and this
lands on its own so the coordinate handling can be tested before the mesher,
raycast, and collision are moved onto it.

Verified the tests fail when the chunk index aliases two grid positions.

## Mesh chunks against their neighbours

*19 July 2026*

ChunkMesher now takes a world and a chunk position rather than a lone chunk,
and looks neighbours up in world coordinates. A face shared with a solid
block in the next chunk is no longer emitted, so a chunk grid does not bury
a full redundant wall at every seam.

Vertices stay in chunk-local coordinates so each chunk mesh can be placed by
its origin.

A solid chunk at the edge of the world still meshes all six sides, because
outside the world is air. That rule did not change, only what counts as
outside.

Strengthen the seam test. Looking neighbours up in chunk-local coordinates
hides the opposite face but still hides exactly one, so a face count alone
could not tell the two apart. The test now builds the far chunk in a world
that is asymmetric about the seam, where the counts differ.

The sandbox uses a one chunk world, so nothing it does changes yet. Raycast
and collision still take a single chunk and read that one.

## Trim the readme

*19 July 2026*

It had grown a changelog-style feature list, mesh face counts that belong in commit messages, and several paragraphs explaining the test suite's reasoning. Cut to what someone opening the repo actually needs.

Also removes two stale claims: that the fully solid chunk test would fail once chunks meshed against their neighbours, which happened today without changing it, and a planned systems list that still listed finished work.

