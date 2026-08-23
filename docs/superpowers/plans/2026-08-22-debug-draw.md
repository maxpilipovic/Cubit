# Debug Line Rendering and GL Debug Output Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let any engine code draw a line or a wireframe box for one frame, and route OpenGL driver messages into the engine log.

**Architecture:** `DebugLineBatch` accumulates line geometry on the CPU with no GL dependency and is fully unit tested; `DebugDraw` wraps one static batch plus lazily created GPU resources and draws it with `glDrawArrays(GL_LINES, ...)`. Separately, `OpenGLContext::Init` probes `glDebugMessageCallback` and installs a synchronous callback that logs driver messages by severity. The Sandbox proves both by outlining the voxel under the crosshair.

**Tech Stack:** C++20, OpenGL 3.3 core via GLAD (generated through 4.6), GLFW, GLM, doctest, premake5 (vs2026).

**Spec:** `docs/superpowers/specs/2026-08-22-debug-draw-design.md`

## Global Constraints

- **Build (Tests):** `"C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" Tests/Tests.vcxproj -p:Configuration=Debug -p:Platform=x64` — FORWARD slashes in the `.vcxproj` path, or MSBuild fails with MSB1009.
- **Build (Sandbox):** same command with `Sandbox/Sandbox.vcxproj`.
- The suite runs as a post-build step, so its stdout appears in the build log. A failing test breaks the build. The suite currently has **243 cases**.
- Run a single case: `bin/Debug-windows-x86_64/Tests/Tests.exe -tc="<case name>"`.
- **Create every new source file BEFORE running `premake5 vs2026`, and regenerate once.** premake expands its `files` globs (`Cubit/src/**.cpp`, `Cubit/include/**.h`, `Tests/src/**.cpp`) at generation time, so a file created afterwards is invisible to the build — and the resulting link error is indistinguishable from a genuine TDD red state. Do NOT run `GenerateProjects.bat`: it deletes `bin/` and ends in a blocking `pause`.
- Every `.cpp` under `Cubit/src/` must begin with `#include "cub.h"` — it is the precompiled header (`pchheader "cub.h"`).
- Public engine classes carry `CB_API` (from `Cubit/Core.h`). A `CB_API` class with a `std::vector` or `std::array` member needs the MSVC 4251 warning pragma around it — see `Cubit/include/Cubit/Renderer/Frustum.h` for the established pattern.
- Comment style: `//` with NO space before the text for doc comments on declarations; `// ` WITH a space for prose inside function bodies.
- Logger macros: `CB_CORE_INFO`, `CB_CORE_WARN`, `CB_CORE_ERROR` (`Cubit/src/Core/CoreLogger.h`), and `CB_INFO` for the Sandbox.
- Commit after each task, with **one short imperative subject line, a blank line, then a short prose paragraph saying why**. Wrapped prose, not bullets. Push to `master` directly — no feature branches. **Never add a Co-Authored-By trailer or any assistant attribution.**

---

### Task 1: `DebugLineBatch`

The geometry, under test, before any GL exists to obscure it.

**Files:**
- Create: `Cubit/include/Cubit/Renderer/DebugLineBatch.h`
- Create: `Cubit/src/Renderer/DebugLineBatch.cpp`
- Test: `Tests/src/DebugLineBatchTests.cpp`

**Interfaces:**
- Consumes: `CB_API` from `Cubit/Core.h`, GLM.
- Produces: `struct DebugVertex { glm::vec3 Position; glm::vec4 Color; };` and `class DebugLineBatch` with `void AddLine(const glm::vec3&, const glm::vec3&, const glm::vec4&)`, `void AddBox(const glm::vec3&, const glm::vec3&, const glm::vec4&)`, `void Clear()`, `const std::vector<DebugVertex>& Vertices() const`.

- [ ] **Step 1: Create the header**

Create `Cubit/include/Cubit/Renderer/DebugLineBatch.h`:

