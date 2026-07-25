#pragma once

#include "Cubit/Core.h"
#include "Cubit/Voxel/Block.h"
#include "Cubit/Voxel/World.h"

#include <glm/glm.hpp>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

//A single MagicaVoxel model, already converted into Cubit's Y-up space.
struct VoxModel
{
    glm::ivec3 Size{ 0 };               // dimensions in blocks, Cubit axes
    std::vector<std::uint8_t> Voxels;   // dense Size.x*Size.y*Size.z palette indices
    Palette Colors{};                   // index-aligned to BlockId

    //Returns the palette index at a Cubit-space position inside the model.
    std::uint8_t At(int x, int y, int z) const
    {
        return Voxels[static_cast<std::size_t>(x) +
            static_cast<std::size_t>(Size.x) *
            (static_cast<std::size_t>(y) +
             static_cast<std::size_t>(Size.y) * static_cast<std::size_t>(z))];
    }
};

class CB_API VoxLoader
{
public:
    //Parses a single-model .vox from a byte buffer. Throws on malformed input.
    static VoxModel Parse(std::span<const std::uint8_t> bytes);

    //Reads a .vox file and parses it. Throws when the file cannot be read.
    static VoxModel LoadFile(const std::string& path);
};

//Builds a world sized to hold the model, with the model's palette installed.
CB_API World BuildWorld(const VoxModel& model);
