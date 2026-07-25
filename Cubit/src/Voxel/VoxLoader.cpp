#include "cub.h"

#include "Cubit/Voxel/VoxLoader.h"
#include "Cubit/Voxel/Chunk.h"

#include <cstring>
#include <fstream>
#include <iterator>
#include <stdexcept>

namespace
{
    constexpr int MaxDimension = 256;

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

    glm::ivec3 voxSize{ 0 };
    bool haveSize = false;
    bool haveVoxels = false;

    struct RawVoxel { std::uint8_t x, y, z, colorIndex; };
    std::vector<RawVoxel> rawVoxels;

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
            voxSize.x = ReadInt(bytes, p);
            voxSize.y = ReadInt(bytes, p);
            voxSize.z = ReadInt(bytes, p);
            if (voxSize.x <= 0 || voxSize.y <= 0 || voxSize.z <= 0 ||
                voxSize.x > MaxDimension || voxSize.y > MaxDimension ||
                voxSize.z > MaxDimension)
                throw std::runtime_error("vox: model dimension out of range");
            haveSize = true;
        }
        else if (std::strcmp(id, "XYZI") == 0)
        {
            std::size_t p = contentStart;
            const std::int32_t count = ReadInt(bytes, p);
            if (count < 0 ||
                p + static_cast<std::size_t>(count) * 4 > bytes.size())
                throw std::runtime_error("vox: truncated voxel data");

            rawVoxels.reserve(static_cast<std::size_t>(count));
            for (std::int32_t i = 0; i < count; ++i)
            {
                RawVoxel v{ bytes[p + 0], bytes[p + 1], bytes[p + 2], bytes[p + 3] };
                p += 4;
                rawVoxels.push_back(v);
            }
            haveVoxels = true;
        }
        else if (std::strcmp(id, "RGBA") == 0)
        {
            if (contentSize < 256 * 4)
                throw std::runtime_error("vox: truncated palette");

            // RGBA entry j is colour index j + 1; entry 255 is unused.
            for (int j = 0; j < 255; ++j)
            {
                const std::size_t p = contentStart + static_cast<std::size_t>(j) * 4;
                palette[static_cast<std::size_t>(j) + 1] = glm::vec3(
                    bytes[p + 0] / 255.0f,
                    bytes[p + 1] / 255.0f,
                    bytes[p + 2] / 255.0f);
            }
        }

        // Skip content and any children we do not read.
        offset = contentStart +
            static_cast<std::size_t>(contentSize) +
            static_cast<std::size_t>(childrenSize);
    }

    if (!haveSize || !haveVoxels)
        throw std::runtime_error("vox: missing SIZE or XYZI chunk");

    // Convert to Cubit's Y-up space: cubit(x, y, z) = vox(x, z, y).
    VoxModel model;
    model.Size = glm::ivec3(voxSize.x, voxSize.z, voxSize.y);
    model.Colors = palette;
    model.Voxels.assign(
        static_cast<std::size_t>(model.Size.x) *
        static_cast<std::size_t>(model.Size.y) *
        static_cast<std::size_t>(model.Size.z), 0);

    for (const RawVoxel& v : rawVoxels)
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
