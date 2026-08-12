# Multi-Model Stitching Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Load and save `.vox` maps larger than a single 256³ model, so Cubit can ship a 512×64×512 battlefield.

**Architecture:** The `.vox` scene graph stays inside the loader and writer. `VoxLoader::Parse` collects every `SIZE`/`XYZI` pair plus the `nTRN`/`nGRP`/`nSHP` nodes, resolves each model's origin by walking the graph, and flattens everything into one dense `VoxModel`. `VoxWriter::Write` does the inverse: a model over 256 on any axis is cut into a uniform 256 grid of tiles and placed by a generated graph. `BuildWorld`, `ToVoxModel`, `TerrainGen` and the Sandbox keep their current signatures.

**Tech Stack:** C++20, GLM, doctest, premake5 + MSBuild (Visual Studio 18), OpenGL 3.3.

Design spec: [`docs/superpowers/specs/2026-08-10-multi-model-stitching-design.md`](../specs/2026-08-10-multi-model-stitching-design.md)

## Global Constraints

- **Build (also runs the test suite as a post-build step):**
  `"C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" Tests/Tests.vcxproj -p:Configuration=Debug -p:Platform=x64`
  Use **forward slashes** in the `.vcxproj` path — backslashes fail with MSB1009.
- **Run one test case:** `./bin/Debug-windows-x86_64/Tests/Tests.exe -tc="<case name>"`
- **Run the whole suite:** `./bin/Debug-windows-x86_64/Tests/Tests.exe`
- Do **not** run `GenerateProjects.bat` — it deletes `bin/` and ends in a blocking `pause`. **No task here adds a source file**, so premake never needs re-running. New helpers go in the existing anonymous namespaces of `VoxLoader.cpp` / `VoxWriter.cpp`; new tests go in the existing `Tests/src/VoxLoaderTests.cpp`, `VoxWriterTests.cpp` and `WorldSaveTests.cpp`.
- Comment style: `//` with no space before the text, sentences explaining *why*, matching surrounding code.
- Commit after every task. **Do not add Claude co-author trailers or attribution.**
- Axis convention throughout: `cubit(x, y, z) = vox(x, z, y)`. This applies to translations as well as voxels.
- `MaxDimension = 256` continues to bound a **single model**. It never bounds the world again.
- The writer's output for a model that fits in 256 on every axis must stay **byte-identical** to today's.
- Version stays `150` in written files. MagicaVoxel ships scene graphs in 150 files, and changing it would break the byte-identical requirement above.

---

### Task 1: Collect models into a list and flatten them

Today `Parse` keeps one `voxSize` and appends every `XYZI` into one `rawVoxels` list, so a two-model file silently loads as one model of the wrong size with both models' voxels mixed in. This task makes models a list and flattens them — all at the origin, since no scene graph is read yet. It fixes real corruption on its own.

**Files:**
- Modify: `Cubit/src/Voxel/VoxLoader.cpp:53-158`
- Test: `Tests/src/VoxLoaderTests.cpp:29-81` (helper rework), plus new cases at end of file

**Interfaces:**
- Consumes: nothing.
- Produces: in `VoxLoader.cpp`'s anonymous namespace — `struct RawVoxel { std::uint8_t x, y, z, colorIndex; }` and `struct RawModel { glm::ivec3 VoxSize; std::vector<RawVoxel> Voxels; }`. In the tests — `struct Vox`, `struct ModelSpec`, `PushModel`, `PushPalette`, `MakeVoxFile`, and `MakeVoxBytes` with its existing signature preserved.

- [x] **Step 1: Rework the test helpers so a file can hold several models**

Replace lines 27-81 of `Tests/src/VoxLoaderTests.cpp` (the `struct Vox` and `MakeVoxBytes` block) with:

```cpp
    struct Vox { std::uint8_t x, y, z, colorIndex; };

    //One model's SIZE/XYZI pair, in vox axes.
    struct ModelSpec
    {
        std::int32_t sx, sy, sz;
        std::vector<Vox> Voxels;
    };

    //Appends a SIZE/XYZI pair. A file's Nth pair is model id N, which is what
    //the scene graph's shape nodes refer to.
    void PushModel(std::vector<std::uint8_t>& out, const ModelSpec& m)
    {
        PushTag(out, "SIZE");
        PushInt(out, 12);
        PushInt(out, 0);
        PushInt(out, m.sx);
        PushInt(out, m.sy);
        PushInt(out, m.sz);

        PushTag(out, "XYZI");
        PushInt(out, 4 + 4 * static_cast<std::int32_t>(m.Voxels.size()));
        PushInt(out, 0);
        PushInt(out, static_cast<std::int32_t>(m.Voxels.size()));
        for (const Vox& v : m.Voxels)
        {
            out.push_back(v.x);
            out.push_back(v.y);
            out.push_back(v.z);
            out.push_back(v.colorIndex);
        }
    }

    //Appends the palette these tests assert against: RGBA entry j is colour
    //index j + 1, coloured (j+1, j+1, j+1).
    void PushPalette(std::vector<std::uint8_t>& out)
    {
        PushTag(out, "RGBA");
        PushInt(out, 256 * 4);
        PushInt(out, 0);
        for (int j = 0; j < 256; ++j)
        {
            const std::uint8_t c = static_cast<std::uint8_t>(j + 1);
            out.push_back(c);   // r
            out.push_back(c);   // g
            out.push_back(c);   // b
            out.push_back(255); // a
        }
    }

    //Wraps MAIN around already-encoded child chunks.
    std::vector<std::uint8_t> MakeVoxFile(const std::vector<std::uint8_t>& children)
    {
        std::vector<std::uint8_t> bytes;
        PushTag(bytes, "VOX ");
        PushInt(bytes, 150);
        PushTag(bytes, "MAIN");
        PushInt(bytes, 0);
        PushInt(bytes, static_cast<std::int32_t>(children.size()));
        bytes.insert(bytes.end(), children.begin(), children.end());
        return bytes;
    }

    //Builds a minimal single-model .vox byte buffer. Kept for the cases written
    //before files could hold more than one model; the bytes are unchanged.
    std::vector<std::uint8_t> MakeVoxBytes(
        std::int32_t sx, std::int32_t sy, std::int32_t sz,
        const std::vector<Vox>& voxels)
    {
        std::vector<std::uint8_t> children;
        PushModel(children, ModelSpec{ sx, sy, sz, voxels });
        PushPalette(children);
        return MakeVoxFile(children);
    }
```

Add `#include <string>` and `#include <utility>` to the file's include block.

- [x] **Step 2: Build and confirm the existing suite still passes**

Run: `"C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" Tests/Tests.vcxproj -p:Configuration=Debug -p:Platform=x64`
Expected: PASS. The helper rework emits the same bytes in the same order, so this is a refactor with no behaviour change. If anything fails, the helpers are wrong — fix before continuing.

- [x] **Step 3: Write the failing test**

Append to `Tests/src/VoxLoaderTests.cpp`:

```cpp
TEST_CASE("A file with two models sized to the larger of them")
{
    // The larger model comes FIRST, so the world cannot be taken from the last
    // SIZE chunk. Order matters here: with the models the other way round, code
    // that simply keeps the last SIZE would agree with the right answer by luck
    // and this case could not fail.
    std::vector<std::uint8_t> children;
    PushModel(children, ModelSpec{ 4, 4, 4, { { 3, 3, 3, 2 } } });
    PushModel(children, ModelSpec{ 2, 2, 2, { { 0, 0, 0, 1 } } });
    PushPalette(children);

    const VoxModel model = VoxLoader::Parse(MakeVoxFile(children));

    CHECK(model.Size == glm::ivec3(4, 4, 4));
}

TEST_CASE("A model is not clipped by a smaller model declared after it")
{
    // The first model's far corner lies outside the second model's bounds, so
    // code that sizes the world from the last SIZE chunk drops that voxel
    // entirely rather than reporting it.
    std::vector<std::uint8_t> children;
    PushModel(children, ModelSpec{ 4, 4, 4, { { 3, 3, 3, 2 } } });
    PushModel(children, ModelSpec{ 2, 2, 2, { { 0, 0, 0, 1 } } });
    PushPalette(children);

    const VoxModel model = VoxLoader::Parse(MakeVoxFile(children));

    CHECK(model.At(3, 3, 3) == 2);
    CHECK(model.At(0, 0, 0) == 1);
}

TEST_CASE("Both models' voxels survive the flatten")
{
    std::vector<std::uint8_t> children;
    PushModel(children, ModelSpec{ 4, 4, 4, { { 0, 0, 0, 1 } } });
    PushModel(children, ModelSpec{ 4, 4, 4, { { 3, 2, 1, 2 } } });
    PushPalette(children);

    const VoxModel model = VoxLoader::Parse(MakeVoxFile(children));

    CHECK(model.At(0, 0, 0) == 1);
    CHECK(model.At(3, 1, 2) == 2); // vox (3,2,1) -> cubit (3,1,2)
}

TEST_CASE("A later model overwrites an earlier one in the same cell")
{
    std::vector<std::uint8_t> children;
    PushModel(children, ModelSpec{ 2, 2, 2, { { 1, 1, 1, 4 } } });
    PushModel(children, ModelSpec{ 2, 2, 2, { { 1, 1, 1, 9 } } });
    PushPalette(children);

    const VoxModel model = VoxLoader::Parse(MakeVoxFile(children));

    CHECK(model.At(1, 1, 1) == 9);
}
```

- [x] **Step 4: Run to verify it fails**