```cpp
#pragma once

#include "Cubit/Core.h"

#include <glm/glm.hpp>

#include <vector>

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4251)
#endif

//One endpoint of a debug line segment.
struct DebugVertex
{
    glm::vec3 Position{ 0.0f };
    glm::vec4 Color{ 1.0f };
};

//Accumulates debug line geometry on the CPU.
//
//Holds no GPU resources and includes no OpenGL headers, so simulation code can
//build debug geometry without acquiring a rendering dependency.
class CB_API DebugLineBatch
{
public:
    //Appends one line segment as two vertices.
    void AddLine(const glm::vec3& from, const glm::vec3& to, const glm::vec4& color);

    //Appends a wireframe axis-aligned box as its twelve edges.
    //
    //Bounds are used as given: a min greater than a max draws the inverted box
    //rather than being silently corrected, because a debug tool that repairs its
    //own input hides the bug being looked for.
    void AddBox(const glm::vec3& min, const glm::vec3& max, const glm::vec4& color);

    //Discards everything accumulated so far.
    void Clear();

    //The accumulated vertices, in pairs, each pair one line segment.
    const std::vector<DebugVertex>& Vertices() const { return m_Vertices; }

private:
    std::vector<DebugVertex> m_Vertices;
};

#ifdef _MSC_VER
#pragma warning(pop)
#endif
```

- [ ] **Step 2: Create the implementation as a stub**

Create `Cubit/src/Renderer/DebugLineBatch.cpp` with the real signatures but empty bodies. This exists now, before the premake run, so one regeneration covers every new file — and it makes the red state failing assertions rather than a link error, which would look identical to a build misconfiguration:

```cpp
#include "cub.h"

#include "Cubit/Renderer/DebugLineBatch.h"

void DebugLineBatch::AddLine(const glm::vec3& from, const glm::vec3& to, const glm::vec4& color)
{
    (void)from;
    (void)to;
    (void)color;
}

void DebugLineBatch::AddBox(const glm::vec3& min, const glm::vec3& max, const glm::vec4& color)
{
    (void)min;
    (void)max;
    (void)color;
}

void DebugLineBatch::Clear()
{
}
```

- [ ] **Step 3: Create the test file**

Create `Tests/src/DebugLineBatchTests.cpp`:

```cpp
#include <doctest.h>

#include "Cubit/Renderer/DebugLineBatch.h"

#include <array>
#include <cstddef>

namespace
{
    const glm::vec4 Red{ 1.0f, 0.0f, 0.0f, 1.0f };

    // Exactly representable in binary floating point, so the corner comparisons
    // below can use == without tolerance.
    const glm::vec3 BoxMin{ 1.0f, 2.0f, 3.0f };
    const glm::vec3 BoxMax{ 4.0f, 6.0f, 9.0f };
}

TEST_CASE("A new batch holds nothing")
{
    const DebugLineBatch batch;

    CHECK(batch.Vertices().empty());
}

TEST_CASE("A line appends its two endpoints")
{
    DebugLineBatch batch;
    batch.AddLine(glm::vec3(1.0f, 2.0f, 3.0f), glm::vec3(4.0f, 5.0f, 6.0f), Red);

    REQUIRE(batch.Vertices().size() == 2);
    CHECK(batch.Vertices()[0].Position == glm::vec3(1.0f, 2.0f, 3.0f));
    CHECK(batch.Vertices()[1].Position == glm::vec3(4.0f, 5.0f, 6.0f));
    CHECK(batch.Vertices()[0].Color == Red);
    CHECK(batch.Vertices()[1].Color == Red);
}

TEST_CASE("A box appends twelve edges")
{
    DebugLineBatch batch;
    batch.AddBox(BoxMin, BoxMax, Red);

    CHECK(batch.Vertices().size() == 24);
}

TEST_CASE("Every box vertex sits on a corner")
{
    DebugLineBatch batch;
    batch.AddBox(BoxMin, BoxMax, Red);

    for (const DebugVertex& vertex : batch.Vertices())
    {
        CHECK((vertex.Position.x == BoxMin.x || vertex.Position.x == BoxMax.x));
        CHECK((vertex.Position.y == BoxMin.y || vertex.Position.y == BoxMax.y));
        CHECK((vertex.Position.z == BoxMin.z || vertex.Position.z == BoxMax.z));
    }
}

TEST_CASE("Every box corner carries three edges")
{
    // The check that can actually fail. A transposed, duplicated or omitted edge
    // still yields 24 vertices and still lands every vertex on a corner, but it
    // breaks this distribution: each of the eight corners has exactly three
    // edges meeting at it.
    DebugLineBatch batch;
    batch.AddBox(BoxMin, BoxMax, Red);

    std::array<int, 8> counts{};
    for (const DebugVertex& vertex : batch.Vertices())
    {
        const std::size_t corner =
            (vertex.Position.x == BoxMax.x ? 1u : 0u) |
            (vertex.Position.y == BoxMax.y ? 2u : 0u) |
            (vertex.Position.z == BoxMax.z ? 4u : 0u);
        ++counts[corner];
    }

    for (int count : counts)
        CHECK(count == 3);
}

TEST_CASE("A degenerate box is drawn rather than rejected")
{
    DebugLineBatch batch;
    batch.AddBox(BoxMin, BoxMin, Red);

    REQUIRE(batch.Vertices().size() == 24);
    for (const DebugVertex& vertex : batch.Vertices())
        CHECK(vertex.Position == BoxMin);
}

TEST_CASE("An inverted box is drawn as given rather than corrected")
{
    // A debug tool that silently repairs its input hides the bug being hunted.
    DebugLineBatch batch;
    batch.AddBox(BoxMax, BoxMin, Red);

    REQUIRE(batch.Vertices().size() == 24);

    bool sawInvertedCorner = false;
    for (const DebugVertex& vertex : batch.Vertices())
        if (vertex.Position == BoxMax)
            sawInvertedCorner = true;

    CHECK(sawInvertedCorner);
}

TEST_CASE("Shapes accumulate rather than replacing one another")
{
    DebugLineBatch batch;
    batch.AddLine(BoxMin, BoxMax, Red);
    batch.AddBox(BoxMin, BoxMax, Red);

    CHECK(batch.Vertices().size() == 26);
}

TEST_CASE("Clear empties the batch")
{
    DebugLineBatch batch;
    batch.AddBox(BoxMin, BoxMax, Red);
    batch.Clear();

    CHECK(batch.Vertices().empty());
}
```

