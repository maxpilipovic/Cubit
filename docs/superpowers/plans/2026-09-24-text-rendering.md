# Text rendering (B4) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Real text — the engine bakes a TrueType font into an atlas and draws with it, so the game's HUD can spell words instead of shouting in the debug font's 22 capitals.

**Architecture:** The engine gains `FontAtlas` (pure data: baked pixels and per-glyph metrics, testable without a GL context) and `Font` (that atlas uploaded as a `Texture2D`), on the same split as `MeshGeometry`/`Mesh`. `ScreenOverlay` gains a font-aware `DrawText` and a `MeasureText`. The game ships the font file as its asset; the harness keeps the code-defined debug font, so the engine still needs nothing from `game/`.

**Tech Stack:** C++20, MSVC, premake5, doctest, `stb_truetype.h` v1.26 (public domain).

**Spec:** `docs/superpowers/specs/2026-09-24-text-rendering-design.md`

## Global Constraints

- Build: `"C:/Program Files/Microsoft Visual Studio/18/Community/MSBuild/Current/Bin/MSBuild.exe" Cubit.slnx -p:Configuration=Debug -p:Platform=x64 -v:minimal -nologo` from the repo root. Both test executables run as post-build steps, so a failing test fails the build.
- **Adding any source file needs the projects regenerated:** `C:\dev\premake\premake5.exe vs2026` from the repo root. **Never run `GenerateProjects.bat`** — it deletes `bin/`.
- Green in Debug **and** Release, then commit and push to `master`. **No Claude co-author trailers or attribution in commits.**
- Comments explain WHY, in the voice of the surrounding code: `//` comments, four-space indent, `m_` members, PascalCase functions.
- **The engine must not include a header from `game/` or `Sandbox/`, and must not read anything under `game/`.** The engine's tests bake from `Tests/fixtures/`, never from `game/assets/`.
- **No stb type may appear in a public engine header.** The game compiles against `Cubit/include` only and must never need `vendor/`. `stb_truetype.h` is included by `FontAtlas.cpp` and nowhere else.
- Test paths are never relative to the working directory: use `FixturePath` (engine, `CB_TEST_FIXTURES`) or `CB_GAME_ASSETS` (game).
- **doctest's `CHECK` line citations in this repo are unreliable** — they can name lines that do not exist. Navigate failures by test case NAME. doctest's headline "test cases: N" counts only cases that RAN; a `skip()`-marked case is reported separately and is additional.
- Starting counts: engine **602** registered (601 run, 1 skipped by design), game **29**.

## Two conventions this plan turns on

**1. The overlay's y axis points up.** `ScreenOverlay`'s camera is `(0, width, 0, height)` — left, right, bottom, top — so y increases upward with the origin at the bottom left of the screen. `stb_truetype` reports glyph offsets with y increasing downward from the top, so the conversion happens once, at bake time, inside `FontAtlas.cpp`.

**2. Texture row zero is the bottom of the quad.** `DebugFont::CreateTexture` says so in its own comment and writes the top row of a glyph to the highest pixel row. The overlay's quad maps `v=0` to the quad's bottom edge. `stbtt_BakeFontBitmap` fills its bitmap top row first, so **the baked bitmap is flipped vertically at bake time** and the glyph UVs are computed against the flipped image. Get this wrong and every glyph draws upside down.

## File structure

| File | Task | Responsibility |
|---|---|---|
| `vendor/stb/stb_truetype.h` | 1 | The baker. Copied in, never edited. |
| `Cubit/include/Cubit/Renderer/FontAtlas.h`, `Cubit/src/Renderer/FontAtlas.cpp` | 1 | Bake a TTF to pixels + metrics. No GL. |
| `Tests/fixtures/CascadiaMono.ttf`, `Tests/fixtures/OFL.txt` | 1 | The engine suite's own font, so it never reads `game/`. |
| `Tests/src/FontAtlasTests.cpp` | 1 | The baking contract. |
| `Cubit/include/Cubit/Renderer/Font.h`, `Cubit/src/Renderer/Font.cpp` | 2 | Atlas + `Texture2D`. GL, so no unit test. |
| `Cubit/include/Cubit/Renderer/ScreenOverlay.h`, `Cubit/src/Renderer/ScreenOverlay.cpp` | 2 | Font-aware `DrawText` and `MeasureText`. |
| `game/assets/fonts/CascadiaMono.ttf`, `game/assets/fonts/OFL.txt` | 3 | The font the game ships. |
| `game/GameApp/src/GameApp.cpp`, `game/Game/src/GameHudLayer.h` | 3 | Load it once; HUD draws with it. |
| `game/GameTests/src/FontTests.cpp`, `game/GameTests/src/GameHudTests.cpp` | 3 | The shipped asset is usable; retire the obsolete debug-font check. |
| `README.md`, `docs/engine-roadmap.md` | 4 | Record it, tick B4, open B4a. |

---

### Task 1: `FontAtlas` — baking, without a GL context

