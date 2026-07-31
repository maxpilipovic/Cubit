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

//Converts a World back into a Cubit-space VoxModel: the inverse of BuildWorld.
//
//Writes the world's full padded size. A World is always a whole number of
//chunks, so a map whose size is not a multiple of 16 comes back larger than it
//went in — the added layers are air and cost nothing on disk, because Write
//stores voxels sparsely.
//
//Sky light is not carried. The .vox format has nowhere to put it, and
//SkyLight::PropagateAll recomputes it when the map loads.
//
//Throws when the world is larger than 256 on any axis, which a single .vox
//model cannot address. Splitting such a world across several models is
//multi-model stitching, which does not exist yet.
CB_API VoxModel ToVoxModel(const World& world);