- [ ] **Step 4: Regenerate the projects**

Run from the repo root: `premake5 vs2026`

Expected: premake prints the generated projects and exits 0. All three new files existed before this ran.

- [ ] **Step 5: Build and confirm the tests fail**

Run: `"C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" Tests/Tests.vcxproj -p:Configuration=Debug -p:Platform=x64`

Expected: the build compiles, then the post-build test run FAILS. "A new batch holds nothing" and "Clear empties the batch" pass against the stub (an always-empty batch satisfies both); the other seven fail. Record the actual output.

- [ ] **Step 6: Write the implementation**

Replace the three stub bodies in `Cubit/src/Renderer/DebugLineBatch.cpp`:

```cpp
void DebugLineBatch::AddLine(const glm::vec3& from, const glm::vec3& to, const glm::vec4& color)
{
    m_Vertices.push_back(DebugVertex{ from, color });
    m_Vertices.push_back(DebugVertex{ to, color });
}

void DebugLineBatch::AddBox(const glm::vec3& min, const glm::vec3& max, const glm::vec4& color)
{
    // Corners are indexed so bit 0 is x, bit 1 is y and bit 2 is z. Two corners
    // share an edge exactly when their indices differ in a single bit, which is
    // what the edge table below encodes — and why every corner ends up with
    // three edges.
    const glm::vec3 corners[8] = {
        { min.x, min.y, min.z },
        { max.x, min.y, min.z },
        { min.x, max.y, min.z },
        { max.x, max.y, min.z },
        { min.x, min.y, max.z },
        { max.x, min.y, max.z },
        { min.x, max.y, max.z },
        { max.x, max.y, max.z },
    };

    static constexpr int Edges[12][2] = {
        { 0, 1 }, { 2, 3 }, { 4, 5 }, { 6, 7 },
        { 0, 2 }, { 1, 3 }, { 4, 6 }, { 5, 7 },
        { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 },
    };

    for (const auto& edge : Edges)
        AddLine(corners[edge[0]], corners[edge[1]], color);
}

void DebugLineBatch::Clear()
{
    m_Vertices.clear();
}
```

- [ ] **Step 7: Build and confirm the tests pass**

Run the Tests build command again.

Expected: green, 252 cases (243 + 9).

- [ ] **Step 8: Prove the corner test can fail**

