# Debug Line Rendering and GL Debug Output — Design

**Date:** 2026-08-22
**Status:** Shipped 2026-08-22 — see the amendments below

## Goal

Make two invisible things visible: geometry the engine reasons about but never
draws, and OpenGL errors, which today are silent.

Two independent capabilities, shipped together because they answer the same
question — *what is actually happening?*

1. **Debug lines and boxes.** Draw a line or a wireframe box from anywhere in the
   engine, for one frame.
2. **A GL debug callback.** Route driver messages into the existing logger.

## Why

There is no way to draw a line or a wireframe box, so a frustum, an AABB, a
raycast, or `FindSpawn`'s outward spiral cannot be looked at. Every visual bug in
this project so far was found by screenshotting and squinting — the buried-spawn
black screen, the camera facing the wrong way, the mid-build world that looked
like missing geometry. Each of those is a shape the engine knew and could not show.

Separately, `glGetError` and `GL_DEBUG_OUTPUT` appear nowhere in the codebase, so
a GL error today produces no output at all. It surfaces as wrong pixels, which is
the most expensive way to learn about it.

Neither is on the voxel arc. Both pay for themselves on the next bug rather than
eventually, which is why they come before gameplay rather than after.

## Context

- The context is requested as **GL 3.3 core** (`WindowsWindow.cpp:43-45`), but GLAD
  is generated through 4.6, so `glDebugMessageCallback` and
  `GL_DEBUG_OUTPUT_SYNCHRONOUS` are declared. Whether the pointer is non-null at
  runtime depends on the driver exposing `GL_KHR_debug`.
- `Renderer::Draw` hardcodes `GL_TRIANGLES` and requires an `IndexBuffer`
  (`Renderer.cpp:88-103`), so line drawing does not fit the existing submit path.
- `VertexBuffer` allocates with `GL_STATIC_DRAW` (`VertexBuffer.cpp:11`), though
  `SetData` exists.
- `Renderer::BeginScene` stores a single file-scope `s_ViewProjection`.
- The logger macros are `CB_CORE_INFO` / `CB_CORE_WARN` / `CB_CORE_ERROR`
  (`Cubit/src/Core/CoreLogger.h:10-12`).

## Part 1 — `DebugLineBatch`

A pure CPU value type, `Cubit/include/Cubit/Renderer/DebugLineBatch.h`. No GL
headers, no statics, fully unit tested — the same split that made `FrameClock`
testable while `Application::Run` was not.

```cpp
struct DebugVertex
{
    glm::vec3 Position;
    glm::vec4 Color;
};

class CB_API DebugLineBatch
{
public:
    //Appends one line segment as two vertices.
    void AddLine(const glm::vec3& from, const glm::vec3& to, const glm::vec4& color);

    //Appends a wireframe axis-aligned box as its twelve edges.
    void AddBox(const glm::vec3& min, const glm::vec3& max, const glm::vec4& color);

    //Discards everything accumulated so far.
    void Clear();

    //The accumulated vertices, in pairs, each pair one line segment.
    const std::vector<DebugVertex>& Vertices() const;
};
```

**Bounds are not normalized.** `AddBox` with `min > max` draws the inverted box it
was given rather than silently correcting it. This is a tool for seeing what is
actually there; a debug renderer that quietly fixes its input hides the bug being
looked for.

### The test that matters

`AddBox` writes 12 edges from 8 corners. The real failure is a wrong edge table,
which produces a plausible-looking wrong shape and still yields 24 vertices — so a
count assertion alone cannot fail usefully.

The discriminating check: **each of the 8 corners must appear exactly 3 times**,
once per incident edge. A transposed, duplicated, or omitted edge breaks that
distribution while leaving the total at 24.

## Part 2 — `DebugDraw`

Statics wrapping one file-scope `DebugLineBatch` plus the GPU resources, in
`Cubit/include/Cubit/Renderer/DebugDraw.h`.

```cpp
class CB_API DebugDraw
{
public:
    DebugDraw() = delete;

    static void Line(const glm::vec3& from, const glm::vec3& to, const glm::vec4& color);
    static void Box(const glm::vec3& min, const glm::vec3& max, const glm::vec4& color);

    //Draws everything accumulated this frame, then clears it.
    static void Flush(const Camera& camera, const glm::mat4& transform = glm::mat4(1.0f));

    //Discards everything accumulated without drawing it.
    static void Clear();
};
```