**Files:**
- Create: `vendor/stb/stb_truetype.h` (copy)
- Create: `Cubit/include/Cubit/Renderer/FontAtlas.h`, `Cubit/src/Renderer/FontAtlas.cpp`
- Create: `Tests/fixtures/CascadiaMono.ttf`, `Tests/fixtures/OFL.txt` (copies)
- Create: `Tests/src/FontAtlasTests.cpp`
- Modify: `Cubit/premake5.lua` (add `"../vendor/stb"` to `includedirs`)

**Interfaces:**
- Consumes: nothing.
- Produces:
  - `struct FontAtlas::Glyph { glm::vec2 Uv0; glm::vec2 Uv1; glm::vec2 Size; glm::vec2 Bearing; float Advance; };`
  - `static FontAtlas FontAtlas::FromTrueType(std::span<const std::uint8_t> ttf, float pixelHeight)`
  - `static FontAtlas FontAtlas::FromFile(const std::string& path, float pixelHeight)`
  - `const Glyph& GlyphFor(char) const`, `float LineHeight() const`, `float Measure(std::string_view) const`
  - `const std::vector<std::uint8_t>& Pixels() const`, `std::uint32_t Width() const`, `std::uint32_t Height() const`
  - `static constexpr char FirstCharacter = ' '; static constexpr char LastCharacter = '~';`

- [ ] **Step 1: Copy in the third-party files**

```bash
mkdir -p vendor/stb Tests/fixtures
cp /c/dev/Minecraft/vendor/stb/stb_truetype.h vendor/stb/stb_truetype.h
cp /c/Windows/Fonts/CascadiaMono.ttf Tests/fixtures/CascadiaMono.ttf
curl -fsSL https://raw.githubusercontent.com/microsoft/cascadia-code/main/LICENSE -o Tests/fixtures/OFL.txt
sha256sum vendor/stb/stb_truetype.h Tests/fixtures/CascadiaMono.ttf Tests/fixtures/OFL.txt
```

Expected, exactly — if any differs, stop and report rather than continuing:

```
2d119d54c88197764a6494c3c79fb6cdb4bd3e8d6708c82b57cbb508b8a70090  vendor/stb/stb_truetype.h
0e141cb99609f6f10ad05313fd1807d5cc9e28658dcbb35ab162e52ff67dc718  Tests/fixtures/CascadiaMono.ttf
51882cd3cdba4e16f220f44ddb08a635c38c44ea6e0975db2574f4be6f958238  Tests/fixtures/OFL.txt
```

The font is Cascadia Mono, under the SIL Open Font License 1.1. `OFL.txt` is that license and **must** sit beside the font: the license permits bundling only when it travels with the font.

- [ ] **Step 2: Add the include path**

In `Cubit/premake5.lua`, add `"../vendor/stb"` to the `includedirs` list, after `"../vendor/ENet/include"`.

- [ ] **Step 3: Write the failing tests**

Create `Tests/src/FontAtlasTests.cpp`:

```cpp
#include <doctest.h>

#include "TestMaps.h"

#include "Cubit/Renderer/FontAtlas.h"

#include <stdexcept>
#include <string>
#include <vector>

namespace
{
    //Baked once for the whole file: every case below reads the same atlas, and
    //baking a real font per case is the slowest thing in this suite.
    const FontAtlas& Baked()
    {
        static const FontAtlas atlas =
            FontAtlas::FromFile(FixturePath("CascadiaMono.ttf").string(), 32.0f);

        return atlas;
    }
}

TEST_CASE("Every printable character bakes to a glyph that advances the pen")
{
    const FontAtlas& atlas = Baked();

    for (char character = FontAtlas::FirstCharacter;
         character <= FontAtlas::LastCharacter; ++character)
    {
        CAPTURE(static_cast<int>(character));
        CHECK(atlas.GlyphFor(character).Advance > 0.0f);
    }
}

TEST_CASE("A character the font does not cover draws a question mark, not nothing")
{
    //The failure this replaces: the debug font draws an unsupported character
    //as a blank, so a readout silently loses the value it was added to show.
    const FontAtlas& atlas = Baked();

    const FontAtlas::Glyph& missing = atlas.GlyphFor('\t');
    const FontAtlas::Glyph& question = atlas.GlyphFor('?');

    CHECK(missing.Advance == doctest::Approx(question.Advance));
    CHECK(missing.Uv0.x == doctest::Approx(question.Uv0.x));
    CHECK(missing.Uv0.y == doctest::Approx(question.Uv0.y));
}

TEST_CASE("Measuring a string adds up its glyphs' advances")
{
    const FontAtlas& atlas = Baked();

    const float expected =
        atlas.GlyphFor('C').Advance +
        atlas.GlyphFor('u').Advance +
        atlas.GlyphFor('b').Advance;

    CHECK(atlas.Measure("Cub") == doctest::Approx(expected));
    CHECK(atlas.Measure("") == doctest::Approx(0.0f));
}

TEST_CASE("The atlas is a real bitmap of the size it reports")
{
    const FontAtlas& atlas = Baked();

    CHECK(atlas.Width() > 0);
    CHECK(atlas.Height() > 0);
    CHECK(atlas.Pixels().size()
        == static_cast<std::size_t>(atlas.Width()) * atlas.Height());
}

TEST_CASE("Something was actually drawn into the atlas")
{
    //A bake that produced an empty bitmap would satisfy every other case here:
    //the metrics would still be there and the sizes would still add up.
    const FontAtlas& atlas = Baked();

    std::size_t covered = 0;
    for (const std::uint8_t value : atlas.Pixels())
        if (value != 0)
            ++covered;

    CHECK(covered > 0);
}

TEST_CASE("A line of text is taller than the glyphs on it")
{
    const FontAtlas& atlas = Baked();

    CHECK(atlas.LineHeight() > atlas.GlyphFor('M').Size.y);
}

TEST_CASE("Baking the same font twice gives the same metrics")
{
    //Text that shifted between runs would make every screenshot comparison
    //and every layout measurement unreliable.
    const FontAtlas first =
        FontAtlas::FromFile(FixturePath("CascadiaMono.ttf").string(), 32.0f);
    const FontAtlas second =
        FontAtlas::FromFile(FixturePath("CascadiaMono.ttf").string(), 32.0f);

    CHECK(first.LineHeight() == doctest::Approx(second.LineHeight()));
    CHECK(first.Measure("Cubit") == doctest::Approx(second.Measure("Cubit")));
    CHECK(first.Pixels() == second.Pixels());
}

TEST_CASE("Bytes that are not a font are refused rather than baked")
{
    const std::vector<std::uint8_t> nonsense(256, 0x7F);

    CHECK_THROWS_AS(FontAtlas::FromTrueType(nonsense, 32.0f), std::runtime_error);
}

TEST_CASE("A font file that is not there is an error, not an empty atlas")
{
    CHECK_THROWS_AS(
        FontAtlas::FromFile("no/such/font.ttf", 32.0f), std::runtime_error);
}
```

