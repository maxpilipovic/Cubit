#include "cub.h"

#include "Cubit/Voxel/VoxWriter.h"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace
{
    void PushInt(std::vector<std::uint8_t>& out, std::int32_t value)
    {
        for (int i = 0; i < 4; ++i)
            out.push_back(static_cast<std::uint8_t>((value >> (i * 8)) & 0xFF));
    }

    void PushTag(std::vector<std::uint8_t>& out, const char* tag)
    {
        out.insert(out.end(), tag, tag + 4);
    }

    //Converts a 0..1 colour channel to a 0..255 byte.
    std::uint8_t ToByte(float channel)
    {
        float c = channel;
        if (c < 0.0f) c = 0.0f;
        if (c > 1.0f) c = 1.0f;
        return static_cast<std::uint8_t>(c * 255.0f + 0.5f);
    }

    //The largest model a single .vox can address, matching VoxLoader's own limit
    //on the way in.
    constexpr int MaxDimension = 256;

    //Rejects a world a single .vox model cannot hold. Write stores each voxel
    //coordinate in one byte, so a larger world would wrap silently and produce a
    //file that loads back as garbage — a corrupt save is worse than a refused
    //one.
    void RequireWritableSize(const glm::ivec3& size)
    {
        const char* const axes[3] = { "x", "y", "z" };

        for (int i = 0; i < 3; ++i)
            if (size[i] > MaxDimension)
                throw std::runtime_error(
                    std::string("vox: world is too large for a single .vox model (")
                    + axes[i] + " = " + std::to_string(size[i]) + ")");
    }

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

    //Writes a model small enough for one .vox model. These are the exact bytes
    //Cubit has always written, and they must stay that way: every .vox in the
    //repo and every round-trip test is built on them.
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
}

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

void VoxWriter::WriteFile(const VoxModel& model, const std::string& path)
{
    const std::vector<std::uint8_t> bytes = Write(model);

    std::ofstream file(path, std::ios::binary);
    if (!file)
        throw std::runtime_error("vox: cannot open file for writing: " + path);

    file.write(reinterpret_cast<const char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size()));

    //Closing explicitly (rather than letting the destructor do it) puts any
    //flush failure into the stream state before this check, so a truncated
    //file is caught instead of discovered only when someone tried to load it.
    file.close();
    if (!file)
        throw std::runtime_error("vox: failed writing file: " + path);
}

VoxModel ToVoxModel(const World& world)
{
    VoxModel model;
    model.Size = glm::ivec3(
        world.GetWidth(), world.GetHeight(), world.GetDepth());
    model.Colors = world.GetPalette();
    model.Voxels.assign(
        static_cast<std::size_t>(model.Size.x) *
        static_cast<std::size_t>(model.Size.y) *
        static_cast<std::size_t>(model.Size.z), 0);

    for (int z = 0; z < model.Size.z; ++z)
        for (int y = 0; y < model.Size.y; ++y)
            for (int x = 0; x < model.Size.x; ++x)
            {
                const BlockId id = world.GetBlock(x, y, z);
                if (id == 0)
                    continue;

                model.Voxels[static_cast<std::size_t>(x) +
                    static_cast<std::size_t>(model.Size.x) *
                    (static_cast<std::size_t>(y) +
                     static_cast<std::size_t>(model.Size.y) *
                     static_cast<std::size_t>(z))] = id;
            }

    return model;
}
