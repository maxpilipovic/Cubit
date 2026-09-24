# Text rendering — design (roadmap B4)

**Date:** 2026-09-24. **Status:** approved in conversation; this records it.

## Problem

`DebugFont` is a 5x7 bitmap defined in code, covering `0123456789-.: ` and 22 capital
letters. It has no lowercase and no J, Q, X or Z. Every HUD label in the project is
written in capitals because that is the entire alphabet available, and a character it
cannot draw comes out **blank** rather than as an error — so a readout can silently lose
the value it was added to show. A scoreboard or a menu needs more than this.

`ScreenOverlay` already draws text, textured quads, a crosshair and full-screen fills, so
this is not about building screen-space drawing. It is about having a font that can spell
words, and knowing how wide the words are.

## Decisions

**The engine renders; the game supplies the font.** The engine gains a font type that
works with any font handed to it, and the game ships the font file as its asset, beside
its maps and its player model. This keeps the engine needing nothing from `game/`, which
B8a established, and lets the harness keep the code-defined debug font and stay
assetless.

**Rasterise from a TTF at load, rather than shipping a baked atlas.** `Texture2D` takes
raw RGBA pixels and there is no image loader anywhere in the project. A pre-baked atlas
would therefore need an image decoder, a metrics file format, and an offline tool to
produce both. `stb_truetype.h` is one public-domain header that yields the atlas pixels
and the metrics together, and `stbtt_BakeFontBitmap` needs no rectangle packer.

**Rasterising is split from uploading**, on the `MeshGeometry`/`Mesh` precedent: the data
half is testable without a GL context, the GL half is verified by running. This is the
same reason that split exists for meshes.

**Text only.** No widgets. A scoreboard is text, measurement and quads, all of which this
provides; a pause menu additionally needs click hit-testing, which is small and belongs
with the screen that needs it rather than invented in advance. B4's UI half becomes B4a.

## Engine

### `FontAtlas` — `Cubit/include/Cubit/Renderer/FontAtlas.h`, `Cubit/src/Renderer/FontAtlas.cpp`

Pure data. No GL, no `Texture2D`, so the suite can test it.

- `struct Glyph { glm::vec2 Uv0, Uv1; glm::vec2 Size; glm::vec2 Bearing; float Advance; };`
  `Uv0`/`Uv1` are the glyph's corners in the atlas in 0..1; `Size` is its size in pixels
  at the baked height; `Bearing` is the offset from the pen position to the glyph's top
  left; `Advance` is how far the pen moves afterwards.
- `static FontAtlas FromTrueType(std::span<const std::uint8_t> ttf, float pixelHeight);`
  Bakes printable ASCII, `' '` (32) through `'~'` (126), into one 8-bit coverage bitmap.
  **Throws `std::runtime_error`** when the font cannot be parsed, or when the glyphs do
  not fit the atlas — `stbtt_BakeFontBitmap` reports that by returning a non-positive
  value, and a silently truncated atlas would show up as missing letters much later.
- `static FontAtlas FromFile(const std::string& path);` — reads the file and calls the
  above, mirroring `VoxLoader::LoadFile`. Throws when the file cannot be read.
- `const std::vector<std::uint8_t>& Pixels() const` — tightly packed 8-bit coverage,
  first row first, with `Width()` and `Height()`.
- `const Glyph& GlyphFor(char character) const` — **a character outside 32..126 returns
  the glyph for `'?'`**, never a blank. Silently drawing nothing is the specific failure
  this design is replacing.
- `float LineHeight() const` — ascent minus descent plus line gap, from
  `stbtt_GetFontVMetrics` scaled by `stbtt_ScaleForPixelHeight`.
- `float Measure(std::string_view text) const` — the sum of the advances, which is what
  centring and right-aligning need.
- `stb_truetype.h` is included only by the `.cpp`. No stb type appears in a public
  header: the game compiles against `Cubit/include` and must not need `vendor/`.

### `Font` — `Cubit/include/Cubit/Renderer/Font.h`, `Cubit/src/Renderer/Font.cpp`

- `explicit Font(const FontAtlas& atlas);` — expands the 8-bit coverage into RGBA (white,
  alpha = coverage) and uploads it as a `Texture2D`. It **keeps the `FontAtlas` as a
  member** rather than copying its metrics out, so there is one definition of a glyph's
  measurements and the two cannot drift.
- `const Texture2D& Texture() const`, and `GlyphFor`, `LineHeight` and `Measure`
  forwarding to that atlas. `Glyph` is `FontAtlas::Glyph`; there is no second glyph type.
- Move-only, like the engine's other GL wrappers.

### `ScreenOverlay` additions

- `void DrawText(const Font& font, std::string_view text, float x, float y,
  float scale = 1.0f, const glm::vec4& colour = glm::vec4(1.0f)) const;`
- `static float MeasureText(const Font& font, std::string_view text, float scale = 1.0f);`
- `x, y` is the top left of the line, y increasing downward — the same convention as the
  existing `DrawText`, so the two can be mixed without surprise.
- **The existing `DrawText(std::string_view, ...)` and everything about `DebugFont` stay
  exactly as they are.** The harness HUD keeps using them, which is what keeps the engine
  assetless.

## Game

- `game/assets/fonts/CascadiaMono.ttf` and `game/assets/fonts/OFL.txt`. Cascadia Mono is
  SIL Open Font License 1.1, which permits bundling as long as the license accompanies
  the font. Monospace also suits a scoreboard's columns.
- `GameApp` loads the atlas once at startup and builds the `Font`, beside the player
  model, and throws if it is missing — the same rule as the map and the model: an asset
  the game ships is a broken build, not a degraded run.
- `GameHudLayer` draws with it instead of the debug font.
- **`GameHudTests`' "Every label the game HUD draws is one the debug font can draw" is
  retired**, because the game's HUD no longer uses the debug font and the constraint it
  guards no longer binds there. The engine's equivalent for the harness HUD
  (`DebugFontTests`) stays exactly as it is.

## Testing

**Engine, headless (`Tests/src/FontAtlasTests.cpp`).** The engine's suite may not read
the game's assets, so it bakes from its own fixture: a copy of the same TTF in
`Tests/fixtures/`, reached by `CB_TEST_FIXTURES`, alongside `stitched.vox` and
`starter.vox`. That the font file exists twice in the repository is the deliberate price
of the split — the alternative is the engine's tests depending on the game's content,
which B8a removed on purpose.

- every printable character 32..126 has a glyph with a positive advance
- a character outside the range yields the same glyph as `'?'`, not a blank
- `Measure` of a string equals the sum of its glyphs' advances, and `Measure("")` is 0
- the atlas has non-zero dimensions and its pixel buffer is exactly `Width() * Height()`
- at least one glyph has non-zero coverage — a bake that produced an empty bitmap would
  otherwise pass everything above
- baking the same bytes twice gives identical metrics, so text does not shift between
  runs
- a truncated or non-font byte sequence throws rather than returning an empty atlas

**Game (`game/GameTests/src/FontTests.cpp`):** the shipped `CascadiaMono.ttf` exists,
parses, and bakes an atlas with a glyph for every printable ASCII character — the same
shape as `PlayerModelTests`, and the check that the asset the game ships is usable.

**By running:** `GameApp`, screenshotted, with the HUD in mixed case — the visible proof
that the GL half works, since `Font` has no unit test by the same precedent as `Mesh`.

## Out of scope

Widgets, layout containers, click handling, kerning, text wrapping, anything outside
ASCII 32..126, and signed-distance-field text for scaling quality. `Font` bakes at one
pixel height; drawing at a different scale stretches that bitmap.
