#pragma once

#include "Cubit/Core.h"

#include <glm/glm.hpp>
#include <cstdint>
#include <vector>

class World;

struct VoxelVertex
{
    glm::vec3 Position{ 0.0f };
    glm::vec4 Color{ 1.0f };
};

//One draw's worth of geometry: vertices and the indices that reference them.
struct MeshGeometry
{
    std::vector<VoxelVertex> Vertices;
    std::vector<std::uint32_t> Indices;
};

//A chunk's mesh, split by how its faces have to be drawn. Transparent faces are
//drawn in a second pass, back to front, so they blend against what is behind
//them — which means they cannot share a draw with the opaque ones.
struct ChunkMeshData
{
    MeshGeometry Opaque;
    MeshGeometry Transparent;
};

class CB_API ChunkMesher
{
public:
    ChunkMesher() = delete;

    //Builds one chunk's mesh, containing only faces exposed to air. Blocks in
    //neighbouring chunks are consulted, so a face shared with a solid block in
    //the next chunk is not emitted. Vertices are in chunk-local coordinates, so
    //the caller positions the mesh by the chunk's origin.
    static ChunkMeshData Build(const World& world, int chunkX, int chunkY, int chunkZ);

    //Brightness multiplier per ambient-occlusion level: index 0 is the most
    //enclosed corner, index 3 is fully open.
    static constexpr float AoShade[4] = { 0.55f, 0.70f, 0.85f, 1.00f };

    //How exposed one corner of a face is, from 0 (fully enclosed) to 3 (open).
    //openCell is the cell in front of the face; sideA and sideB are the offsets
    //from it toward the two edges meeting at the corner. Public so it can be
    //tested directly, and so a future greedy mesher can merge on equal values.
    static int CornerAoLevel(
        const World& world,
        const glm::ivec3& openCell,
        const glm::ivec3& sideA,
        const glm::ivec3& sideB);

    //The raw fraction of sky light a face corner gets, between 0 and 1. This is
    //not a brightness — the floor is applied once, to the finished shading, in
    //AddFace, not here. Averages the light of the open cells touching the
    //corner, which is what makes lighting graduate smoothly across a surface
    //instead of stepping from block to block. Opaque cells hold no light and
    //are left out of the average.
    static float CornerLightShade(
        const World& world,
        const glm::ivec3& openCell,
        const glm::ivec3& sideA,
        const glm::ivec3& sideB);

    //How dark a fully unlit surface goes, as a floor on the finished shading
    //(face shade times AO times light), not on light alone. Not zero: an unlit
    //bunker should read as dark but still be navigable, rather than being a
    //black void.
    static constexpr float LightFloor = 0.15f;
};