- [ ] **Step 4: Add the header, and a stub so the tests compile and fail**

Create `Cubit/include/Cubit/Renderer/FontAtlas.h`:

```cpp
#pragma once

#include "Cubit/Core.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4251)
#endif

//A TrueType font baked into one bitmap, with the measurements needed to draw
//from it. Holds no GL object, so it can be built and tested without a window -
//the same split as MeshGeometry against Mesh.
class CB_API FontAtlas
{
public:
    //Where one character sits in the atlas and how it sits on the line.
    //
    //Measured in the overlay's own axes: y points UP, and Bearing.y is the
    //distance from the baseline up to the glyph's BOTTOM edge, so a descender
    //like g is negative. stb_truetype counts the other way, and converting it
    //once here is what keeps that out of every caller.
    struct Glyph
    {
        glm::vec2 Uv0{ 0.0f };
        glm::vec2 Uv1{ 0.0f };
        glm::vec2 Size{ 0.0f };
        glm::vec2 Bearing{ 0.0f };
        float Advance = 0.0f;
    };

    //Printable ASCII, and nothing else: the game writes English and a bake of
    //every codepoint would cost an atlas far larger than a HUD needs.
    static constexpr char FirstCharacter = ' ';
    static constexpr char LastCharacter = '~';
    static constexpr int CharacterCount = LastCharacter - FirstCharacter + 1;

    //Bakes the font at a pixel height. Throws std::runtime_error when the bytes
    //are not a font, or when the glyphs do not fit the atlas - a truncated bake
    //would surface as missing letters much later and far from the cause.
    static FontAtlas FromTrueType(std::span<const std::uint8_t> ttf, float pixelHeight);

    //Reads a .ttf and bakes it, mirroring VoxLoader::LoadFile. Throws when the
    //file cannot be read.
    static FontAtlas FromFile(const std::string& path, float pixelHeight);

    //The glyph for a character, or the one for '?' when it is outside the baked
    //range. Never a blank: silently drawing nothing is the failure this whole
    //type exists to replace.
    const Glyph& GlyphFor(char character) const;

    //Baseline to baseline, including the font's own line gap.
    float LineHeight() const { return m_LineHeight; }

    //Width of the text in pixels at the baked height, which is what centring
    //and right-aligning are arithmetic on.
    float Measure(std::string_view text) const;

    //Tightly packed 8-bit coverage. Row zero is the BOTTOM row, matching the
    //engine's texture convention - see DebugFont::CreateTexture.
    const std::vector<std::uint8_t>& Pixels() const { return m_Pixels; }
    std::uint32_t Width() const { return m_Width; }
    std::uint32_t Height() const { return m_Height; }

private:
    std::vector<std::uint8_t> m_Pixels;
    std::uint32_t m_Width = 0;
    std::uint32_t m_Height = 0;
    float m_LineHeight = 0.0f;
    std::vector<Glyph> m_Glyphs;
};

#ifdef _MSC_VER
#pragma warning(pop)
#endif
```

Create `Cubit/src/Renderer/FontAtlas.cpp` as a stub that bakes nothing:

```cpp
#include "cub.h"

#include "Cubit/Renderer/FontAtlas.h"

FontAtlas FontAtlas::FromTrueType(std::span<const std::uint8_t> ttf, float pixelHeight)
{
    (void)ttf;
    (void)pixelHeight;
    return FontAtlas();
}

FontAtlas FontAtlas::FromFile(const std::string& path, float pixelHeight)
{
    (void)path;
    (void)pixelHeight;
    return FontAtlas();
}

const FontAtlas::Glyph& FontAtlas::GlyphFor(char character) const
{
    (void)character;
    static const Glyph empty;
    return empty;
}

float FontAtlas::Measure(std::string_view text) const
{
    (void)text;
    return 0.0f;
}
```