Temporarily change one edge in the table — replace `{ 3, 7 }` with `{ 0, 1 }`, duplicating an edge and dropping another. Rebuild.

Expected: "Every box corner carries three edges" FAILS while "A box appends twelve edges" and "Every box vertex sits on a corner" still PASS — proving the corner test catches what the other two cannot. Restore `{ 3, 7 }` and rebuild to green. Record both outputs; a test that cannot fail is worse than no test.

- [ ] **Step 9: Commit**

```bash
git add Cubit/include/Cubit/Renderer/DebugLineBatch.h Cubit/src/Renderer/DebugLineBatch.cpp Tests/src/DebugLineBatchTests.cpp
```

Commit with subject `Add a CPU batch for debug line geometry` and a prose body explaining that the engine has no way to draw a line, that this is the geometry half kept free of GL so it can be tested, and that the edge table is verified by corner degree rather than vertex count.

---

### Task 2: OpenGL debug output

Independent of the rest, and deliberately before `DebugDraw`: this is the instrument that reports GL errors, and the next task is the one that might cause some.

**Files:**
- Modify: `Cubit/src/Platform/Windows/WindowsWindow.cpp` (beside the existing `glfwWindowHint` calls, around lines 43-45)
- Modify: `Cubit/src/Platform/OpenGL/OpenGLContext.cpp` (`OpenGLContext::Init`)

**Interfaces:**
- Consumes: nothing from earlier tasks.
- Produces: nothing later tasks call. Behaviour only.

No unit test: this needs a live GL context, which the suite has no way to create. It is verified by running the Sandbox and reading the log.

- [ ] **Step 1: Request a debug context in Debug builds**

In `Cubit/src/Platform/Windows/WindowsWindow.cpp`, immediately after the existing `glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);`:

```cpp
    // A debug context is what lets the driver report through the callback
    // installed in OpenGLContext::Init. It costs performance, so only Debug
    // builds ask for one.
#ifdef CB_DEBUG
    glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GLFW_TRUE);
#endif
```

- [ ] **Step 2: Add the callback**

In `Cubit/src/Platform/OpenGL/OpenGLContext.cpp`, add an anonymous namespace above `OpenGLContext::OpenGLContext`:

```cpp
namespace
{
    //Routes a driver message into the engine log, mapping GL severity onto the
    //logger's levels.
    void APIENTRY GLDebugCallback(
        GLenum source,
        GLenum type,
        GLuint id,
        GLenum severity,
        GLsizei length,
        const GLchar* message,
        const void* userParameter)
    {
        (void)source;
        (void)type;
        (void)id;
        (void)length;
        (void)userParameter;

        const std::string text = std::string("OpenGL: ") + message;

        if (severity == GL_DEBUG_SEVERITY_HIGH)
        {
            CB_CORE_ERROR(text);
            return;
        }

        CB_CORE_WARN(text);
    }
}
```

`APIENTRY` is defined by `glad.h`, which this file already includes. `<string>` arrives through the precompiled header, which this file already relies on for the `std::string` concatenation in the existing logging.

- [ ] **Step 3: Install it**

In `OpenGLContext::Init`, after the three existing `CB_CORE_INFO` vendor/renderer/version lines:

```cpp
    // Without this a GL error produces no output at all and surfaces as wrong
    // pixels, which is the most expensive way to find out. GL_DEBUG_OUTPUT is
    // core only in 4.3 and this context is 3.3, so the entry point is probed
    // rather than assumed; nearly every driver offers it through GL_KHR_debug.
    if (glDebugMessageCallback != nullptr)
    {
        glEnable(GL_DEBUG_OUTPUT);

        // Synchronous is the point of the feature, not a detail: the callback
        // then runs inside the offending GL call, so the stack above it names
        // the culprit. Asynchronously the message can arrive arbitrarily later,
        // on any thread, which makes it nearly useless.
        glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
        glDebugMessageCallback(GLDebugCallback, nullptr);

        // Drivers emit allocation chatter at notification level constantly, and
        // a log nobody can read is a log nobody reads.
        glDebugMessageControl(
            GL_DONT_CARE,
            GL_DONT_CARE,
            GL_DEBUG_SEVERITY_NOTIFICATION,
            0,
            nullptr,
            GL_FALSE);

        CB_CORE_INFO("OpenGL debug output enabled");
    }
    else
    {
        CB_CORE_WARN("OpenGL debug output unavailable: no KHR_debug entry point");
    }
```

