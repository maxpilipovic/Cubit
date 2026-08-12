#include <doctest.h>

#include "Cubit/Voxel/VoxLoader.h"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace
{
    //Appends a little-endian int32 to a byte buffer.
    void PushInt(std::vector<std::uint8_t>& out, std::int32_t value)
    {
        for (int i = 0; i < 4; ++i)
            out.push_back(static_cast<std::uint8_t>((value >> (i * 8)) & 0xFF));
    }

    //Appends a 4-byte chunk tag.
    void PushTag(std::vector<std::uint8_t>& out, const char* tag)
    {
        out.insert(out.end(), tag, tag + 4);
    }

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
}

TEST_CASE("Parsing reports the model size with axes swapped to Y-up")
{
    // vox size (2, 3, 4) -> cubit (x=2, y=vox z=4, z=vox y=3)
    const auto bytes = MakeVoxBytes(2, 3, 4, {});
    const VoxModel model = VoxLoader::Parse(bytes);

    CHECK(model.Size == glm::ivec3(2, 4, 3));
}

TEST_CASE("Parsing places a voxel at the axis-swapped position")
{
    // A voxel at vox (1, 2, 3) lands at cubit (1, 3, 2).
    const auto bytes = MakeVoxBytes(4, 4, 4, { { 1, 2, 3, 7 } });
    const VoxModel model = VoxLoader::Parse(bytes);

    CHECK(model.At(1, 3, 2) == 7);
    CHECK(model.At(0, 0, 0) == 0);
}

TEST_CASE("Parsing maps colour indices through the palette off-by-one")
{
    const auto bytes = MakeVoxBytes(1, 1, 1, { { 0, 0, 0, 5 } });
    const VoxModel model = VoxLoader::Parse(bytes);

    // Colour index 5 came from RGBA entry 4, whose bytes are (5,5,5).
    const glm::vec3 expected(5.0f / 255.0f, 5.0f / 255.0f, 5.0f / 255.0f);
    CHECK(model.Colors[5].x == doctest::Approx(expected.x));
    CHECK(model.Colors[5].y == doctest::Approx(expected.y));
    CHECK(model.Colors[5].z == doctest::Approx(expected.z));
}

TEST_CASE("Parsing rejects a buffer without the VOX magic")
{
    std::vector<std::uint8_t> bytes = { 'N', 'O', 'P', 'E' };
    CHECK_THROWS_AS(VoxLoader::Parse(bytes), std::runtime_error);
}

TEST_CASE("Parsing rejects a model larger than 256 on an axis")
{
    const auto bytes = MakeVoxBytes(257, 1, 1, {});
    CHECK_THROWS_AS(VoxLoader::Parse(bytes), std::runtime_error);
}

TEST_CASE("Parsing rejects a truncated buffer")
{
    auto bytes = MakeVoxBytes(2, 2, 2, { { 0, 0, 0, 1 } });
    bytes.resize(bytes.size() - 8); // chop off part of the RGBA chunk
    CHECK_THROWS_AS(VoxLoader::Parse(bytes), std::runtime_error);
}

TEST_CASE("LoadFile round-trips a written .vox file")
{
    const auto bytes = MakeVoxBytes(2, 2, 2, { { 1, 0, 1, 3 } });

    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "cubit_loadfile_test.vox";
    {
        std::ofstream out(path, std::ios::binary);
        out.write(reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
    }

    const VoxModel model = VoxLoader::LoadFile(path.string());
    std::filesystem::remove(path);

    CHECK(model.Size == glm::ivec3(2, 2, 2));
    CHECK(model.At(1, 1, 0) == 3); // vox (1,0,1) -> cubit (1,1,0)
}

TEST_CASE("LoadFile throws when the file is missing")
{
    CHECK_THROWS_AS(
        VoxLoader::LoadFile("this_file_does_not_exist.vox"),
        std::runtime_error);
}

TEST_CASE("The starter map parses into a single chunk")
{
    const std::filesystem::path path = "Sandbox/assets/maps/starter.vox";
    if (!std::filesystem::exists(path))
        return; // asset not reachable from this working directory; skip

    const VoxModel model = VoxLoader::LoadFile(path.string());
    CHECK(model.Size == glm::ivec3(16, 6, 16));
    CHECK(model.At(0, 0, 0) == 1); // floor corner is the green index
}

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