**Static rather than an instance**, against the `WorldRenderer` precedent, for one
reason: the point is to look at engine internals, and `FindSpawn` or
`VoxelCollision` hold no renderer reference to plumb one through. An instance API
would mean threading a parameter into exactly the call sites this exists to
inspect.

**The header stays GL-free.** `Cubit/src/Voxel/` includes no GL headers today, and
the fixed-timestep design records that as load-bearing for an eventual headless
server. Voxel code may call `DebugDraw` without acquiring a GL include.

The consequence: a headless build that calls `Line` and never calls `Flush`
accumulates without bound. `Flush` clears, and `Clear` exists for the case where
nothing flushes.

### The flush is explicit

`Flush` takes a camera rather than reusing `Renderer`'s stored `s_ViewProjection`.

An automatic end-of-frame flush was considered and rejected. `HudLayer` is pushed
as an **overlay**, so it renders last and its `Renderer::BeginScene(m_Camera)`
leaves `s_ViewProjection` holding an *orthographic* matrix. Debug lines flushed
after the layer stack would be drawn in HUD screen space. The caller flushes while
the world camera is current.

The optional `transform` mirrors `Renderer::Submit`'s. The Sandbox's camera lives
in world-plus-`WorldOffset` space while debug shapes are naturally in world space,
so one transform at the flush beats offsetting every point at every call site.

### GPU resources

One `VertexArray` and one `VertexBuffer`, lazily created on first flush, grown when
a frame's batch exceeds the current capacity. Drawn with
`glDrawArrays(GL_LINES, ...)`, bypassing `Renderer::Submit`.

A flush with an empty batch returns immediately, touching no GL state and
allocating nothing. The common case — an engine built with `DebugDraw` calls that
nothing is currently drawing — must cost a branch, not a bind.

`VertexBuffer` gains a size-only constructor allocating `GL_DYNAMIC_DRAW` — a
targeted addition to the class being used, not a refactor of it.

Depth testing stays enabled, so a box sits in the world and is occluded by terrain
in front of it. Blending is already enabled globally by `Renderer::Init`. Shader
source is an inline string literal in `DebugDraw.cpp`, matching how the Sandbox
declares its shaders, because there is no asset layer.

**Lines are one pixel.** `glLineWidth` above 1.0 is not supported in a core profile
on most drivers; thick lines need quad expansion, which is not worth building for a
debug tool.

## Part 3 — GL debug output

Two changes.

`WindowsWindow.cpp`, beside the existing hints: request a debug context, gated to
Debug builds because a debug context costs performance.

```cpp
#ifdef CB_DEBUG
    glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GLFW_TRUE);
#endif
```

`OpenGLContext::Init`, after `gladLoadGLLoader` succeeds and after the existing
vendor/renderer/version logging: probe the function pointer, and install only if
it is there.

```cpp
if (glDebugMessageCallback != nullptr)
{
    glEnable(GL_DEBUG_OUTPUT);
    glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
    glDebugMessageCallback(GLDebugCallback, nullptr);
    glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DEBUG_SEVERITY_NOTIFICATION,
        0, nullptr, GL_FALSE);
}
else
{
    CB_CORE_WARN("GL debug output unavailable; driver does not expose KHR_debug");
}
```

**`GL_DEBUG_OUTPUT_SYNCHRONOUS` is the point of this, not a detail.** Without it the
callback may fire long after the call that caused it, on any thread, which makes the
message nearly useless. With it, the callback runs inside the offending GL call and
the stack above it names the culprit.

Severity maps onto the existing logger: `HIGH` to `CB_CORE_ERROR`, `MEDIUM` and
`LOW` to `CB_CORE_WARN`. `NOTIFICATION` is filtered out at the driver rather than in
the callback — drivers emit buffer-allocation chatter at that level constantly, and
a log nobody can read is a log nobody reads.

The probe is a runtime check rather than a version bump. Raising the requested
context to 4.3 would guarantee the feature but would also raise the engine's
hardware floor from 2010 to 2012 hardware, purely to obtain a debug facility that
nearly every driver already offers on 3.3 through `GL_KHR_debug`.