- [ ] **Step 4: Build both projects**

Run the Tests build command, then the Sandbox build command.

Expected: both succeed. Tests stays at 252 — this task adds none.

- [ ] **Step 5: Run the Sandbox and read the log**

Launch from `bin/Debug-windows-x86_64/Sandbox` — the map path is relative and a load failure throws out of the constructor, producing an "abort() has been called" dialog over a blank white window that looks exactly like a render bug. Run detached with stdout redirected to a file; the logger writes to `std::cout`, which is fully buffered when redirected, so force-killing the process discards every line. Allow ~45 s, then close with `PostMessage(hwnd, WM_CLOSE, 0, 0)` — do NOT `Stop-Process`.

Expected in the log: either `OpenGL debug output enabled` or the `unavailable` warning. Report which, plus any `OpenGL:` messages that appeared. Driver warnings appearing here are a *finding to report*, not a failure of this task — they were always happening and were simply invisible before.

- [ ] **Step 6: Commit**

```bash
git add Cubit/src/Platform/Windows/WindowsWindow.cpp Cubit/src/Platform/OpenGL/OpenGLContext.cpp
```

Commit with subject `Report OpenGL driver messages through the engine log` and a prose body explaining that a GL error was previously silent, that the entry point is probed because the context is 3.3 rather than 4.3, and why synchronous delivery matters.

---

### Task 3: `DebugDraw`

**Files:**
- Create: `Cubit/include/Cubit/Renderer/DebugDraw.h`
- Create: `Cubit/src/Renderer/DebugDraw.cpp`
- Modify: `Cubit/include/Cubit/Renderer/VertexBuffer.h` (add one constructor declaration)
- Modify: `Cubit/src/Renderer/VertexBuffer.cpp` (add its definition)
- Modify: `Cubit/include/Cubit/Cubit.h` (export the two new headers)
- Modify: `Cubit/src/Application.cpp` (`Application::~Application`)

**Interfaces:**
- Consumes: `DebugLineBatch`, `DebugVertex` from Task 1. `Camera::GetViewProjectionMatrix()`, `Shader::SetMat4/Bind`, `VertexArray::AddBuffer/Bind`, `VertexBuffer::SetData`, `BufferLayout`, `ShaderDataType`.
- Produces: `DebugDraw::Line`, `DebugDraw::Box`, `DebugDraw::Flush(const Camera&, const glm::mat4& = identity)`, `DebugDraw::Clear`, `DebugDraw::Shutdown`. Task 4 calls `Box` and `Flush`.

No unit test: every path here needs a GL context. Verified on screen in Task 4.

- [ ] **Step 1: Add a dynamic vertex buffer constructor**

In `Cubit/include/Cubit/Renderer/VertexBuffer.h`, after the existing constructor:

```cpp
    //Creates empty GPU storage sized for vertex data that is rewritten every
    //frame.
    explicit VertexBuffer(std::uint32_t size);
```

In `Cubit/src/Renderer/VertexBuffer.cpp`, after the existing constructor:

```cpp
VertexBuffer::VertexBuffer(std::uint32_t size)
{
    glGenBuffers(1, &m_RendererId);
    glBindBuffer(GL_ARRAY_BUFFER, m_RendererId);
    glBufferData(GL_ARRAY_BUFFER, size, nullptr, GL_DYNAMIC_DRAW);
}
```

- [ ] **Step 2: Create the header**

Create `Cubit/include/Cubit/Renderer/DebugDraw.h`:

