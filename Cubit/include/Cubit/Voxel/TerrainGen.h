#pragma once

#include "Cubit/Core.h"
#include "Cubit/Voxel/VoxLoader.h"   // VoxModel
#include "Cubit/Voxel/Block.h"       // BlockId, Palette

#include <glm/glm.hpp>

//Tunable parameters for one generated map. Defaults describe the shipped
//128x48x128 battlefield.
struct TerrainConfig
{
    glm::ivec3 Size{ 128, 48, 128 };
    unsigned Seed = 1337;
    int WaterLevel = 10;      // y at/below which the river holds water
    int GroundBase = 13;      // baseline grass height before noise
    int MountainHeight = 30;  // extra height added toward the z flanks
    int SnowLine = 34;        // surfaces above this y become snow/dark rock
    float ForestDensity = 0.06f; // per-eligible-column tree probability
};

//Block ids the generator writes, index-aligned to MapPalette().
namespace MapBlocks
{
    constexpr BlockId Grass = 1;
    constexpr BlockId GrassDark = 2;
    constexpr BlockId Dirt = 3;
    constexpr BlockId Stone = 4;
    constexpr BlockId StoneDark = 5;
    constexpr BlockId Sand = 6;
    constexpr BlockId Water = 7;
    constexpr BlockId Wood = 8;
    constexpr BlockId Leaves = 9;
    constexpr BlockId LeavesLight = 10;
    constexpr BlockId Snow = 11;
    constexpr BlockId RedBase = 12;
    constexpr BlockId BlueBase = 13;
}

//Generates a symmetric Ace-of-Spades-style map as a Cubit-space VoxModel.
class CB_API TerrainGen
{
public:
    static VoxModel Generate(const TerrainConfig& config);

    //The natural + team-colour palette the generator installs.
    static Palette MapPalette();
};