Run: `./bin/Debug-windows-x86_64/Tests/Tests.exe -tc="A file with two models sized to the larger of them"`
Expected: FAIL — reports `(2,2,2)`, taken from the last `SIZE` chunk, instead of `(4,4,4)`.

Run: `./bin/Debug-windows-x86_64/Tests/Tests.exe -tc="A model is not clipped by a smaller model declared after it"`
Expected: FAIL — the voxel at `(3,3,3)` is outside the wrongly-sized world and is dropped by the loader's out-of-bounds guard.

The other three cases (`Both models' voxels survive the flatten`, `A later model overwrites an earlier one in the same cell`, and the size case's sibling) **pass before the fix and that is expected** — they use models of equal size, where last-`SIZE`-wins coincides with the right answer. They are regression guards, not red tests. The two cases above are the ones that must be red; if either passes, stop and report BLOCKED, because the tests are then not exercising the bug.

- [x] **Step 5: Replace the accumulation with a model list**

In `Cubit/src/Voxel/VoxLoader.cpp`, replace lines 53-58 (the `voxSize`/`haveSize`/`haveVoxels`/`RawVoxel`/`rawVoxels` declarations) with:

```cpp
    struct RawVoxel { std::uint8_t x, y, z, colorIndex; };

    //One SIZE/XYZI pair, still in vox axes. A model's index in this vector is
    //the id the scene graph's shape nodes reference.
    struct RawModel
    {
        glm::ivec3 VoxSize{ 0 };
        std::vector<RawVoxel> Voxels;
    };

    std::vector<RawModel> models;
```

Note `RawVoxel` moves above `RawModel` because `RawModel` names it.

- [x] **Step 6: Make SIZE open a model and XYZI fill it**

Replace the `SIZE` branch (lines 77-88) with:

```cpp
        if (std::strcmp(id, "SIZE") == 0)
        {
            std::size_t p = contentStart;
            glm::ivec3 voxSize{ 0 };
            voxSize.x = ReadInt(bytes, p);
            voxSize.y = ReadInt(bytes, p);
            voxSize.z = ReadInt(bytes, p);
            if (voxSize.x <= 0 || voxSize.y <= 0 || voxSize.z <= 0 ||
                voxSize.x > MaxDimension || voxSize.y > MaxDimension ||
                voxSize.z > MaxDimension)
                throw std::runtime_error("vox: model dimension out of range");

            // SIZE opens a model; the XYZI that follows fills it. The pair is
            // always adjacent and always in this order.
            models.push_back(RawModel{ voxSize, {} });
        }
```

Replace the `XYZI` branch (lines 89-105) with:

```cpp
        else if (std::strcmp(id, "XYZI") == 0)
        {
            if (models.empty())
                throw std::runtime_error("vox: voxel data before any SIZE chunk");

            std::size_t p = contentStart;
            const std::int32_t count = ReadInt(bytes, p);
            if (count < 0 ||
                p + static_cast<std::size_t>(count) * 4 > bytes.size())
                throw std::runtime_error("vox: truncated voxel data");

            std::vector<RawVoxel>& voxels = models.back().Voxels;
            voxels.reserve(static_cast<std::size_t>(count));
            for (std::int32_t i = 0; i < count; ++i)
            {
                voxels.push_back(RawVoxel{
                    bytes[p + 0], bytes[p + 1], bytes[p + 2], bytes[p + 3] });
                p += 4;
            }
        }
```

- [x] **Step 7: Flatten the model list**

Replace lines 129-158 (from `if (!haveSize || !haveVoxels)` to the `return model;`) with:

```cpp
    if (models.empty())
        throw std::runtime_error("vox: missing SIZE or XYZI chunk");

    // With no scene graph read yet, every model sits at the origin, so the
    // world is the largest of them on each axis.
    glm::ivec3 worldSize{ 0 };
    for (const RawModel& raw : models)
        worldSize = glm::max(worldSize,
            glm::ivec3(raw.VoxSize.x, raw.VoxSize.z, raw.VoxSize.y));

    VoxModel model;
    model.Size = worldSize;
    model.Colors = palette;
    model.Voxels.assign(
        static_cast<std::size_t>(model.Size.x) *
        static_cast<std::size_t>(model.Size.y) *
        static_cast<std::size_t>(model.Size.z), 0);

    // Later models win where they overlap, matching the order they are drawn.
    for (const RawModel& raw : models)
        for (const RawVoxel& v : raw.Voxels)
        {
            const int cx = v.x;
            const int cy = v.z;
            const int cz = v.y;
            if (cx < 0 || cx >= model.Size.x ||
                cy < 0 || cy >= model.Size.y ||
                cz < 0 || cz >= model.Size.z)
                continue; // defensively ignore voxels outside the declared size

            const std::size_t index = static_cast<std::size_t>(cx) +
                static_cast<std::size_t>(model.Size.x) *
                (static_cast<std::size_t>(cy) +
                 static_cast<std::size_t>(model.Size.y) * static_cast<std::size_t>(cz));
            model.Voxels[index] = v.colorIndex;
        }

    return model;
```

- [x] **Step 8: Build and run the whole suite**

Run: `"C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" Tests/Tests.vcxproj -p:Configuration=Debug -p:Platform=x64`
Expected: PASS, including the three new cases and every existing loader/writer/world-save case.

- [x] **Step 9: Commit**

```bash
git add Cubit/src/Voxel/VoxLoader.cpp Tests/src/VoxLoaderTests.cpp
git commit -m "Read every model in a .vox file, not just the last

SIZE opened a variable that the next SIZE overwrote while XYZI appended
into one shared list, so a two-model file loaded as one model of the
wrong size with both models' voxels in it."
```

---

### Task 2: Parse the scene-graph node chunks

Read `nTRN`, `nGRP` and `nSHP` into a node table. Nothing uses the table yet — placement is Task 3. Splitting the byte-level parsing from the placement maths means a failure in either has one obvious cause.

**Files:**
- Modify: `Cubit/src/Voxel/VoxLoader.cpp` (anonymous namespace, and the chunk walk)
- Test: `Tests/src/VoxLoaderTests.cpp` (helpers + new cases)

**Interfaces:**
- Consumes: `RawModel`, `PushModel`, `PushPalette`, `MakeVoxFile` from Task 1.
- Produces: in `VoxLoader.cpp` — `ReadString`, `ReadDict`, `struct SceneNode { enum class Kind { Transform, Group, Shape } Type; glm::ivec3 Translation; std::vector<int> Children; std::vector<int> ModelIds; }`, and a `std::map<int, SceneNode> nodes` local in `Parse`. In the tests — `PushTestString`, `PushTestDict`, `PushTestChunk`, `struct GraphPlacement { glm::ivec3 Translation; std::string Rotation; }`, `PushSceneGraph`.

- [x] **Step 1: Add the byte readers**

In `Cubit/src/Voxel/VoxLoader.cpp`, add to the anonymous namespace after `TagEquals` (line 32):

```cpp
    //Reads a length-prefixed .vox STRING and advances the offset. The bytes are
    //not null-terminated, so the length is the only thing that ends the string.
    std::string ReadString(std::span<const std::uint8_t> bytes, std::size_t& offset)
    {
        const std::int32_t length = ReadInt(bytes, offset);
        if (length < 0 || offset + static_cast<std::size_t>(length) > bytes.size())
            throw std::runtime_error("vox: truncated string");

        std::string value(reinterpret_cast<const char*>(bytes.data() + offset),
            static_cast<std::size_t>(length));
        offset += static_cast<std::size_t>(length);
        return value;
    }

    //Reads a .vox DICT: a pair count followed by that many key/value strings.
    //Every node chunk carries at least one, so this must consume it even when
    //nothing in it interests us — the fields after it are positional.
    std::map<std::string, std::string> ReadDict(
        std::span<const std::uint8_t> bytes, std::size_t& offset)
    {
        const std::int32_t count = ReadInt(bytes, offset);
        if (count < 0)
            throw std::runtime_error("vox: negative dictionary size");

        std::map<std::string, std::string> dict;
        for (std::int32_t i = 0; i < count; ++i)
        {
            const std::string key = ReadString(bytes, offset);
            const std::string value = ReadString(bytes, offset);
            dict[key] = value;
        }
        return dict;
    }

    //Parses a "_t" attribute: three space-separated integers in vox axes.
    glm::ivec3 ParseTranslation(const std::string& text)
    {
        std::istringstream stream(text);
        glm::ivec3 t{ 0 };
        stream >> t.x >> t.y >> t.z;
        if (stream.fail())
            throw std::runtime_error("vox: malformed translation attribute");
        return t;
    }

    //A scene-graph node reduced to the fields placement needs. One struct for
    //all three node kinds keeps the traversal a single switch rather than three
    //parallel lookups.
    struct SceneNode
    {
        enum class Kind { Transform, Group, Shape };

        Kind Type = Kind::Group;
        glm::ivec3 Translation{ 0 };  // vox axes; transform nodes only
        std::vector<int> Children;    // child node ids; transform nodes have one
        std::vector<int> ModelIds;    // shape nodes only
    };
```

Add `#include <map>`, `#include <sstream>` and `#include <string>` to the file's include block.

- [x] **Step 2: Declare the node table in `Parse`**

In `Parse`, next to the `std::vector<RawModel> models;` line added in Task 1, add:

```cpp
    std::map<int, SceneNode> nodes;
```

- [x] **Step 3: Write the failing test**

First add these helpers to the anonymous namespace of `Tests/src/VoxLoaderTests.cpp`, after `PushPalette`:

```cpp
    //Appends a length-prefixed .vox STRING.
    void PushTestString(std::vector<std::uint8_t>& out, const std::string& text)
    {
        PushInt(out, static_cast<std::int32_t>(text.size()));
        out.insert(out.end(), text.begin(), text.end());
    }

    //Appends a .vox DICT.
    void PushTestDict(std::vector<std::uint8_t>& out,
        const std::vector<std::pair<std::string, std::string>>& entries)
    {
        PushInt(out, static_cast<std::int32_t>(entries.size()));
        for (const auto& entry : entries)
        {
            PushTestString(out, entry.first);
            PushTestString(out, entry.second);
        }
    }

    //Wraps content bytes in a chunk header. Node chunks carry no child chunks:
    //the scene tree is expressed by node ids, not by chunk nesting.
    void PushTestChunk(std::vector<std::uint8_t>& out, const char* tag,
        const std::vector<std::uint8_t>& content)
    {
        PushTag(out, tag);
        PushInt(out, static_cast<std::int32_t>(content.size()));
        PushInt(out, 0);
        out.insert(out.end(), content.begin(), content.end());
    }

    //Where one model sits. Rotation is the raw "_r" attribute; leave it empty to
    //omit the attribute, which is how an unrotated model is normally written.
    struct GraphPlacement
    {
        glm::ivec3 Translation{ 0 };
        std::string Rotation;
    };

    //Appends a scene graph placing model i at placements[i]: a root transform, a
    //group holding one transform per model, and a shape under each transform.
    void PushSceneGraph(std::vector<std::uint8_t>& out,
        const std::vector<GraphPlacement>& placements)
    {
        {
            std::vector<std::uint8_t> content;
            PushInt(content, 0);   // node id
            PushTestDict(content, {});
            PushInt(content, 1);   // child: the group
            PushInt(content, -1);  // reserved
            PushInt(content, -1);  // layer
            PushInt(content, 1);   // frame count
            PushTestDict(content, {});
            PushTestChunk(out, "nTRN", content);
        }

        {
            std::vector<std::uint8_t> content;
            PushInt(content, 1);   // node id
            PushTestDict(content, {});
            PushInt(content, static_cast<std::int32_t>(placements.size()));
            for (std::size_t i = 0; i < placements.size(); ++i)
                PushInt(content, static_cast<std::int32_t>(2 + i * 2));
            PushTestChunk(out, "nGRP", content);
        }

        for (std::size_t i = 0; i < placements.size(); ++i)
        {
            const std::int32_t transformId = static_cast<std::int32_t>(2 + i * 2);
            const std::int32_t shapeId = transformId + 1;
            const GraphPlacement& p = placements[i];

            std::vector<std::pair<std::string, std::string>> frame;
            frame.push_back({ "_t",
                std::to_string(p.Translation.x) + " " +
                std::to_string(p.Translation.y) + " " +
                std::to_string(p.Translation.z) });
            if (!p.Rotation.empty())
                frame.push_back({ "_r", p.Rotation });

            {
                std::vector<std::uint8_t> content;
                PushInt(content, transformId);
                PushTestDict(content, {});
                PushInt(content, shapeId);
                PushInt(content, -1);
                PushInt(content, -1);
                PushInt(content, 1);
                PushTestDict(content, frame);
                PushTestChunk(out, "nTRN", content);
            }

            {
                std::vector<std::uint8_t> content;
                PushInt(content, shapeId);
                PushTestDict(content, {});
                PushInt(content, 1);                            // one model
                PushInt(content, static_cast<std::int32_t>(i)); // model id
                PushTestDict(content, {});
                PushTestChunk(out, "nSHP", content);
            }
        }
    }
```

Then append this case:

```cpp
TEST_CASE("A scene graph parses without disturbing the chunk walk")
{
    // Placement is not read yet: this only proves the node chunks are consumed
    // to exactly their own length, so the chunks after them still parse.
    std::vector<std::uint8_t> children;
    PushModel(children, ModelSpec{ 2, 2, 2, { { 0, 0, 0, 3 } } });
    PushSceneGraph(children, { GraphPlacement{ glm::ivec3(0, 0, 0), "" } });
    PushPalette(children);

    const VoxModel model = VoxLoader::Parse(MakeVoxFile(children));

    CHECK(model.Size == glm::ivec3(2, 2, 2));
    CHECK(model.At(0, 0, 0) == 3);
    // The palette came after the graph, so reading it back proves the graph's
    // chunks were skipped by the right number of bytes.
    CHECK(model.Colors[3].x == doctest::Approx(3.0f / 255.0f));
}
```

- [x] **Step 4: Run to verify it passes already, then make it meaningful**

Run: `./bin/Debug-windows-x86_64/Tests/Tests.exe -tc="A scene graph parses without disturbing the chunk walk"`
Expected: **PASS** — the existing walk skips unknown chunks by their declared content size, so this case passes before the node parsing exists. That is fine and expected: it is a regression guard for Step 5, which is where the code starts reading inside those chunks and can start reading the wrong number of bytes.

- [x] **Step 5: Parse the three node chunks**

In `Parse`'s chunk walk, add these branches after the `RGBA` branch (currently ending at line 121):

```cpp
        else if (std::strcmp(id, "nTRN") == 0)
        {
            std::size_t p = contentStart;
            const std::int32_t nodeId = ReadInt(bytes, p);
            ReadDict(bytes, p);                       // node attributes, unused
            const std::int32_t childId = ReadInt(bytes, p);
            ReadInt(bytes, p);                        // reserved, always -1
            ReadInt(bytes, p);                        // layer id
            const std::int32_t frameCount = ReadInt(bytes, p);
            if (frameCount < 1)
                throw std::runtime_error("vox: transform node has no frames");

            SceneNode node;
            node.Type = SceneNode::Kind::Transform;
            node.Children.push_back(childId);

            for (std::int32_t f = 0; f < frameCount; ++f)
            {
                const std::map<std::string, std::string> frame = ReadDict(bytes, p);

                // Only the first frame is used: the rest describe the same model
                // at later animation times, and Cubit has no animation. They are
                // still read, because skipping them would leave the offset short.
                if (f != 0)
                    continue;

                const auto t = frame.find("_t");
                if (t != frame.end())
                    node.Translation = ParseTranslation(t->second);
            }

            nodes[nodeId] = std::move(node);
        }
        else if (std::strcmp(id, "nGRP") == 0)
        {
            std::size_t p = contentStart;
            const std::int32_t nodeId = ReadInt(bytes, p);
            ReadDict(bytes, p);
            const std::int32_t childCount = ReadInt(bytes, p);
            if (childCount < 0)
                throw std::runtime_error("vox: group node has a negative child count");

            SceneNode node;
            node.Type = SceneNode::Kind::Group;
            for (std::int32_t i = 0; i < childCount; ++i)
                node.Children.push_back(ReadInt(bytes, p));

            nodes[nodeId] = std::move(node);
        }
        else if (std::strcmp(id, "nSHP") == 0)
        {
            std::size_t p = contentStart;
            const std::int32_t nodeId = ReadInt(bytes, p);
            ReadDict(bytes, p);
            const std::int32_t modelCount = ReadInt(bytes, p);
            if (modelCount < 0)
                throw std::runtime_error("vox: shape node has a negative model count");

            SceneNode node;
            node.Type = SceneNode::Kind::Shape;
            for (std::int32_t i = 0; i < modelCount; ++i)
            {
                node.ModelIds.push_back(ReadInt(bytes, p));
                ReadDict(bytes, p);               // per-model attributes, unused
            }

            nodes[nodeId] = std::move(node);
        }
```

Add `(void)nodes;` immediately before the `if (models.empty())` line so the unused-variable warning does not fail the build. Task 3 removes it.

- [x] **Step 6: Run the whole suite**

Run: `"C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" Tests/Tests.vcxproj -p:Configuration=Debug -p:Platform=x64`
Expected: PASS. The graph case still passing now means the node parsing consumed exactly the right bytes; if the palette assertion fails, a field is being read at the wrong offset.

- [x] **Step 7: Commit**

```bash
git add Cubit/src/Voxel/VoxLoader.cpp Tests/src/VoxLoaderTests.cpp
git commit -m "Parse the .vox scene-graph node chunks

Reads nTRN, nGRP and nSHP into a node table. Nothing places models by it
yet; keeping the byte-level parsing separate from the placement maths
means a failure in either has one cause."
```

---

### Task 3: Place models by walking the graph

Turn the node table into an origin per model, take the union, and normalise. This is where the centre convention and the axis swap live.

**Files:**
- Modify: `Cubit/src/Voxel/VoxLoader.cpp` (anonymous namespace + the flatten in `Parse`)
- Test: `Tests/src/VoxLoaderTests.cpp` (new cases)

**Interfaces:**
- Consumes: `SceneNode`, `nodes`, `RawModel`, `models` from Task 2; `GraphPlacement`, `PushSceneGraph` from the tests.
- Produces: `struct PlacedModel { int ModelId; glm::ivec3 VoxOrigin; }` and `ResolvePlacement(const std::map<int, SceneNode>&, const std::vector<RawModel>&, int nodeId, glm::ivec3 translation, std::vector<PlacedModel>& out, int depth)`, plus `constexpr int RootNodeId = 0;` and `constexpr int MaxSceneDepth = 64;`.

- [x] **Step 1: Write the failing tests**

Append to `Tests/src/VoxLoaderTests.cpp`:

```cpp
TEST_CASE("A translation places a model by its centre, not its corner")
{
    // A 4x4x4 model whose centre is at vox (10, 10, 10) has its minimum corner
    // at 10 - 4/2 = 8 on every axis.
    std::vector<std::uint8_t> children;
    PushModel(children, ModelSpec{ 4, 4, 4, { { 0, 0, 0, 5 } } });
    PushSceneGraph(children, { GraphPlacement{ glm::ivec3(10, 10, 10), "" } });
    PushPalette(children);

    const VoxModel model = VoxLoader::Parse(MakeVoxFile(children));

    // One model, so normalisation puts its corner back at the origin.
    CHECK(model.Size == glm::ivec3(4, 4, 4));
    CHECK(model.At(0, 0, 0) == 5);
}

TEST_CASE("An odd-sized model halves the same way on both sides")
{
    // Size 3 halves to 1, so a centre at 5 puts the corner at 4. Two models
    // pin the relative offset, which is what an off-by-one would move.
    std::vector<std::uint8_t> children;
    PushModel(children, ModelSpec{ 3, 3, 3, { { 0, 0, 0, 1 } } });
    PushModel(children, ModelSpec{ 3, 3, 3, { { 0, 0, 0, 2 } } });
    PushPalette(children);
    PushSceneGraph(children, {
        GraphPlacement{ glm::ivec3(1, 1, 1), "" },   // corner (0,0,0)
        GraphPlacement{ glm::ivec3(4, 1, 1), "" } }); // corner (3,0,0)

    const VoxModel model = VoxLoader::Parse(MakeVoxFile(children));

    CHECK(model.Size == glm::ivec3(6, 3, 3));
    CHECK(model.At(0, 0, 0) == 1);
    CHECK(model.At(3, 0, 0) == 2);
}

TEST_CASE("Two models stitch into one world side by side")
{
    // Two 2-wide models, the second offset 2 along vox x. Centres are at 1 and 3.
    std::vector<std::uint8_t> children;
    PushModel(children, ModelSpec{ 2, 2, 2, { { 0, 0, 0, 1 } } });
    PushModel(children, ModelSpec{ 2, 2, 2, { { 1, 0, 0, 2 } } });
    PushSceneGraph(children, {
        GraphPlacement{ glm::ivec3(1, 1, 1), "" },
        GraphPlacement{ glm::ivec3(3, 1, 1), "" } });
    PushPalette(children);

    const VoxModel model = VoxLoader::Parse(MakeVoxFile(children));

    CHECK(model.Size == glm::ivec3(4, 2, 2));
    CHECK(model.At(0, 0, 0) == 1);
    CHECK(model.At(3, 0, 0) == 2); // second model's local x=1, placed at x=2
}

TEST_CASE("A translation in vox axes lands on the swapped Cubit axis")
{
    // Offset along vox y must move the model along Cubit z, not Cubit y.
    std::vector<std::uint8_t> children;
    PushModel(children, ModelSpec{ 2, 2, 2, { { 0, 0, 0, 1 } } });
    PushModel(children, ModelSpec{ 2, 2, 2, { { 0, 0, 0, 2 } } });
    PushSceneGraph(children, {
        GraphPlacement{ glm::ivec3(1, 1, 1), "" },
        GraphPlacement{ glm::ivec3(1, 3, 1), "" } });
    PushPalette(children);

    const VoxModel model = VoxLoader::Parse(MakeVoxFile(children));

    CHECK(model.Size == glm::ivec3(2, 2, 4));
    CHECK(model.At(0, 0, 0) == 1);
    CHECK(model.At(0, 0, 2) == 2);
}

TEST_CASE("Negative translations normalise so the world corner is the origin")
{
    std::vector<std::uint8_t> children;
    PushModel(children, ModelSpec{ 2, 2, 2, { { 0, 0, 0, 1 } } });
    PushModel(children, ModelSpec{ 2, 2, 2, { { 1, 1, 1, 2 } } });
    PushSceneGraph(children, {
        GraphPlacement{ glm::ivec3(-9, 1, 1), "" },  // corner (-10, 0, 0)
        GraphPlacement{ glm::ivec3(-7, 1, 1), "" } }); // corner (-8, 0, 0)
    PushPalette(children);

    const VoxModel model = VoxLoader::Parse(MakeVoxFile(children));

    CHECK(model.Size == glm::ivec3(4, 2, 2));
    CHECK(model.At(0, 0, 0) == 1);
    CHECK(model.At(3, 1, 1) == 2);
}
```

- [x] **Step 2: Run to verify they fail**

Run: `./bin/Debug-windows-x86_64/Tests/Tests.exe -tc="Two models stitch into one world side by side"`
Expected: FAIL — the world comes back 2×2×2 with both models on top of each other, because placement is not read yet.

- [x] **Step 3: Add the traversal**

**Correction found while executing:** `ResolvePlacement` names `RawModel`, but Task 1
declared `RawVoxel`/`RawModel` *inside* `Parse`, where a namespace-scope function
cannot see them. Hoist both structs to the top of the anonymous namespace first.
Nothing else changes — `Parse` still declares `std::vector<RawModel> models;` locally.

Add to `VoxLoader.cpp`'s anonymous namespace, after `SceneNode`:

```cpp
    //The scene graph's entry point. MagicaVoxel always writes the root
    //transform as node 0.
    constexpr int RootNodeId = 0;

    //Nothing in the format stops a hand-made file's graph containing a cycle,
    //which would recurse until the stack ran out. A depth cap turns a crash
    //into an error message.
    constexpr int MaxSceneDepth = 64;

    //One model with the origin the graph placed it at, in vox axes and before
    //the world is normalised.
    struct PlacedModel
    {
        int ModelId = 0;
        glm::ivec3 VoxOrigin{ 0 };
    };

    //Walks the graph from a node, accumulating translations, and appends every
    //model it reaches with the origin it ends up at.
    void ResolvePlacement(
        const std::map<int, SceneNode>& nodes,
        const std::vector<RawModel>& models,
        int nodeId,
        glm::ivec3 translation,
        std::vector<PlacedModel>& out,
        int depth)
    {
        if (depth > MaxSceneDepth)
            throw std::runtime_error("vox: scene graph is too deeply nested");

        const auto it = nodes.find(nodeId);
        if (it == nodes.end())
            throw std::runtime_error("vox: scene graph references a missing node");

        const SceneNode& node = it->second;

        switch (node.Type)
        {
        case SceneNode::Kind::Transform:
            translation += node.Translation;
            // A transform holds exactly one child, so the group's loop below
            // walks it correctly and there is no second traversal to write.
            [[fallthrough]];

        case SceneNode::Kind::Group:
            for (const int child : node.Children)
                ResolvePlacement(nodes, models, child, translation, out, depth + 1);
            break;

        case SceneNode::Kind::Shape:
            for (const int modelId : node.ModelIds)
            {
                if (modelId < 0 ||
                    static_cast<std::size_t>(modelId) >= models.size())
                    throw std::runtime_error(
                        "vox: shape node references a missing model");

                //_t places the model's centre, so its minimum corner is half a
                //model back. Sizes are positive, so halving needs no floor
                //correction, and the writer halves identically — which is what
                //makes writing and reading an exact inverse.
                const glm::ivec3 half = models[modelId].VoxSize / 2;
                out.push_back(PlacedModel{ modelId, translation - half });
            }
            break;
        }
    }
```

Add `#include <limits>` to the include block (Step 4 uses it).

- [x] **Step 4: Replace the flatten with a placed flatten**

Delete the `(void)nodes;` line added in Task 2. Replace everything from `// With no scene graph read yet...` through the closing of the blit loop (the block added in Task 1, Step 7, keeping the `if (models.empty())` throw above it) with:

```cpp
    std::vector<PlacedModel> placed;
    if (nodes.empty())
    {
        // A file with no scene graph — which is what Cubit's own writer emits
        // for a single model, and what MagicaVoxel wrote before the graph
        // existed — puts every model at the origin.
        for (std::size_t i = 0; i < models.size(); ++i)
            placed.push_back(PlacedModel{ static_cast<int>(i), glm::ivec3(0) });
    }
    else
    {
        ResolvePlacement(nodes, models, RootNodeId, glm::ivec3(0), placed, 0);
    }

    if (placed.empty())
        throw std::runtime_error("vox: scene graph places no models");

    //The world is the union of where the models landed, in Cubit axes, so it is
    //exactly big enough to hold every one of them wherever the file put it.
    glm::ivec3 unionMin(std::numeric_limits<int>::max());
    glm::ivec3 unionMax(std::numeric_limits<int>::min());

    for (const PlacedModel& p : placed)
    {
        const glm::ivec3 voxSize = models[p.ModelId].VoxSize;
        const glm::ivec3 origin(p.VoxOrigin.x, p.VoxOrigin.z, p.VoxOrigin.y);
        const glm::ivec3 size(voxSize.x, voxSize.z, voxSize.y);

        unionMin = glm::min(unionMin, origin);
        unionMax = glm::max(unionMax, origin + size);
    }

    VoxModel model;
    model.Size = unionMax - unionMin;
    model.Colors = palette;
    model.Voxels.assign(
        static_cast<std::size_t>(model.Size.x) *
        static_cast<std::size_t>(model.Size.y) *
        static_cast<std::size_t>(model.Size.z), 0);

    // Later placements win where they overlap, matching the order they are
    // drawn. Files Cubit writes never overlap; the rule exists so a hand-made
    // one has a defined answer.
    for (const PlacedModel& p : placed)
    {
        const RawModel& raw = models[p.ModelId];
        const glm::ivec3 origin =
            glm::ivec3(p.VoxOrigin.x, p.VoxOrigin.z, p.VoxOrigin.y) - unionMin;

        for (const RawVoxel& v : raw.Voxels)
        {
            const int cx = origin.x + v.x;
            const int cy = origin.y + v.z;
            const int cz = origin.z + v.y;
            if (cx < 0 || cx >= model.Size.x ||
                cy < 0 || cy >= model.Size.y ||
                cz < 0 || cz >= model.Size.z)
                continue; // defensively ignore voxels outside the declared size

            const std::size_t index = static_cast<std::size_t>(cx) +
                static_cast<std::size_t>(model.Size.x) *
                (static_cast<std::size_t>(cy) +
                 static_cast<std::size_t>(model.Size.y) * static_cast<std::size_t>(cz));
            model.Voxels[index] = v.colorIndex;
        }
    }

    return model;
```

- [x] **Step 5: Run the whole suite**

Run: `"C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" Tests/Tests.vcxproj -p:Configuration=Debug -p:Platform=x64`
Expected: PASS, including all five new cases and the Task 1 no-graph cases (which now go through the `nodes.empty()` branch).

- [x] **Step 6: Commit**

```bash
git add Cubit/src/Voxel/VoxLoader.cpp Tests/src/VoxLoaderTests.cpp
git commit -m "Place .vox models by walking the scene graph

_t gives a model's centre, not its corner, and it is in vox axes, so both
the halving and the axis swap have to be applied before the union. The
world normalises to the union's minimum so negative translations load."
```

---

### Task 4: Reject rotation and absurd extents

Two guards. Neither changes a valid file; both turn a silent wrong answer or an allocation failure into a message.

**Files:**
- Modify: `Cubit/src/Voxel/VoxLoader.cpp`
- Test: `Tests/src/VoxLoaderTests.cpp`

**Interfaces:**
- Consumes: everything from Task 3.
- Produces: `constexpr std::uint8_t IdentityRotation`, `constexpr int MaxWorldDimension`, `constexpr std::size_t MaxWorldVolume`, `RequireIdentityRotation(const std::map<std::string, std::string>&)`, `RequireLoadableSize(const glm::ivec3&)`.

- [x] **Step 1: Write the failing tests**

Append to `Tests/src/VoxLoaderTests.cpp`:

```cpp
TEST_CASE("A rotated model is rejected rather than placed wrongly")
{
    // "1" is a rotation that swaps two axes. Loading it as if it were identity
    // would put geometry somewhere the file never asked for and say nothing.
    std::vector<std::uint8_t> children;
    PushModel(children, ModelSpec{ 2, 2, 2, { { 0, 0, 0, 1 } } });
    PushSceneGraph(children, { GraphPlacement{ glm::ivec3(1, 1, 1), "1" } });
    PushPalette(children);

    CHECK_THROWS_AS(VoxLoader::Parse(MakeVoxFile(children)), std::runtime_error);
}

TEST_CASE("The identity rotation is accepted when written out explicitly")
{
    std::vector<std::uint8_t> children;
    PushModel(children, ModelSpec{ 2, 2, 2, { { 0, 0, 0, 1 } } });
    PushSceneGraph(children, { GraphPlacement{ glm::ivec3(1, 1, 1), "4" } });
    PushPalette(children);

    const VoxModel model = VoxLoader::Parse(MakeVoxFile(children));
    CHECK(model.At(0, 0, 0) == 1);
}

TEST_CASE("A union extent past the world limit is rejected")
{
    // Two small models pushed a very long way apart claim an enormous world.
    std::vector<std::uint8_t> children;
    PushModel(children, ModelSpec{ 2, 2, 2, { { 0, 0, 0, 1 } } });
    PushModel(children, ModelSpec{ 2, 2, 2, { { 0, 0, 0, 2 } } });
    PushSceneGraph(children, {
        GraphPlacement{ glm::ivec3(0, 0, 0), "" },
        GraphPlacement{ glm::ivec3(100000, 0, 0), "" } });
    PushPalette(children);

    CHECK_THROWS_AS(VoxLoader::Parse(MakeVoxFile(children)), std::runtime_error);
}
```

- [x] **Step 2: Run to verify they fail**

Run: `./bin/Debug-windows-x86_64/Tests/Tests.exe -tc="A rotated model is rejected rather than placed wrongly"`
Expected: FAIL — no exception is thrown; the rotation is currently ignored.

Run: `./bin/Debug-windows-x86_64/Tests/Tests.exe -tc="A union extent past the world limit is rejected"`
Expected: FAIL, likely by crashing or hanging on a ~100000-wide allocation. That crash *is* the bug this guard exists for. If the process dies rather than reporting a failed assertion, that still counts as red — continue.

- [x] **Step 3: Add the constants and guards**

Add to `VoxLoader.cpp`'s anonymous namespace, next to `MaxDimension`:

```cpp
    //The identity rotation as .vox packs it: bits 0-1 name the column holding
    //row 0's non-zero entry, bits 2-3 do the same for row 1, and bits 4-6 are
    //the three sign bits. Identity is row 0 to column 0, row 1 to column 1,
    //all positive, which is 1 << 2.
    //
    //A wrong constant here refuses a file that should load, which is a message
    //someone can act on — never a map placed somewhere it does not belong.
    constexpr int IdentityRotation = 4;

    //A file can claim any translation it likes, so the union of its models can
    //be arbitrarily large. Without these, a malformed one asks for a
    //multi-gigabyte allocation and is discovered only by the allocator.
    //1024 comfortably clears the 512 maps this exists for.
    constexpr int MaxWorldDimension = 1024;
    constexpr std::size_t MaxWorldVolume = 128ull * 1024ull * 1024ull;
```

And after `ParseTranslation`:

```cpp
    //Refuses a rotated model instead of ignoring the rotation. A missing "_r"
    //means identity and is the common case, because MagicaVoxel only writes the
    //attribute when it is not identity.
    void RequireIdentityRotation(const std::map<std::string, std::string>& frame)
    {
        const auto it = frame.find("_r");
        if (it == frame.end())
            return;

        std::istringstream stream(it->second);
        int rotation = 0;
        stream >> rotation;
        if (stream.fail())
            throw std::runtime_error("vox: malformed rotation attribute");

        if (rotation != IdentityRotation)
            throw std::runtime_error("vox: rotated models are not supported");
    }

    //Rejects a union extent no map should have.
    void RequireLoadableSize(const glm::ivec3& size)
    {
        const char* const axes[3] = { "x", "y", "z" };

        for (int i = 0; i < 3; ++i)
            if (size[i] > MaxWorldDimension)
                throw std::runtime_error(
                    std::string("vox: stitched world is too large (")
                    + axes[i] + " = " + std::to_string(size[i]) + ")");

        const std::size_t volume =
            static_cast<std::size_t>(size.x) *
            static_cast<std::size_t>(size.y) *
            static_cast<std::size_t>(size.z);

        if (volume > MaxWorldVolume)
            throw std::runtime_error("vox: stitched world has too many voxels");
    }
```

- [x] **Step 4: Call the guards**

In the `nTRN` branch, inside the `if (f != 0) continue;` block's frame handling, add the rotation check immediately after the `continue`:

```cpp
                if (f != 0)
                    continue;

                RequireIdentityRotation(frame);

                const auto t = frame.find("_t");
```

In `Parse`, immediately after `model.Size = unionMax - unionMin;` insert:

```cpp
    RequireLoadableSize(model.Size);
```

- [x] **Step 5: Run the whole suite**

Run: `"C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" Tests/Tests.vcxproj -p:Configuration=Debug -p:Platform=x64`
Expected: PASS, all three new cases included.

- [x] **Step 6: Commit**

```bash
git add Cubit/src/Voxel/VoxLoader.cpp Tests/src/VoxLoaderTests.cpp
git commit -m "Refuse rotated models and absurd stitched extents

Ignoring a rotation would place geometry somewhere the file did not ask
for and report nothing. An unbounded union would be discovered only by a
failed multi-gigabyte allocation."
```

---

### Task 5: Split a large model into tiles on write

`Write` grows a second path. Models that fit in 256 still take the old one and emit identical bytes.

**Files:**
- Modify: `Cubit/src/Voxel/VoxWriter.cpp:53-121` (extract today's body) and the anonymous namespace
- Test: `Tests/src/VoxWriterTests.cpp`

**Interfaces:**
- Consumes: `VoxLoader::Parse` handling multi-model files (Tasks 1–4).
- Produces: no public API change — `VoxWriter::Write` keeps its signature. Internally: `constexpr int TileSize`, `struct Placement { glm::ivec3 Origin; glm::ivec3 Size; }`, `PushString`, `PushDict`, `PushChunk`, `ExtractTile`, `WriteSingleModel`, `WriteTiled`, `BuildSceneGraph`.

- [x] **Step 1: Write the failing tests**

Append to `Tests/src/VoxWriterTests.cpp`:

```cpp
//Counts SIZE chunks, which is one per model written.
static int CountModels(const std::vector<std::uint8_t>& bytes)
{
    int count = 0;
    for (std::size_t i = 0; i + 4 <= bytes.size(); ++i)
        if (std::memcmp(bytes.data() + i, "SIZE", 4) == 0)
            ++count;
    return count;
}

TEST_CASE("A model wider than 256 is written as several tiles")
{
    VoxModel m = MakeModel(300, 2, 2);
    SetId(m, 0, 0, 0, 1);
    SetId(m, 299, 1, 1, 2);

    const std::vector<std::uint8_t> bytes = VoxWriter::Write(m);

    CHECK(CountModels(bytes) == 2); // 256 + 44
}

TEST_CASE("A tiled model round-trips through the loader")
{
    VoxModel m = MakeModel(300, 2, 300);
    SetId(m, 0, 0, 0, 1);
    SetId(m, 299, 1, 299, 2);
    SetId(m, 255, 0, 256, 3);   // straddles the tile boundary on both axes
    SetId(m, 256, 0, 255, 4);

    const VoxModel r = VoxLoader::Parse(VoxWriter::Write(m));

    CHECK(r.Size == m.Size);
    for (int z = 0; z < m.Size.z; ++z)
        for (int y = 0; y < m.Size.y; ++y)
            for (int x = 0; x < m.Size.x; ++x)
                REQUIRE(r.At(x, y, z) == m.At(x, y, z));
}

TEST_CASE("All-air tiles are skipped rather than written empty")
{
    // 300 wide is two tiles, but only the first holds anything.
    VoxModel m = MakeModel(300, 2, 2);
    SetId(m, 0, 0, 0, 1);

    const std::vector<std::uint8_t> bytes = VoxWriter::Write(m);

    CHECK(CountModels(bytes) == 1);
    CHECK(VoxLoader::Parse(bytes).At(0, 0, 0) == 1);
}

TEST_CASE("A model that fits in one tile is written exactly as before")
{
    // Pins the single-model path: its bytes are what starter.vox and the
    // existing suite are built on, so tiling must not touch them.
    VoxModel m = MakeModel(4, 4, 4);
    SetId(m, 1, 2, 3, 7);

    const std::vector<std::uint8_t> bytes = VoxWriter::Write(m);

    CHECK(CountModels(bytes) == 1);
    // No scene graph at all: a single model needs none, and adding one would
    // change every file Cubit has already written.
    bool hasGraph = false;
    for (std::size_t i = 0; i + 4 <= bytes.size(); ++i)
        if (std::memcmp(bytes.data() + i, "nTRN", 4) == 0)
            hasGraph = true;
    CHECK_FALSE(hasGraph);
}
```

Add `#include <cstring>` to the file's include block.

- [x] **Step 2: Run to verify they fail**

Run: `./bin/Debug-windows-x86_64/Tests/Tests.exe -tc="A model wider than 256 is written as several tiles"`
Expected: FAIL — `VoxWriter::Write` throws `"vox: world is too large for a single .vox model (x = 300)"`.

- [x] **Step 3: Extract today's body as the single-model path**

In `Cubit/src/Voxel/VoxWriter.cpp`, rename `std::vector<std::uint8_t> VoxWriter::Write(const VoxModel& model)` (line 53) to a free function in the anonymous namespace:

```cpp
    //Writes a model small enough for one .vox model. These are the exact bytes
    //Cubit has always written, and they must stay that way: every .vox in the
    //repo and every round-trip test is built on them.
    std::vector<std::uint8_t> WriteSingleModel(const VoxModel& model)
```

Keep its body unchanged, including the `RequireWritableSize(model.Size)` call at the top — inside this path the check is now a genuine invariant rather than a limit on the world.

- [x] **Step 4: Add the tiling helpers**

Add to the anonymous namespace, after `RequireWritableSize`:

```cpp
    //Tiles are the largest a model can be, so a map is cut into as few as
    //possible.
    constexpr int TileSize = MaxDimension;

    //Where one written tile sits in the whole model, in Cubit axes.
    struct Placement
    {
        glm::ivec3 Origin{ 0 };
        glm::ivec3 Size{ 0 };
    };

    void PushString(std::vector<std::uint8_t>& out, const std::string& text)
    {
        PushInt(out, static_cast<std::int32_t>(text.size()));
        out.insert(out.end(), text.begin(), text.end());
    }

    void PushDict(std::vector<std::uint8_t>& out,
        const std::vector<std::pair<std::string, std::string>>& entries)
    {
        PushInt(out, static_cast<std::int32_t>(entries.size()));
        for (const auto& entry : entries)
        {
            PushString(out, entry.first);
            PushString(out, entry.second);
        }
    }

    //Wraps content in a chunk header. Node chunks carry no child chunks: the
    //scene tree is expressed by node ids, not by chunk nesting.
    void PushChunk(std::vector<std::uint8_t>& out, const char* tag,
        const std::vector<std::uint8_t>& content)
    {
        PushTag(out, tag);
        PushInt(out, static_cast<std::int32_t>(content.size()));
        PushInt(out, 0);
        out.insert(out.end(), content.begin(), content.end());
    }

    //Copies one tile out of a larger model. Reports whether it holds anything,
    //so the caller can drop empty tiles: an empty corner of a map should cost
    //nothing, and an empty model is not something MagicaVoxel expects to open.
    bool ExtractTile(const VoxModel& source, const Placement& tile, VoxModel& out)
    {
        out.Size = tile.Size;
        out.Colors = source.Colors;
        out.Voxels.assign(
            static_cast<std::size_t>(tile.Size.x) *
            static_cast<std::size_t>(tile.Size.y) *
            static_cast<std::size_t>(tile.Size.z), 0);

        bool any = false;
        for (int z = 0; z < tile.Size.z; ++z)
            for (int y = 0; y < tile.Size.y; ++y)
                for (int x = 0; x < tile.Size.x; ++x)
                {
                    const std::uint8_t id = source.At(
                        tile.Origin.x + x, tile.Origin.y + y, tile.Origin.z + z);
                    if (id == 0)
                        continue;

                    out.Voxels[static_cast<std::size_t>(x) +
                        static_cast<std::size_t>(tile.Size.x) *
                        (static_cast<std::size_t>(y) +
                         static_cast<std::size_t>(tile.Size.y) *
                         static_cast<std::size_t>(z))] = id;
                    any = true;
                }

        return any;
    }

    //Emits the graph placing each tile: a root transform, one group holding
    //every tile, and per tile a transform carrying its offset above a shape
    //naming its model. Node ids are assigned here — 0 root, 1 group, then a
    //transform and a shape per tile.
    std::vector<std::uint8_t> BuildSceneGraph(const std::vector<Placement>& tiles)
    {
        std::vector<std::uint8_t> out;

        {
            std::vector<std::uint8_t> content;
            PushInt(content, 0);   // node id
            PushDict(content, {}); // node attributes
            PushInt(content, 1);   // child: the group
            PushInt(content, -1);  // reserved
            PushInt(content, -1);  // layer: none, since no LAYR chunk is written
            PushInt(content, 1);   // frame count
            PushDict(content, {}); // frame attributes: identity
            PushChunk(out, "nTRN", content);
        }

        {
            std::vector<std::uint8_t> content;
            PushInt(content, 1);
            PushDict(content, {});
            PushInt(content, static_cast<std::int32_t>(tiles.size()));
            for (std::size_t i = 0; i < tiles.size(); ++i)
                PushInt(content, static_cast<std::int32_t>(2 + i * 2));
            PushChunk(out, "nGRP", content);
        }

        for (std::size_t i = 0; i < tiles.size(); ++i)
        {
            const std::int32_t transformId = static_cast<std::int32_t>(2 + i * 2);
            const std::int32_t shapeId = transformId + 1;

            //_t places the model's centre in vox axes, so half the tile goes
            //back on. The loader subtracts the same half; sizes are positive,
            //so both sides truncate identically and the pair is exact.
            const glm::ivec3 voxOrigin(
                tiles[i].Origin.x, tiles[i].Origin.z, tiles[i].Origin.y);
            const glm::ivec3 voxSize(
                tiles[i].Size.x, tiles[i].Size.z, tiles[i].Size.y);
            const glm::ivec3 centre = voxOrigin + voxSize / 2;

            {
                std::vector<std::uint8_t> content;
                PushInt(content, transformId);
                PushDict(content, {});
                PushInt(content, shapeId);
                PushInt(content, -1);
                PushInt(content, -1);
                PushInt(content, 1);
                PushDict(content, { { "_t",
                    std::to_string(centre.x) + " " +
                    std::to_string(centre.y) + " " +
                    std::to_string(centre.z) } });
                PushChunk(out, "nTRN", content);
            }

            {
                std::vector<std::uint8_t> content;
                PushInt(content, shapeId);
                PushDict(content, {});
                PushInt(content, 1);                            // one model
                PushInt(content, static_cast<std::int32_t>(i)); // model id
                PushDict(content, {});                          // model attributes
                PushChunk(out, "nSHP", content);
            }
        }

        return out;
    }
```

Add `#include <algorithm>`, `#include <utility>` and `#include <vector>` to the include block if not already present.

- [x] **Step 5: Add the tiled path and the branch**

Add after `BuildSceneGraph`, still in the anonymous namespace, a function that reuses the existing `SIZE`/`XYZI`/`RGBA` encoding. To avoid duplicating that encoding, first extract it: in `WriteSingleModel`, the three local buffers `size`, `xyzi` and `rgba` are built and then concatenated. Move the first two into helpers:

```cpp
    //Encodes one model's SIZE chunk. Vox axes are (x, z, y) relative to Cubit.
    void PushSizeChunk(std::vector<std::uint8_t>& out, const glm::ivec3& size)
    {
        PushTag(out, "SIZE");
        PushInt(out, 12);
        PushInt(out, 0);
        PushInt(out, size.x);
        PushInt(out, size.z);
        PushInt(out, size.y);
    }

    //Encodes one model's XYZI chunk: sparse, non-air voxels only, stored at
    //vox (cx, cz, cy).
    void PushVoxelChunk(std::vector<std::uint8_t>& out, const VoxModel& model)
    {
        std::vector<std::uint8_t> voxelBytes;
        std::int32_t count = 0;
        for (int z = 0; z < model.Size.z; ++z)
            for (int y = 0; y < model.Size.y; ++y)
                for (int x = 0; x < model.Size.x; ++x)
                {
                    const std::uint8_t id = model.At(x, y, z);
                    if (id == 0)
                        continue;
                    voxelBytes.push_back(static_cast<std::uint8_t>(x));
                    voxelBytes.push_back(static_cast<std::uint8_t>(z));
                    voxelBytes.push_back(static_cast<std::uint8_t>(y));
                    voxelBytes.push_back(id);
                    ++count;
                }

        PushTag(out, "XYZI");
        PushInt(out, 4 + 4 * count);
        PushInt(out, 0);
        PushInt(out, count);
        out.insert(out.end(), voxelBytes.begin(), voxelBytes.end());
    }

    //Encodes the palette. Entry j is colour index j + 1, the inverse of the
    //loader's off-by-one; entry 255 is unused and written as zero.
    void PushPaletteChunk(std::vector<std::uint8_t>& out, const Palette& colors)
    {
        PushTag(out, "RGBA");
        PushInt(out, 256 * 4);
        PushInt(out, 0);
        for (int j = 0; j < 256; ++j)
        {
            if (j < 255)
            {
                const glm::vec4& c = colors[static_cast<std::size_t>(j) + 1];
                out.push_back(ToByte(c.r));
                out.push_back(ToByte(c.g));
                out.push_back(ToByte(c.b));
                out.push_back(ToByte(c.a));
            }
            else
            {
                out.push_back(0); out.push_back(0); out.push_back(0); out.push_back(0);
            }
        }
    }
```

Rewrite `WriteSingleModel`'s body to use them, producing the same bytes:

```cpp
    std::vector<std::uint8_t> WriteSingleModel(const VoxModel& model)
    {
        RequireWritableSize(model.Size);

        std::vector<std::uint8_t> children;
        PushSizeChunk(children, model.Size);
        PushVoxelChunk(children, model);
        PushPaletteChunk(children, model.Colors);

        std::vector<std::uint8_t> bytes;
        PushTag(bytes, "VOX ");
        PushInt(bytes, 150);
        PushTag(bytes, "MAIN");
        PushInt(bytes, 0);
        PushInt(bytes, static_cast<std::int32_t>(children.size()));
        bytes.insert(bytes.end(), children.begin(), children.end());
        return bytes;
    }
```

Then add the tiled path:

```cpp
    //Writes a model too large for one .vox as a uniform 256 grid of tiles,
    //placed by a generated scene graph.
    std::vector<std::uint8_t> WriteTiled(const VoxModel& model)
    {
        const glm::ivec3 tileCounts(
            (model.Size.x + TileSize - 1) / TileSize,
            (model.Size.y + TileSize - 1) / TileSize,
            (model.Size.z + TileSize - 1) / TileSize);

        std::vector<std::uint8_t> children;
        std::vector<Placement> written;

        for (int tz = 0; tz < tileCounts.z; ++tz)
            for (int ty = 0; ty < tileCounts.y; ++ty)
                for (int tx = 0; tx < tileCounts.x; ++tx)
                {
                    Placement tile;
                    tile.Origin = glm::ivec3(
                        tx * TileSize, ty * TileSize, tz * TileSize);
                    tile.Size = glm::ivec3(
                        std::min(TileSize, model.Size.x - tile.Origin.x),
                        std::min(TileSize, model.Size.y - tile.Origin.y),
                        std::min(TileSize, model.Size.z - tile.Origin.z));

                    VoxModel piece;
                    if (!ExtractTile(model, tile, piece))
                        continue;

                    PushSizeChunk(children, piece.Size);
                    PushVoxelChunk(children, piece);
                    written.push_back(tile);
                }

        // A map of nothing but air still has to produce a loadable file, so
        // write one empty tile rather than a model-less one.
        if (written.empty())
        {
            Placement tile;
            tile.Origin = glm::ivec3(0);
            tile.Size = glm::ivec3(
                std::min(TileSize, model.Size.x),
                std::min(TileSize, model.Size.y),
                std::min(TileSize, model.Size.z));

            VoxModel piece;
            ExtractTile(model, tile, piece);
            PushSizeChunk(children, piece.Size);
            PushVoxelChunk(children, piece);
            written.push_back(tile);
        }

        const std::vector<std::uint8_t> graph = BuildSceneGraph(written);
        children.insert(children.end(), graph.begin(), graph.end());
        PushPaletteChunk(children, model.Colors);

        std::vector<std::uint8_t> bytes;
        PushTag(bytes, "VOX ");
        PushInt(bytes, 150);
        PushTag(bytes, "MAIN");
        PushInt(bytes, 0);
        PushInt(bytes, static_cast<std::int32_t>(children.size()));
        bytes.insert(bytes.end(), children.begin(), children.end());
        return bytes;
    }
```

Finally add the public entry point where `Write` used to be:

```cpp
std::vector<std::uint8_t> VoxWriter::Write(const VoxModel& model)
{
    //A model that fits in one .vox takes the path it always has, byte for byte.
    //Tiling is only for what that path cannot express.
    if (model.Size.x <= MaxDimension &&
        model.Size.y <= MaxDimension &&
        model.Size.z <= MaxDimension)
        return WriteSingleModel(model);

    return WriteTiled(model);
}
```

- [x] **Step 6: Run the whole suite**

Run: `"C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" Tests/Tests.vcxproj -p:Configuration=Debug -p:Platform=x64`
Expected: PASS, including the four new cases. The pre-existing `VoxWriter round-trips…` cases passing unchanged is what proves the single-model bytes did not move.

- [x] **Step 7: Commit**

```bash
git add Cubit/src/Voxel/VoxWriter.cpp Tests/src/VoxWriterTests.cpp
git commit -m "Write a model larger than 256 as placed tiles

Cuts it on a uniform 256 grid, drops all-air tiles, and emits a scene
graph placing the rest. A model that fits in one .vox still takes the
old path and produces the same bytes it always did."
```

---

### Task 6: Let a world larger than 256 be saved

`ToVoxModel` refuses any world over 256. Two existing tests assert that refusal and must go — they encode the limitation this project removes.

**Files:**
- Modify: `Cubit/src/Voxel/VoxWriter.cpp` (`ToVoxModel`)
- Modify: `Cubit/include/Cubit/Voxel/VoxWriter.h:23-36` (doc comment)
- Test: `Tests/src/WorldSaveTests.cpp:142-149` and `:169-180` (delete), plus new cases

**Interfaces:**
- Consumes: `WriteTiled` from Task 5.
- Produces: `ToVoxModel` keeps its signature and stops throwing on size.

- [x] **Step 1: Delete the two tests that assert the old limit**

Remove `TEST_CASE("A world too wide for a single .vox model is rejected")` (`Tests/src/WorldSaveTests.cpp:142-149`) and `TEST_CASE("A world too deep for a single .vox model is rejected")` (`:169-180`) in full. Keep `TEST_CASE("A world exactly 256 blocks on an axis is accepted")` — it still passes and still pins the single-model boundary.

- [x] **Step 2: Write the failing test**

Append to `Tests/src/WorldSaveTests.cpp`:

```cpp
TEST_CASE("A world past 256 blocks survives a round trip through .vox bytes")
{
    // 17 chunks on x and z is 272 blocks: over the single-model limit, so this
    // goes out as tiles, and the far edge is a 16-wide remainder tile — which
    // is where a halving mistake in the placement would show up.
    World world(17, 1, 17);
    world.SetBlock(0, 0, 0, 1);
    world.SetBlock(271, 5, 271, 2);
    world.SetBlock(255, 0, 256, 3);
    world.SetBlock(256, 0, 255, 4);

    const World back = BuildWorld(VoxLoader::Parse(VoxWriter::Write(ToVoxModel(world))));

    CHECK(back.GetWidth() == world.GetWidth());
    CHECK(back.GetDepth() == world.GetDepth());
    CHECK(back.GetBlock(0, 0, 0) == 1);
    CHECK(back.GetBlock(271, 5, 271) == 2);
    CHECK(back.GetBlock(255, 0, 256) == 3);
    CHECK(back.GetBlock(256, 0, 255) == 4);
    CHECK(back.GetBlock(100, 0, 100) == 0);
}
```

Confirm `WorldSaveTests.cpp` includes `"Cubit/Voxel/VoxLoader.h"`; add it if not.

- [x] **Step 3: Run to verify it fails**

Run: `./bin/Debug-windows-x86_64/Tests/Tests.exe -tc="A world past 256 blocks survives a round trip through .vox bytes"`
Expected: FAIL — `ToVoxModel` throws `"vox: world is too large for a single .vox model (x = 272)"`.

- [x] **Step 4: Drop the check from `ToVoxModel`**

In `Cubit/src/Voxel/VoxWriter.cpp`, delete the `RequireWritableSize(model.Size);` line from `ToVoxModel`. The size limit belongs to a single written model, which is now `WriteSingleModel`'s business.

- [x] **Step 5: Correct the header comment**

Replace lines 32-35 of `Cubit/include/Cubit/Voxel/VoxWriter.h`:

```cpp
//Throws when the world is larger than 256 on any axis, which a single .vox
//model cannot address. Splitting such a world across several models is
//multi-model stitching, which does not exist yet.
```

with:

```cpp
//Any size of world converts. A world larger than 256 on an axis is more than a
//single .vox model can address, so VoxWriter::Write splits it into a grid of
//placed models; that is the writer's business, not this function's.
```

- [x] **Step 6: Run the whole suite**

Run: `"C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" Tests/Tests.vcxproj -p:Configuration=Debug -p:Platform=x64`
Expected: PASS.

- [x] **Step 7: Commit**

```bash
git add Cubit/src/Voxel/VoxWriter.cpp Cubit/include/Cubit/Voxel/VoxWriter.h Tests/src/WorldSaveTests.cpp
git commit -m "Let a world larger than one .vox model be saved

The 256 limit belongs to a single written model, not to the world.
Removes the two tests that asserted the old refusal."
```

---

### Task 7: Give MapGen a size argument

**Files:**
- Modify: `MapGen/src/MapGen.cpp:11-31`

**Interfaces:**
- Consumes: nothing from earlier tasks.
- Produces: `MapGen.exe [out.vox] [--size W H D]`.

- [x] **Step 1: Add the argument**

Replace the body of `main` in `MapGen/src/MapGen.cpp` (lines 11-31) with:

```cpp
int main(int argc, char** argv)
{
    std::string out = "battlefield.vox";
    TerrainConfig config;
    config.Size = glm::ivec3(256, 64, 256);

    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];

        if (arg == "--size")
        {
            // Cubit axes: width, Y-up height, depth. Anything larger than 256
            // on an axis is written as several stitched models.
            if (i + 3 >= argc)
            {
                std::cerr << "--size needs three values: W H D\n";
                return 1;
            }
            config.Size = glm::ivec3(
                std::atoi(argv[i + 1]), std::atoi(argv[i + 2]), std::atoi(argv[i + 3]));
            i += 3;

            if (config.Size.x < 1 || config.Size.y < 1 || config.Size.z < 1)
            {
                std::cerr << "--size values must be positive\n";
                return 1;
            }
        }
        else
        {
            out = arg;
        }
    }

    const VoxModel model = TerrainGen::Generate(config);
    const std::vector<std::uint8_t> bytes = VoxWriter::Write(model);

    std::ofstream file(out, std::ios::binary);
    if (!file)
    {
        std::cerr << "Cannot open output: " << out << "\n";
        return 1;
    }
    file.write(reinterpret_cast<const char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size()));

    std::cout << "Wrote " << out << " ("
        << config.Size.x << "x" << config.Size.y << "x" << config.Size.z
        << ", " << bytes.size() << " bytes)\n";
    return 0;
}
```

Add `#include <cstdlib>` to the include block.

- [x] **Step 2: Build MapGen**

Run: `"C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" MapGen/MapGen.vcxproj -p:Configuration=Debug -p:Platform=x64`
Expected: builds clean.

- [x] **Step 3: Verify the default is unchanged**

Run: `./bin/Debug-windows-x86_64/MapGen/MapGen.exe /tmp-check.vox` — use a scratch path, not the shipped map.
Expected: reports `256x64x256`. Compare its byte size against `Sandbox/assets/maps/battlefield.vox`; they should match, since the generator is deterministic on its seed and nothing about the 256 path changed. Delete the scratch file afterwards.

- [x] **Step 4: Commit**

```bash
git add MapGen/src/MapGen.cpp
git commit -m "Let MapGen generate a map of any size

--size W H D in Cubit axes, defaulting to the 256x64x256 the tool has
always produced."
```

---

### Task 8: Generate the 512 map and verify it in the Sandbox

The seam between tiles is what unit tests are least able to check. This task is the one that finds it.

**Files:**
- Create: `Sandbox/assets/maps/battlefield512.vox`
- Modify: `Sandbox/src/Sandbox.cpp:380` (the map path)

**Interfaces:**
- Consumes: everything above.
- Produces: nothing code-facing.

- [x] **Step 1: Generate the map**

Run: `./bin/Debug-windows-x86_64/MapGen/MapGen.exe Sandbox/assets/maps/battlefield512.vox --size 512 64 512`
Expected: reports `512x64x512`. This is a Debug build generating 16.7M cells — allow it a minute.

- [x] **Step 2: Confirm it loads back before touching the Sandbox**

Add a temporary case to `Tests/src/VoxLoaderTests.cpp`:

```cpp
TEST_CASE("The 512 battlefield loads back at full size")
{
    const std::filesystem::path path = "Sandbox/assets/maps/battlefield512.vox";
    if (!std::filesystem::exists(path))
        return; // asset not reachable from this working directory; skip

    const VoxModel model = VoxLoader::LoadFile(path.string());
    CHECK(model.Size == glm::ivec3(512, 64, 512));
}
```

Run the suite. Expected: PASS. **Keep this case** — it is the only test that exercises a real stitched file end to end.

- [x] **Step 3: Point the Sandbox at it**

In `Sandbox/src/Sandbox.cpp`, change the map path at line 380 from `battlefield.vox` to `battlefield512.vox`. Read the surrounding lines first: the path may be a named constant rather than a literal at the call site.

- [x] **Step 4: Build and run the Sandbox**

Build the Sandbox project, then launch it. Per `docs/superpowers/` practice, screenshot the GLFW30 window — **not** `MainWindowHandle` — and close via `WM_CLOSE` so the log survives.

Check, in order:
1. The map renders and the HUD chunk count is roughly 4096 total.
2. **Walk to x=256 and to z=256.** There must be no wall, trench, gap or lighting seam along either. This is the failure the whole design turns on.
3. Press `F5` to save, then `F9` to reload, and confirm the map comes back identical.

- [x] **Step 5: Record the load numbers**

From the run's log, note the wall-clock time to first frame and the time until the chunk count stops rising. These are the measured replacements for the extrapolation in the spec.

- [x] **Step 6: Commit**

```bash
git add Sandbox/assets/maps/battlefield512.vox Sandbox/src/Sandbox.cpp Tests/src/VoxLoaderTests.cpp
git commit -m "Ship a 512x64x512 battlefield

Four stitched models. The Sandbox loads it in place of the 256 map."
```

---

### Task 9: Update the docs

**Files:**
- Modify: `docs/engine-roadmap.md:64-65` and `:87`
- Modify: `docs/performance.md` (summary table and the load-time note at line ~250)

- [x] **Step 1: Mark the roadmap item done**

In `docs/engine-roadmap.md`, change item 6 under "Format / smaller gaps" from:

```markdown
6. **Multi-model stitching** — load maps larger than 256³ (true Ace-of-Spades
   512×512 scale needs several stitched `.vox` models).
```

to a `~~struck-through~~` **DONE 2026-08-10** entry in the style of the items around it, naming what shipped: the loader flattens placed models into one dense `VoxModel`, the writer cuts one larger than 256 into a placed 256 grid, and `battlefield512.vox` is 512×64×512. Do the same for arc item 5 at line 87, and update the `_Last updated:_` date at the top.

Then add a line stating that the finish-the-engine arc is complete and the remaining engine gap is the **camera aim API** (item 8), since that becomes the next item.

- [x] **Step 2: Record the measured load cost**

In `docs/performance.md`, replace the extrapolated sentence at the end of "Where an edit stands now" with the **measured** figures from Task 8 Step 5 for the 512 map, and add a line to the summary table for the load-time problem: `SkyLight::PropagateAll` plus whole-world meshing at load, priority now high, status open, fix is threading. Update `_Last updated:_`.

- [x] **Step 3: Note the fort-scale follow-up**

Add to `docs/engine-roadmap.md`, under a gameplay or follow-up heading: on a 512 map the forts stay `FortEdgeOffset = 8` from the edge at `FortRadius = 5`, so they are two small specks 496 apart. Tuning them is gameplay work, deliberately not done with stitching.

- [x] **Step 4: Commit**

```bash
git add docs/engine-roadmap.md docs/performance.md
git commit -m "Record multi-model stitching as shipped

Closes the finish-the-engine arc and replaces the extrapolated 512 load
cost with the measured one."
```

---

## Self-Review

**Spec coverage.** Read path → Tasks 1–4 (models list, node chunks, placement, guards). Write path → Task 5 (tiling, graph, byte-identical single model, empty-tile skipping) and Task 6 (`ToVoxModel`). The 512 map → Tasks 7–8. Testing → each task's own cases, with the hand-authored byte fixtures in Tasks 1–4 and the round trips in Tasks 5–6. Verification → Task 8. Out-of-scope load time → measured in Task 8 Step 5, written down in Task 9.

**One spec deviation, deliberate.** The spec said the identity rotation constant would be verified against a MagicaVoxel-authored file. There is no such file in the repo and MagicaVoxel is not installed, so Task 4 instead documents *why the risk is acceptable*: a wrong constant makes the loader **refuse** a file it should accept, never misplace one. That failure is loud and fixable on first encounter. If a MagicaVoxel file turns up later, the `"4"` case in Task 4 Step 1 is where to confirm it.

**Placeholders.** None. Every code step carries the code.

**Type consistency.** `RawVoxel`/`RawModel` (Task 1) are used by `ResolvePlacement` (Task 3). `SceneNode` and `nodes` (Task 2) are consumed by Task 3. `Placement` is the writer's own struct (Task 5) and is distinct from the loader's `PlacedModel` (Task 3) and from the tests' `GraphPlacement` (Task 2) — three different types, three different names, none crossing a translation unit. `PushSizeChunk`/`PushVoxelChunk`/`PushPaletteChunk` are introduced in Task 5 Step 5 and used by both `WriteSingleModel` and `WriteTiled` in that same step.