```cpp
#pragma once

#include "Cubit/Core.h"

#include <glm/glm.hpp>

class Camera;

//Draws line geometry for one frame, callable from anywhere in the engine.
//
//Immediate mode: a shape is submitted on every frame it should be visible, and
//Flush both draws and discards the frame's batch. There are no handles and
//nothing to remove.
//
//Static rather than an instance because the point is to look at engine
//internals, and code like SpawnFinder or VoxelCollision holds no renderer
//reference to plumb one through. This header pulls in no OpenGL, so such code
//may call it without acquiring a rendering dependency — but a build that never
//calls Flush accumulates without bound, which is what Clear is for.
class CB_API DebugDraw
{
public:
    //Prevents instances of the static debug drawer.
    DebugDraw() = delete;

    //Queues one line segment for this frame.
    static void Line(const glm::vec3& from, const glm::vec3& to, const glm::vec4& color);

    //Queues a wireframe axis-aligned box for this frame.
    static void Box(const glm::vec3& min, const glm::vec3& max, const glm::vec4& color);

    //Draws everything queued this frame and then discards it.
    //
    //Takes a camera rather than reusing the renderer's current scene: overlays
    //render last and leave an orthographic matrix behind, so an automatic
    //end-of-frame flush would draw the world's debug lines in screen space.
    static void Flush(const Camera& camera, const glm::mat4& transform = glm::mat4(1.0f));

    //Discards everything queued without drawing it.
    static void Clear();

    //Releases the GPU resources. Must run before the OpenGL context is
    //destroyed, which is why the Application calls it rather than leaving it to
    //static destruction at DLL unload.
    static void Shutdown();
};
```

- [ ] **Step 3: Create the implementation**

Create `Cubit/src/Renderer/DebugDraw.cpp`:

```cpp
#include "cub.h"

#include "Cubit/Renderer/DebugDraw.h"

#include "Cubit/Renderer/Camera.h"
#include "Cubit/Renderer/DebugLineBatch.h"
#include "Cubit/Renderer/Shader.h"
#include "Cubit/Renderer/VertexArray.h"
#include "Cubit/Renderer/VertexBuffer.h"

#include <glad/glad.h>

#include <memory>

namespace
{
    DebugLineBatch s_Batch;

    std::unique_ptr<Shader> s_Shader;
    std::unique_ptr<VertexArray> s_VertexArray;
    std::unique_ptr<VertexBuffer> s_VertexBuffer;
    std::uint32_t s_Capacity = 0;

    constexpr std::string_view VertexSource = R"(
        #version 330 core
        layout(location = 0) in vec3 a_Position;
        layout(location = 1) in vec4 a_Color;

        uniform mat4 u_ViewProjection;
        uniform mat4 u_Transform;

        out vec4 v_Color;

        void main()
        {
            v_Color = a_Color;
            gl_Position = u_ViewProjection * u_Transform * vec4(a_Position, 1.0);
        }
    )";

    constexpr std::string_view FragmentSource = R"(
        #version 330 core
        layout(location = 0) out vec4 color;

        in vec4 v_Color;

        void main()
        {
            color = v_Color;
        }
    )";

    //Creates the GPU resources on first use and grows the vertex buffer when a
    //frame needs more room than any previous frame did. Lazy because a debug
    //drawer nothing draws through should cost no GPU memory, and because this
    //runs long after the context exists.
    void EnsureCapacity(std::uint32_t vertexCount)
    {
        if (s_Shader == nullptr)
            s_Shader = std::make_unique<Shader>(VertexSource, FragmentSource);

        if (s_VertexBuffer != nullptr && vertexCount <= s_Capacity)
            return;

        s_Capacity = vertexCount;
        s_VertexBuffer = std::make_unique<VertexBuffer>(
            static_cast<std::uint32_t>(s_Capacity * sizeof(DebugVertex)));

        // The array records the buffer it was configured against, so it is
        // rebuilt alongside it rather than left pointing at the freed one.
        s_VertexArray = std::make_unique<VertexArray>();
        s_VertexArray->AddBuffer(
            *s_VertexBuffer,
            BufferLayout{
                { ShaderDataType::Float3 },
                { ShaderDataType::Float4 }
            });
    }
}

void DebugDraw::Line(const glm::vec3& from, const glm::vec3& to, const glm::vec4& color)
{
    s_Batch.AddLine(from, to, color);
}

void DebugDraw::Box(const glm::vec3& min, const glm::vec3& max, const glm::vec4& color)
{
    s_Batch.AddBox(min, max, color);
}

void DebugDraw::Flush(const Camera& camera, const glm::mat4& transform)
{
    const std::vector<DebugVertex>& vertices = s_Batch.Vertices();

    // The common case once the engine is littered with DebugDraw calls is a
    // frame drawing none of them. That must cost a branch, not a bind.
    if (vertices.empty())
        return;

    EnsureCapacity(static_cast<std::uint32_t>(vertices.size()));

    s_VertexBuffer->SetData(
        vertices.data(),
        static_cast<std::uint32_t>(vertices.size() * sizeof(DebugVertex)));

    s_Shader->Bind();
    s_Shader->SetMat4("u_ViewProjection", camera.GetViewProjectionMatrix());
    s_Shader->SetMat4("u_Transform", transform);

    s_VertexArray->Bind();
    glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(vertices.size()));

    s_Batch.Clear();
}

void DebugDraw::Clear()
{
    s_Batch.Clear();
}

void DebugDraw::Shutdown()
{
    s_VertexArray.reset();
    s_VertexBuffer.reset();
    s_Shader.reset();
    s_Capacity = 0;
    s_Batch.Clear();
}
```