## Part 4 — Sandbox integration

The Sandbox draws a wireframe outline around the voxel the edit ray is pointing at
— the familiar block highlight — and flushes at the end of `OnRender`, after the
world and before the HUD overlay runs.

This target was chosen over the obvious one. Drawing the player's own AABB proves
nothing visually: the box is centred on the camera, so it is seen from inside and
renders as nothing. The block outline is visible in a still screenshot, is
obviously right or wrong at a glance, requires no keyboard input to trigger, and
exercises the raycast, `Box`, and the flush path together.

It reuses `VoxelRaycast::Cast` with the same arguments the editing path already
uses, so a divergence between what is highlighted and what a click would break is
itself a bug worth seeing.

## Testing

New `Tests/src/DebugLineBatchTests.cpp`. Adding a test file requires re-running
`premake5 vs2026` directly — **not** `GenerateProjects.bat`, which deletes `bin/`
and ends in a blocking `pause`. Create every new source file before the single
regeneration: premake expands its `files` globs at generation time, so a file
created afterwards is invisible to the build and the resulting link error is
indistinguishable from a genuine red state.

- An empty batch has no vertices.
- `AddLine` appends exactly two vertices, carrying the given endpoints and colour.
- `AddBox` appends exactly 24 vertices.
- Every `AddBox` vertex is a corner: each component equals either the min or the
  max for its axis.
- **Each of the 8 corners appears exactly 3 times** — the edge-table check.
- `AddBox` with `min == max` yields 24 identical vertices rather than being
  rejected.
- `AddBox` with `min > max` is drawn as given, not normalized.
- Two shapes accumulate rather than replacing one another.
- `Clear` empties the batch.

`DebugDraw`, the GL callback, and the Sandbox integration need a GL context and are
not covered by the suite, consistent with how `SandboxLayer` and `Application::Run`
are treated.

## Verification on screen

Build, run the Sandbox, screenshot, close via `WM_CLOSE`.

- A wireframe box outlines the block under the crosshair, aligned to the voxel grid
  rather than floating or offset by one.
- The outline is occluded by terrain in front of it, confirming depth testing.
- Looking at open sky draws no box.
- The log carries either GL debug messages or the single "unavailable" warning, and
  no GL errors during normal play.

## Out of scope

- Thick lines, which need quad expansion.
- A `Frustum` debug helper. Cubit's `Frustum` stores six planes, not corners
  (`Frustum.h:29`); recovering corners means intersecting plane triples, which is
  real math deserving its own tests and its own decision.
- Text rendering in 3D, and screen-space debug shapes.
- Any on/off toggle. Immediate mode means not calling it, and a key binding cannot
  be exercised by a screenshot script anyway.
- Replacing the Sandbox's inline shader strings with an asset layer.

## Amendments after implementation

The Part 3 argument above — probe on a 3.3 context rather than pay for a 4.3
one — turned out to be wrong, and not for a reason the driver had any say in.
GLAD in this project was generated without the `GL_KHR_debug` extension, so it
resolves `glDebugMessageCallback` only through `load_GL_VERSION_4_3`. On a 3.3
context that pointer is null unconditionally; no driver's `KHR_debug` support,
however complete, could ever have made the probe succeed. The "nearly every
driver already offers this on 3.3" claim was true of drivers and irrelevant to
GLAD. What shipped instead: Debug builds request a 4.3 context and Release
keeps 3.3, so the probe still runs — and now it means something. It succeeds
in Debug and fails in Release by construction, which is exactly the coverage
this spec wanted, just reached by raising the ceiling for the builds that can
afford it rather than raising the floor for everyone.

`DebugDraw` also picked up a `Shutdown()` this document's sketch omits.
`Application`'s destructor deletes the window, and with it the GL context,
before the process tears down its statics. Without `Shutdown()`, the
namespace-scope `unique_ptr`s in `DebugDraw.cpp` would release their GL
objects at static destruction — after the context is already gone. `Shutdown`
gives `Application` a place to release them while the context is still current,
the same reason it calls it explicitly rather than leaving it to DLL unload.