- [ ] **Step 5: Regenerate, build, and confirm the red**

Run `C:\dev\premake\premake5.exe vs2026`, then the Debug build.

Expected: the build fails on the post-build test step. Of the nine new cases, **two pass against the stub** — "A character the font does not cover draws a question mark, not nothing" (every glyph is the same empty one, so they match) and "Baking the same font twice gives the same metrics" (two empty atlases agree). The other seven fail. Step 8 proves the first of those two binds.

To see which cases failed: `cd Tests && ../bin/Debug-windows-x86_64/Tests/Tests.exe -sf="*FontAtlasTests.cpp"`.

- [ ] **Step 6: Implement the bake**

Replace `Cubit/src/Renderer/FontAtlas.cpp` with:

```cpp
#include "cub.h"

#include "Cubit/Renderer/FontAtlas.h"

#include <fstream>
#include <stdexcept>

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

namespace
{
    //Square, and big enough for printable ASCII at the sizes a HUD uses. A bake
    //that does not fit is refused rather than truncated, so this being too
    //small shows up as an error at load and not as missing letters on screen.
    constexpr int AtlasSize = 512;
}

FontAtlas FontAtlas::FromTrueType(std::span<const std::uint8_t> ttf, float pixelHeight)
{
    if (ttf.empty())
        throw std::runtime_error("font: no bytes to bake");

    stbtt_fontinfo info;
    const int offset = stbtt_GetFontOffsetForIndex(ttf.data(), 0);
    if (offset < 0 || !stbtt_InitFont(&info, ttf.data(), offset))
        throw std::runtime_error("font: not a TrueType font");

    FontAtlas atlas;
    atlas.m_Width = AtlasSize;
    atlas.m_Height = AtlasSize;

    std::vector<std::uint8_t> baked(
        static_cast<std::size_t>(AtlasSize) * AtlasSize, 0);
    std::vector<stbtt_bakedchar> characters(CharacterCount);

    //Negative means the glyphs did not fit; positive is how many rows were used.
    const int result = stbtt_BakeFontBitmap(
        ttf.data(), 0, pixelHeight, baked.data(), AtlasSize, AtlasSize,
        FirstCharacter, CharacterCount, characters.data());

    if (result <= 0)
        throw std::runtime_error("font: does not fit the atlas at this size");

    //stb fills its bitmap top row first; the engine's textures start at the
    //bottom row - see DebugFont::CreateTexture - so flip it once here rather
    //than flipping every glyph's coordinates at every draw.
    atlas.m_Pixels.resize(baked.size());
    for (int row = 0; row < AtlasSize; ++row)
    {
        const std::size_t source = static_cast<std::size_t>(row) * AtlasSize;
        const std::size_t destination =
            static_cast<std::size_t>(AtlasSize - 1 - row) * AtlasSize;

        std::copy_n(baked.begin() + source, AtlasSize,
            atlas.m_Pixels.begin() + destination);
    }

    const float size = static_cast<float>(AtlasSize);

    atlas.m_Glyphs.resize(CharacterCount);
    for (int i = 0; i < CharacterCount; ++i)
    {
        const stbtt_bakedchar& character = characters[i];
        Glyph& glyph = atlas.m_Glyphs[i];

        const float height =
            static_cast<float>(character.y1) - static_cast<float>(character.y0);

        //The rows moved when the bitmap was flipped, so the glyph's top edge in
        //stb's image is its bottom edge in ours.
        glyph.Uv0 = glm::vec2(
            static_cast<float>(character.x0) / size,
            (size - static_cast<float>(character.y1)) / size);
        glyph.Uv1 = glm::vec2(
            static_cast<float>(character.x1) / size,
            (size - static_cast<float>(character.y0)) / size);

        glyph.Size = glm::vec2(
            static_cast<float>(character.x1) - static_cast<float>(character.x0),
            height);

        //yoff is the distance from the baseline DOWN to the glyph's top edge,
        //so the distance from the baseline UP to its bottom edge is the
        //negation of the far edge.
        glyph.Bearing = glm::vec2(character.xoff, -(character.yoff + height));
        glyph.Advance = character.xadvance;
    }

    int ascent = 0;
    int descent = 0;
    int lineGap = 0;
    stbtt_GetFontVMetrics(&info, &ascent, &descent, &lineGap);

    atlas.m_LineHeight =
        static_cast<float>(ascent - descent + lineGap) *
        stbtt_ScaleForPixelHeight(&info, pixelHeight);

    return atlas;
}

FontAtlas FontAtlas::FromFile(const std::string& path, float pixelHeight)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file)
        throw std::runtime_error("font: cannot open file: " + path);

    const std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    if (!file.read(reinterpret_cast<char*>(bytes.data()), size))
        throw std::runtime_error("font: cannot read file: " + path);

    return FromTrueType(bytes, pixelHeight);
}

const FontAtlas::Glyph& FontAtlas::GlyphFor(char character) const
{
    const int index = static_cast<int>(character) - FirstCharacter;

    if (index < 0 || index >= static_cast<int>(m_Glyphs.size()))
        return GlyphFor('?');

    return m_Glyphs[static_cast<std::size_t>(index)];
}

float FontAtlas::Measure(std::string_view text) const
{
    float width = 0.0f;
    for (const char character : text)
        width += GlyphFor(character).Advance;

    return width;
}
```