- [ ] **Step 4: Release the resources before the context dies**

`Application::~Application` calls `delete m_Data`, which destroys the `Window` and with it the GL context. Static `unique_ptr`s at namespace scope would instead run at DLL unload, deleting GL objects against a context that no longer exists.

In `Cubit/src/Application.cpp`, add the include beside the existing ones:

```cpp
#include "Cubit/Renderer/DebugDraw.h"
```

and make `Application::~Application` begin:

```cpp
Application::~Application()
{
    // Before delete m_Data, which destroys the window and with it the GL
    // context these resources belong to.
    DebugDraw::Shutdown();

    Input::SetWindow(nullptr);
    delete m_Data;
```

- [ ] **Step 5: Export the new headers**

In `Cubit/include/Cubit/Cubit.h`, add both alongside the other renderer headers, matching the file's existing one-include-per-line formatting:

```cpp
#include "Cubit/Renderer/DebugDraw.h"
#include "Cubit/Renderer/DebugLineBatch.h"
```

- [ ] **Step 6: Regenerate and build both projects**

Run `premake5 vs2026` — `Cubit/src/Renderer/DebugDraw.cpp` and its header are new. Then run the Tests build command, then the Sandbox build command.

Expected: both succeed, tests stay at 252. Nothing calls `DebugDraw` yet, so no visible change.

- [ ] **Step 7: Commit**

```bash
git add Cubit/include/Cubit/Renderer/DebugDraw.h Cubit/src/Renderer/DebugDraw.cpp Cubit/include/Cubit/Renderer/VertexBuffer.h Cubit/src/Renderer/VertexBuffer.cpp Cubit/include/Cubit/Cubit.h Cubit/src/Application.cpp
```

Commit with subject `Draw queued debug lines through a static drawer` and a prose body explaining that the drawer is static so simulation code can reach it without a renderer reference, that the flush takes a camera because overlays leave an orthographic matrix in the renderer's current scene, and that Shutdown exists because static destruction runs after the context is gone.

---

### Task 4: Outline the targeted block, and verify

An unused debug renderer is an unverified one.

**Files:**
- Modify: `Sandbox/src/Sandbox.cpp` (`OnRender`, and a new helper beside `OnMouseButtonPressed`)
- Modify: `docs/engine-roadmap.md`

**Interfaces:**
- Consumes: `DebugDraw::Box`, `DebugDraw::Flush` from Task 3. `VoxelRaycast::Cast`, `VoxelRayHit` (`Cubit/Voxel/VoxelRaycast.h`).
- Produces: nothing.

- [ ] **Step 1: Add the outline helper**

In `Sandbox/src/Sandbox.cpp`, beside `OnMouseButtonPressed`, add:

```cpp
    //Outlines the block a click would break, using the same ray the edit uses so
    //a disagreement between what is highlighted and what an edit hits is itself
    //visible.
    void DrawTargetedBlockOutline()
    {
        const PerspectiveCamera& camera = m_CameraController.GetCamera();
        const VoxelRayHit hit = VoxelRaycast::Cast(
            m_World,
            camera.GetPosition() - WorldOffset,
            camera.GetForwardDirection(),
            ReachDistance,
            true);

        if (!hit.Hit)
            return;

        // Nudged outward a hair so the outline is not z-fighting with the block
        // face it traces.
        constexpr float Swell = 0.002f;
        const glm::vec3 min = glm::vec3(hit.Block) - glm::vec3(Swell);
        const glm::vec3 max = min + glm::vec3(1.0f + Swell * 2.0f);

        DebugDraw::Box(min, max, OutlineColor);
    }
```

