#pragma once

#include "Cubit/Voxel/TerrainGen.h"
#include "Cubit/Voxel/VoxLoader.h"

#include <filesystem>

//Worlds for the suite to test against, from the engine's side of the split.
//Nothing here reads the game's assets: the game ships the maps, and the engine
//has to be testable without the game beside it.

//A file in Tests/fixtures, by absolute path. See CB_TEST_FIXTURES in
//Tests/premake5.lua for why this is not relative to the working directory.
inline std::filesystem::path FixturePath(const char* name)
{
    return std::filesystem::path(CB_TEST_FIXTURES) / name;
}

//The world the game ships as battlefield512.vox, generated rather than loaded.
//
//Identical cell for cell: MapGen writes default TerrainConfig plus a size and
//nothing else. Two tests hold that, one on each side of the split - this
//suite's "Generation is deterministic for a fixed seed", and the game suite's
//comparison of its shipped file against this same call, which only the game
//can make because only the game may read its own map. It is also cheaper to
//generate: about 0.3 s in Debug, against 0.9 s to read the 24 MB file.
inline VoxModel ShippedBattlefield()
{
    TerrainConfig config;
    config.Size = glm::ivec3(512, 64, 512);

    return TerrainGen::Generate(config);
}
