#include "cub.h"

#include "Cubit/Voxel/VoxWriter.h"

#include <cstring>

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
}

std::vector<std::uint8_t> VoxWriter::Write(const VoxModel& model)
{
    // SIZE: vox axes are (x, z, y) relative to Cubit.
    std::vector<std::uint8_t> size;
    PushTag(size, "SIZE");
    PushInt(size, 12);
    PushInt(size, 0);
    PushInt(size, model.Size.x);
    PushInt(size, model.Size.z);
    PushInt(size, model.Size.y);

    // XYZI: sparse, non-air voxels only, stored at vox (cx, cz, cy).
    std::vector<std::uint8_t> voxelBytes;
    std::int32_t count = 0;
    for (int z = 0; z < model.Size.z; ++z)
        for (int y = 0; y < model.Size.y; ++y)
            for (int x = 0; x < model.Size.x; ++x)
            {
                const std::uint8_t id = model.At(x, y, z);
                if (id == 0)
                    continue;
                voxelBytes.push_back(static_cast<std::uint8_t>(x)); // vox x
                voxelBytes.push_back(static_cast<std::uint8_t>(z)); // vox y = cubit z
                voxelBytes.push_back(static_cast<std::uint8_t>(y)); // vox z = cubit y
                voxelBytes.push_back(id);
                ++count;
            }

    std::vector<std::uint8_t> xyzi;
    PushTag(xyzi, "XYZI");
    PushInt(xyzi, 4 + 4 * count);
    PushInt(xyzi, 0);
    PushInt(xyzi, count);
    xyzi.insert(xyzi.end(), voxelBytes.begin(), voxelBytes.end());

    // RGBA: entry j is colour index j + 1 (inverse of the loader's off-by-one).
    std::vector<std::uint8_t> rgba;
    PushTag(rgba, "RGBA");
    PushInt(rgba, 256 * 4);
    PushInt(rgba, 0);
    for (int j = 0; j < 256; ++j)
    {
        if (j < 255)
        {
            const glm::vec3& c = model.Colors[static_cast<std::size_t>(j) + 1];
            rgba.push_back(ToByte(c.r));
            rgba.push_back(ToByte(c.g));
            rgba.push_back(ToByte(c.b));
            rgba.push_back(255);
        }
        else
        {
            rgba.push_back(0); rgba.push_back(0); rgba.push_back(0); rgba.push_back(0);
        }
    }

    std::vector<std::uint8_t> bytes;
    PushTag(bytes, "VOX ");
    PushInt(bytes, 150);
    PushTag(bytes, "MAIN");
    PushInt(bytes, 0);
    PushInt(bytes, static_cast<std::int32_t>(size.size() + xyzi.size() + rgba.size()));
    bytes.insert(bytes.end(), size.begin(), size.end());
    bytes.insert(bytes.end(), xyzi.begin(), xyzi.end());
    bytes.insert(bytes.end(), rgba.begin(), rgba.end());
    return bytes;
}