Add the colour to the anonymous namespace at the top of the file, beside `WorldOffset` and `ReachDistance`:

```cpp
    //Near-black, so the outline reads against both lit terrain and sky.
    const glm::vec4 OutlineColor{ 0.05f, 0.05f, 0.05f, 1.0f };
```

- [ ] **Step 2: Call it and flush**

In `SandboxLayer::OnRender`, after the existing `Renderer::EndScene();` that closes the world draw and before the `m_HudState->MeshFaceCount = ...` bookkeeping:

```cpp
        // Flushed here, while the world camera is current. The HUD overlay
        // renders after this layer and leaves an orthographic matrix behind, so
        // a later flush would draw these lines in screen space.
        DrawTargetedBlockOutline();
        DebugDraw::Flush(m_CameraController.GetCamera(), glm::translate(glm::mat4(1.0f), WorldOffset));
```

The transform carries `WorldOffset` for the same reason the world shader's `u_Transform` does: the outline is computed in world coordinates while the camera lives in world-plus-offset space. `glm::translate` needs `<glm/gtc/matrix_transform.hpp>`, which `Sandbox.cpp:9` already includes — no include change is required.

The insertion point is `Sandbox/src/Sandbox.cpp:269-271`: after `Renderer::EndScene();` and before `m_HudState->MeshFaceCount = ...`. Line numbers shift as earlier steps edit the file, so locate it by content.

- [ ] **Step 3: Build the Sandbox**

Run the Sandbox build command. Expected: success. No premake regeneration is needed — this task creates no files.

- [ ] **Step 4: Run it and look at the frame**

Launch from `bin/Debug-windows-x86_64/Sandbox`, detached, stdout redirected to a file. Allow ~45 s for the 512×64×512 map to load and mesh — capturing early catches the world mid-build, which looks like missing geometry and is not.

Screenshot the **GLFW30** window, not the process's `MainWindowHandle`, which returns the black console: use `EnumWindows` + `GetWindowThreadProcessId` for the pid and pick the handle whose class is not `*ConsoleWindow*`. Minimise the console with `ShowWindow(h, 6)` and set the GL window topmost via `SetWindowPos(h, (IntPtr)(-1), 0, 0, 0, 0, 0x0003)`. Capture with `Graphics.CopyFromScreen`, then close with `PostMessage(hwnd, WM_CLOSE, 0, 0)`.

Read the screenshot with the Read tool and check:
- A wireframe box outlines the block under the crosshair, aligned to the voxel grid rather than floating or offset by one.
- Its far edges are hidden by the block itself where they should be, confirming depth testing.
- The lines do not shimmer or z-fight against the block face.

Also report the log tail, including whether `OpenGL debug output enabled` appeared and whether any `OpenGL:` messages were logged once real drawing began.

Note: the camera aims wherever the mouse last left it, and ambient cursor motion reaches the app, so what is under the crosshair is not predictable — if the shot happens to look at open sky, no box is correct behaviour. Re-capture rather than concluding it is broken.

- [ ] **Step 5: Update the roadmap**

In `docs/engine-roadmap.md`, under "Beyond the voxel engine — gaps found 2026-08-20" → "Worth doing before gameplay", mark item 2 (**"Debug line and box rendering, plus a `KHR_debug` callback"**) done in the style the arc items use: strike the heading, append **DONE 2026-08-22**, then prose.

Match the document's voice — flowing prose with em-dashes, candid about what turned out differently. Cover: `DebugLineBatch` tested by corner degree rather than vertex count; `DebugDraw` static so simulation code can reach it; why the flush is explicit; that the callback is probed rather than requiring a 4.3 context; and that the Sandbox proves it by outlining the targeted block. Note that a `Frustum` debug helper stayed out of scope because `Frustum` stores planes rather than corners.

Do not mark item 3 (`BlockEdit`) done.

- [ ] **Step 6: Commit**

```bash
git add Sandbox/src/Sandbox.cpp docs/engine-roadmap.md
```

Commit with subject `Outline the block the edit ray is aimed at` and a prose body explaining that this is what proves the debug drawer works — chosen over the player's own AABB, which is centred on the camera and so renders as nothing from inside.