`#include <algorithm>` is needed for `std::copy_n`; add it beside `<fstream>`.

- [ ] **Step 7: Build Debug and confirm green**

Expected: engine **611** registered (602 + 9), 610 run, 1 skipped; game 29.

- [ ] **Step 8: Prove the fallback case binds**

Temporarily make `GlyphFor` return `m_Glyphs[0]` (the space) instead of `GlyphFor('?')` for an out-of-range character. Rebuild and run the file's cases: "A character the font does not cover draws a question mark, not nothing" must fail. Restore, rebuild, green.

- [ ] **Step 9: Build Release, then commit**

```bash
git add vendor/stb Tests/fixtures Cubit/include/Cubit/Renderer/FontAtlas.h Cubit/src/Renderer/FontAtlas.cpp Cubit/premake5.lua Tests/src/FontAtlasTests.cpp
git commit -m "Bake a TrueType font into an atlas the engine can measure"
git push origin master
```

---

### Task 2: `Font` and drawing with it

**Files:**
- Create: `Cubit/include/Cubit/Renderer/Font.h`, `Cubit/src/Renderer/Font.cpp`
- Modify: `Cubit/include/Cubit/Renderer/ScreenOverlay.h`, `Cubit/src/Renderer/ScreenOverlay.cpp`

**Interfaces:**
- Consumes: `FontAtlas`, `FontAtlas::Glyph`, `GlyphFor`, `LineHeight`, `Measure`, `Pixels`, `Width`, `Height` (Task 1).
- Produces:
  - `explicit Font(const FontAtlas& atlas)`; `const Texture2D& Texture() const`; `const FontAtlas::Glyph& GlyphFor(char) const`; `float LineHeight() const`; `float Measure(std::string_view) const`; `const FontAtlas& Atlas() const`
  - `void ScreenOverlay::DrawText(const Font&, std::string_view, float x, float y, float scale = 1.0f, const glm::vec4& colour = glm::vec4(1.0f)) const`
  - `static float ScreenOverlay::MeasureText(const Font&, std::string_view, float scale = 1.0f)`

- [ ] **Step 1: Add `Font`**

Create `Cubit/include/Cubit/Renderer/Font.h`:

```cpp
#pragma once

#include "Cubit/Renderer/FontAtlas.h"
#include "Cubit/Renderer/Texture2D.h"

#include <memory>
#include <string_view>

//A baked font on the GPU: a FontAtlas plus the texture it was uploaded into.
//
//Needs a live GL context, so one of these is built from a layer and never
//before the window exists - the same rule as Mesh, and the reason neither has
//a unit test.
class CB_API Font
{
public:
    //Uploads the atlas's coverage as an RGBA texture, white where the glyph is.
    explicit Font(const FontAtlas& atlas);

    //Keeps the atlas rather than copying its measurements out, so a glyph is
    //described in exactly one place.
    const FontAtlas& Atlas() const { return m_Atlas; }
    const Texture2D& Texture() const { return *m_Texture; }

    const FontAtlas::Glyph& GlyphFor(char character) const
    {
        return m_Atlas.GlyphFor(character);
    }

    float LineHeight() const { return m_Atlas.LineHeight(); }
    float Measure(std::string_view text) const { return m_Atlas.Measure(text); }

private:
    FontAtlas m_Atlas;
    std::unique_ptr<Texture2D> m_Texture;
};
```

Create `Cubit/src/Renderer/Font.cpp`:

```cpp
#include "cub.h"

#include "Cubit/Renderer/Font.h"

#include <vector>

Font::Font(const FontAtlas& atlas)
    : m_Atlas(atlas)
{
    //Texture2D takes RGBA, and the bake is one coverage byte per pixel. White
    //everywhere, with the coverage as alpha, so a tint at draw time decides the
    //colour and the same font serves a white readout and a red warning.
    std::vector<std::uint8_t> rgba(
        static_cast<std::size_t>(m_Atlas.Pixels().size()) * 4);

    for (std::size_t i = 0; i < m_Atlas.Pixels().size(); ++i)
    {
        rgba[i * 4 + 0] = 255;
        rgba[i * 4 + 1] = 255;
        rgba[i * 4 + 2] = 255;
        rgba[i * 4 + 3] = m_Atlas.Pixels()[i];
    }

    m_Texture = std::make_unique<Texture2D>(
        m_Atlas.Width(), m_Atlas.Height(), rgba.data());
}
```

- [ ] **Step 2: Draw with it**

In `ScreenOverlay.h`, add `#include "Cubit/Renderer/Font.h"` beside the existing includes, and these members after the existing `DrawText`:

