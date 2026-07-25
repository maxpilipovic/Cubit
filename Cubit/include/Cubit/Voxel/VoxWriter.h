#pragma once

#include "Cubit/Core.h"
#include "Cubit/Voxel/VoxLoader.h"   // VoxModel

#include <cstdint>
#include <vector>

//Serializes a Cubit-space VoxModel into MagicaVoxel .vox bytes. The exact inverse
//of VoxLoader::Parse: Parse(Write(model)) reproduces model (colours up to byte
//quantisation).
class CB_API VoxWriter
{
public:
    static std::vector<std::uint8_t> Write(const VoxModel& model);
};
