#include "cub.h"

#include "Cubit/Voxel/VoxLoader.h"
#include "Cubit/Voxel/Chunk.h"

#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>

namespace
{
    constexpr int MaxDimension = 256;

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

    //Reads a little-endian int32 at offset and advances it.
    std::int32_t ReadInt(std::span<const std::uint8_t> bytes, std::size_t& offset)
    {
        if (offset + 4 > bytes.size())
            throw std::runtime_error("vox: unexpected end of file");

        std::int32_t value = 0;
        std::memcpy(&value, bytes.data() + offset, 4);
        offset += 4;
        return value;
    }

    //Reports whether the four bytes at offset equal a chunk tag.
    bool TagEquals(std::span<const std::uint8_t> bytes, std::size_t offset, const char* tag)
    {
        return offset + 4 <= bytes.size() &&
            std::memcmp(bytes.data() + offset, tag, 4) == 0;
    }

    struct RawVoxel { std::uint8_t x, y, z, colorIndex; };

    //One SIZE/XYZI pair, still in vox axes. A model's index in this vector is
    //the id the scene graph's shape nodes reference.
    struct RawModel
    {
        glm::ivec3 VoxSize{ 0 };
        std::vector<RawVoxel> Voxels;
    };

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
}

VoxModel VoxLoader::Parse(std::span<const std::uint8_t> bytes)
{
    std::size_t offset = 0;

    if (!TagEquals(bytes, offset, "VOX "))
        throw std::runtime_error("vox: missing VOX magic");
    offset += 4;

    const std::int32_t version = ReadInt(bytes, offset);
    if (version != 150 && version != 200)
        throw std::runtime_error("vox: unsupported version");

    if (!TagEquals(bytes, offset, "MAIN"))
        throw std::runtime_error("vox: missing MAIN chunk");
    offset += 4;
    ReadInt(bytes, offset); // MAIN content size (0)
    ReadInt(bytes, offset); // MAIN children size

    std::vector<RawModel> models;
    std::map<int, SceneNode> nodes;

    Palette palette = DefaultPalette();

    // Walk MAIN's child chunks until the buffer is consumed.
    while (offset + 12 <= bytes.size())
    {
        char id[5] = { 0 };
        std::memcpy(id, bytes.data() + offset, 4);
        offset += 4;
        const std::int32_t contentSize = ReadInt(bytes, offset);
        const std::int32_t childrenSize = ReadInt(bytes, offset);

        if (contentSize < 0 || childrenSize < 0 ||
            offset + static_cast<std::size_t>(contentSize) > bytes.size())
            throw std::runtime_error("vox: truncated chunk");

        const std::size_t contentStart = offset;

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
        else if (std::strcmp(id, "RGBA") == 0)
        {
            if (contentSize < 256 * 4)
                throw std::runtime_error("vox: truncated palette");

            // RGBA entry j is colour index j + 1; entry 255 is unused.
            for (int j = 0; j < 255; ++j)
            {
                const std::size_t p = contentStart + static_cast<std::size_t>(j) * 4;
                palette[static_cast<std::size_t>(j) + 1] = glm::vec4(
                    bytes[p + 0] / 255.0f,
                    bytes[p + 1] / 255.0f,
                    bytes[p + 2] / 255.0f,
                    bytes[p + 3] / 255.0f);
            }
        }
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

                RequireIdentityRotation(frame);

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

        // Skip content and any children we do not read.
        offset = contentStart +
            static_cast<std::size_t>(contentSize) +
            static_cast<std::size_t>(childrenSize);
    }

    if (models.empty())
        throw std::runtime_error("vox: missing SIZE or XYZI chunk");

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
    RequireLoadableSize(model.Size);
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
}

VoxModel VoxLoader::LoadFile(const std::string& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
        throw std::runtime_error("vox: cannot open file: " + path);

    std::vector<std::uint8_t> bytes(
        (std::istreambuf_iterator<char>(file)),
        std::istreambuf_iterator<char>());

    return Parse(bytes);
}

World BuildWorld(const VoxModel& model)
{
    const int chunksX = (model.Size.x + Chunk::Width - 1) / Chunk::Width;
    const int chunksY = (model.Size.y + Chunk::Height - 1) / Chunk::Height;
    const int chunksZ = (model.Size.z + Chunk::Depth - 1) / Chunk::Depth;

    World world(
        chunksX > 0 ? chunksX : 1,
        chunksY > 0 ? chunksY : 1,
        chunksZ > 0 ? chunksZ : 1);
    world.SetPalette(model.Colors);

    for (int z = 0; z < model.Size.z; ++z)
        for (int y = 0; y < model.Size.y; ++y)
            for (int x = 0; x < model.Size.x; ++x)
            {
                const std::uint8_t id = model.At(x, y, z);
                if (id != 0)
                    world.SetBlock(x, y, z, id);
            }

    return world;
}