```cpp
    //Draws a line of text in a real font. `x, y` is the pen on the BASELINE, in
    //the overlay's y-up pixel space - deliberately not the same as the debug
    //font's DrawText above, whose y is the bottom of a fixed glyph cell. A real
    //font has descenders, so a baseline is the only origin that makes sense.
    void DrawText(const Font& font, std::string_view text, float x, float y,
        float scale = 1.0f, const glm::vec4& colour = glm::vec4(1.0f)) const;

    //Width of that text if it were drawn, for centring and right-aligning.
    static float MeasureText(const Font& font, std::string_view text,
        float scale = 1.0f);
```

In `ScreenOverlay.cpp`:

```cpp
void ScreenOverlay::DrawText(const Font& font, std::string_view text, float x, float y,
    float scale, const glm::vec4& colour) const
{
    float pen = x;

    for (const char character : text)
    {
        const FontAtlas::Glyph& glyph = font.GlyphFor(character);

        //A space has an advance and no pixels. Skipping it saves a draw call
        //and, more importantly, saves sampling a zero-area region of the atlas.
        if (glyph.Size.x > 0.0f && glyph.Size.y > 0.0f)
        {
            DrawQuad(
                font.Texture(),
                pen + glyph.Bearing.x * scale,
                y + glyph.Bearing.y * scale,
                glyph.Size.x * scale,
                glyph.Size.y * scale,
                glyph.Uv0,
                glyph.Uv1 - glyph.Uv0,
                colour);
        }

        pen += glyph.Advance * scale;
    }
}

float ScreenOverlay::MeasureText(const Font& font, std::string_view text, float scale)
{
    return font.Measure(text) * scale;
}
```

- [ ] **Step 3: Build Debug and Release green**

Expected: engine 611 registered / 610 run / 1 skipped, game 29 — unchanged by this task. `Font` and the new `DrawText` have no unit test: they need a live GL context, the same precedent as `Mesh` and `VertexArray`. Task 3 is where they are seen working.

- [ ] **Step 4: Commit**

```bash
git add Cubit/include/Cubit/Renderer/Font.h Cubit/src/Renderer/Font.cpp Cubit/include/Cubit/Renderer/ScreenOverlay.h Cubit/src/Renderer/ScreenOverlay.cpp
git commit -m "Draw text in a real font, measured for layout"
git push origin master
```

---

### Task 3: The game ships a font and its HUD uses it

**Files:**
- Create: `game/assets/fonts/CascadiaMono.ttf`, `game/assets/fonts/OFL.txt` (copies)
- Create: `game/GameTests/src/FontTests.cpp`
- Modify: `game/Game/src/GameHudLayer.h`, `game/GameApp/src/GameApp.cpp`
- Modify: `game/GameTests/src/GameHudTests.cpp` (retire one case)

**Interfaces:**
- Consumes: `FontAtlas::FromFile`, `Font`, `ScreenOverlay::DrawText(const Font&, ...)`, `ScreenOverlay::MeasureText` (Tasks 1-2).
- Produces: nothing other tasks read.

- [ ] **Step 1: Ship the font**

```bash
mkdir -p game/assets/fonts
cp /c/Windows/Fonts/CascadiaMono.ttf game/assets/fonts/CascadiaMono.ttf
cp Tests/fixtures/OFL.txt game/assets/fonts/OFL.txt
sha256sum game/assets/fonts/CascadiaMono.ttf game/assets/fonts/OFL.txt
```

Expected: `0e141cb9...` for the font and `51882cd3...` for the license, the same two files Task 1 verified. The license must ship beside the font.

The build already copies `game/assets` next to the game's executables wholesale, so no premake change is needed — Step 5 verifies that rather than assuming it.

- [ ] **Step 2: Write the failing test**

Create `game/GameTests/src/FontTests.cpp`:

```cpp
#include <doctest.h>

#include "Cubit/Renderer/FontAtlas.h"

#include <filesystem>

namespace
{
    //The game's own assets, by absolute path. See CB_GAME_ASSETS in
    //game/premake5.lua for why this is not relative to the working directory.
    std::filesystem::path FontPath(const char* name)
    {
        return std::filesystem::path(CB_GAME_ASSETS) / "fonts" / name;
    }
}

TEST_CASE("The font the game ships bakes, and covers everything the HUD can print")
{
    const std::filesystem::path path = FontPath("CascadiaMono.ttf");
    REQUIRE(std::filesystem::exists(path));

    const FontAtlas atlas = FontAtlas::FromFile(path.string(), 32.0f);

    for (char character = FontAtlas::FirstCharacter;
         character <= FontAtlas::LastCharacter; ++character)
    {
        CAPTURE(static_cast<int>(character));
        CHECK(atlas.GlyphFor(character).Advance > 0.0f);
    }
}

TEST_CASE("The font's license ships beside it")
{
    //SIL Open Font License 1.1 permits bundling the font only while its license
    //travels with it, so a build that lost this file would not be shippable.
    REQUIRE(std::filesystem::exists(FontPath("OFL.txt")));
    CHECK(std::filesystem::file_size(FontPath("OFL.txt")) > 0);
}
```

- [ ] **Step 3: Run it and watch it pass**

Regenerate (`C:\dev\premake\premake5.exe vs2026`) and build Debug.

Expected: green, game **31** (29 + 2). These two cases pass immediately — they describe an asset that Step 1 just added, not code that is missing, so there is no red phase to stage. What they guard is the asset going missing or being replaced by something that does not bake.

- [ ] **Step 4: Draw the HUD with it**

In `game/Game/src/GameHudLayer.h`, add `#include "Cubit/Renderer/Font.h"` and `#include "Cubit/Renderer/FontAtlas.h"`, give the layer a font member, and build it in the constructor:

```cpp
    //The game's own font, loaded once. A missing file throws, the same as the
    //map and the player model: it is an asset the game ships, so its absence is
    //a broken build rather than a reason to fall back to the debug font.
    Font m_Font{ FontAtlas::FromFile("assets/fonts/CascadiaMono.ttf", FontPixelHeight) };
```

with, beside the layer's other constants:

```cpp
    //Baked once at this height and scaled when drawn. 32 is comfortably above
    //the size the HUD draws at, so scaling shrinks rather than enlarges and the
    //glyphs stay sharp.
    constexpr float FontPixelHeight = 32.0f;
```

Then replace each `m_Overlay.DrawText(<text>, margin, y)` call with the font overload:

```cpp
        m_Overlay.DrawText(m_Font, <text>, margin, y, HudTextScale);
```

with

```cpp
    //Half the baked height, which lands close to the debug font's old size.
    constexpr float HudTextScale = 0.5f;
```

Leave the labels' wording alone in this task: changing what the HUD says is a separate decision from changing how it is drawn, and keeping the strings identical makes the before-and-after screenshots comparable.

Line spacing now comes from the font rather than the debug font's fixed cell, so replace the layer's `ScreenOverlay::LineHeight()` with `m_Font.LineHeight() * HudTextScale`, and its `m_Overlay.TopLine()` starting position with:

```cpp
        //The first baseline: down from the top by the margin and one line.
        float y = static_cast<float>(m_Overlay.Height()) - ScreenOverlay::Margin
            - m_Font.LineHeight() * HudTextScale;
```

- [ ] **Step 5: Retire the obsolete HUD check, and the list it existed for**

`game/GameTests/src/GameHudTests.cpp` holds exactly one case, "Every label the game HUD draws is one the debug font can draw", and it no longer describes the game: its HUD now draws in a font covering all printable ASCII. **Delete the whole file** rather than leaving one with no cases in it.

Then delete `GameHudLayer::Labels` and the comment above it (`game/Game/src/GameHudLayer.h:112-121`). Check first that nothing else reads it — `grep -rn "Labels" game/ Cubit/ Sandbox/ Tests/` should come back with only that declaration. It is a hand-maintained list of the words the readout draws, duplicating the literals in `DrawReadout` below it, and it exists **only** so that test could read them: test-only data sitting in production code. With the test gone it is dead, and a duplicate list that can silently drift from the real labels is not worth keeping on its own.

**Leave `Tests/src/DebugFontTests.cpp` and the harness's own label list completely alone** — the harness still draws in the debug font and still needs that guard.

A file was removed, so regenerate: `C:\dev\premake\premake5.exe vs2026`.

Expected after this: game **30** (31 − 1).

- [ ] **Step 6: Build both configurations, then verify by running**

Debug and Release green: engine 611 registered / 610 run / 1 skipped, game 30.

Read `C:\Users\maxpk\.claude\projects\C--dev-Cubit\memory\screenshot-cubit-gl-window.md` before launching. Launch `GameApp.exe` **from its own directory** (`bin\Debug-windows-x86_64\GameApp`); find the `GLFW30` window by process id, never `MainWindowHandle`; move it to a known rect and capture exactly that rect, never a full-screen grab; close with `PostMessage(WM_CLOSE)`. If you script it in PowerShell, do **not** name a parameter `-Args` — it collides with the automatic `$args` and silently drops arguments. The run grabs the cursor for a few seconds.

Check on the screenshot, and report each as expected-versus-actual:
1. The HUD reads in the new font, and is **the right way up** — upside-down glyphs mean the atlas flip or the UVs are wrong.
2. Lines do not overlap and are not spread far apart: the line height is coming from the font.
3. The readouts are legible at `HudTextScale`, and positioned much as before.
4. Nothing else on screen changed: the crosshair and the underwater tint are untouched.

Then verify the asset copy actually happened: `ls bin/Debug-windows-x86_64/GameApp/assets/fonts/` must list both files.

- [ ] **Step 7: Commit**

```bash
git add game/assets/fonts game/Game/src/GameHudLayer.h game/GameApp/src/GameApp.cpp game/GameTests/src/FontTests.cpp game/GameTests/src/GameHudTests.cpp
git commit -m "Give the game a real font and draw its HUD with it"
git push origin master
```

---

### Task 4: Write it down

**Files:**
- Modify: `README.md`
- Modify: `docs/engine-roadmap.md` (B4)

**Interfaces:**
- Consumes: everything above.
- Produces: nothing.

- [ ] **Step 1: Update the README**

In the **Rendering** list, after the `Mesh` bullet, add:

```markdown
- `FontAtlas` and `Font`: a TrueType file baked into one atlas with per-glyph metrics,
  and that atlas on the GPU. The engine renders text and the game supplies the font, so
  the engine ships no assets of its own. `ScreenOverlay` draws a line of it at a
  baseline and measures a string for centring, and a character the font does not cover
  draws a question mark rather than a blank
```

In the **Tests** paragraph, update both counts to what the build reports — read them from a build's own doctest summary lines and remember the engine's headline number excludes the skipped case, so the registered total is that number plus one.

In the **Layout** section's `vendor/` list, add `stb` beside the existing entries if that list names them individually.

- [ ] **Step 2: Tick B4**

Replace B4's entry in `docs/engine-roadmap.md` so it begins:

```markdown
- [x] **B4. Text beyond the debug font.** **Done 2026-09-24**, from
  [the spec](superpowers/specs/2026-09-24-text-rendering-design.md). The UI half is now
  B4a; this is the text half.
  - `FontAtlas` bakes a `.ttf` with `stb_truetype` into one 512x512 coverage bitmap plus
    per-glyph metrics, and holds no GL object, so the suite tests it without a window -
    the `MeshGeometry`/`Mesh` split, for the same reason. `Font` is that atlas uploaded
    as a texture.
  - Two conventions are converted once, at bake time, so no caller has to know them: the
    overlay's y axis points up while stb's points down, and the engine's textures start
    at the bottom row while stb's bitmap starts at the top.
  - **A character the font does not cover draws a question mark.** The debug font drew a
    blank, which is how a readout could silently lose the value it existed to show.
  - The engine renders and the game supplies the font: `game/assets/fonts/CascadiaMono.ttf`,
    SIL Open Font License 1.1, with `OFL.txt` beside it because the license only permits
    bundling while it travels with the font. The harness keeps the code-defined debug
    font, so the engine still ships no assets.
  - **Tests:** 9 engine cases on the bake - every printable character advances the pen,
    an uncovered character falls back to `?`, measuring adds up the advances, the bitmap
    is the size it claims, something was actually drawn into it, a line is taller than
    its glyphs, two bakes agree, and non-font bytes and a missing file each throw. 2 game
    cases: the shipped font bakes and covers printable ASCII, and its license ships with
    it. `Font` itself has no unit test - it needs a GL context, like `Mesh`.
  - `GameHudTests`' debug-font label check is retired, and with it
    `GameHudLayer::Labels` — a hand-maintained duplicate of the words the readout draws,
    which existed only for that test and could drift from the real labels. The harness's
    equivalent in `DebugFontTests` stays, because the harness still draws in the debug
    font.

  The original entry follows. `DebugFont` is a 5x7 bitmap with no
```

Then add, after B4:

```markdown
- [ ] **B4a. Widgets: panels, buttons and a screen to put them on.** Split out of B4 on
  2026-09-24. Text, measurement and quads are enough to lay out a scoreboard, so the
  missing piece is interaction: hit-testing a click against a rectangle, hover, and
  which screen has focus. Deliberately not designed in advance of the first screen that
  needs it - a pause menu and a scoreboard are D-section work, and widgets invented
  without one tend to fit neither. Scope doc ENG-07, POL-03.
```

- [ ] **Step 3: Commit**

```bash
git add README.md docs/engine-roadmap.md
git commit -m "Write down how text is drawn"
git push origin master
```

---

## Self-Review

**Spec coverage.** `FontAtlas` with its glyph struct, ASCII range, `FromTrueType`/`FromFile`, `GlyphFor` fallback, `LineHeight`, `Measure`, `Pixels`/`Width`/`Height`, and the throw-rather-than-truncate rule — Task 1. The y-up and row-zero-is-bottom conversions — Task 1, Step 6, with both named in "Two conventions this plan turns on". `Font` keeping the atlas as a member — Task 2. `ScreenOverlay::DrawText`/`MeasureText` with a baseline origin — Task 2. The game's font, its license, loading it, the HUD, and retiring the obsolete HUD case — Task 3. Every engine and game test the spec's Testing section names has a case in Task 1 Step 3 or Task 3 Step 2. "By running" — Task 3 Step 6. Out-of-scope items are recorded as B4a in Task 4.

**Placeholders.** None. The one instruction that defers to the build rather than naming a number is the README's test counts in Task 4, which is deliberate: those counts have twice been wrong in this project when written from memory, so the step says to read them off a build.

**Type consistency.** `FontAtlas::Glyph` with `Uv0`, `Uv1`, `Size`, `Bearing`, `Advance` is defined in Task 1 and used under those names in Task 2's `DrawText`. `FirstCharacter`, `LastCharacter`, `CharacterCount` are defined in Task 1 and used in Tasks 1 and 3. `Font::GlyphFor`, `LineHeight`, `Measure`, `Texture` are defined in Task 2 and used in Tasks 2 and 3. `FromFile(path, pixelHeight)` takes both arguments everywhere it appears.

**Counts.** Engine 602 registered → 611 after Task 1 (9 cases). Game 29 → 31 after Task 3's two new cases → 30 after retiring the debug-font label case.
