#include <doctest.h>

#include "Cubit/Voxel/Chunk.h"
#include "Cubit/Voxel/ChunkMesher.h"

#include <cmath>
#include <cstddef>

namespace
{
    //Counts air-exposed faces directly from the chunk, without using the mesher.
    //This is the independent oracle the mesher is checked against.
    std::size_t CountExposedFaces(const Chunk& chunk)
    {
        static constexpr int offsets[6][3] =
        {
            {  0,  0,  1 },
            {  0,  0, -1 },
            {  1,  0,  0 },
            { -1,  0,  0 },
            {  0,  1,  0 },
            {  0, -1,  0 }
        };

        std::size_t faces = 0;

        for (int z = 0; z < Chunk::Depth; ++z)
        {
            for (int y = 0; y < Chunk::Height; ++y)
            {
                for (int x = 0; x < Chunk::Width; ++x)
                {
                    if (!chunk.IsBlockSolid(x, y, z))
                        continue;

                    for (const auto& offset : offsets)
                        if (!chunk.IsBlockSolid(x + offset[0], y + offset[1], z + offset[2]))
                            ++faces;
                }
            }
        }

        return faces;
    }

    //Returns the mesher's face count, derived from its four-vertex quads.
    std::size_t MeshedFaceCount(const ChunkMeshData& mesh)
    {
        return mesh.Vertices.size() / 4;
    }

    //Fails when a mesh is not made of well-formed indexed quads.
    void RequireWellFormed(const ChunkMeshData& mesh)
    {
        REQUIRE(mesh.Vertices.size() % 4 == 0);
        REQUIRE(mesh.Indices.size() % 6 == 0);
        REQUIRE(mesh.Indices.size() == MeshedFaceCount(mesh) * 6);

        for (const std::uint32_t index : mesh.Indices)
            REQUIRE(index < mesh.Vertices.size());
    }

    //Fills a chunk with the rolling test terrain the sandbox renders.
    void BuildTestTerrain(Chunk& chunk)
    {
        for (int z = 0; z < Chunk::Depth; ++z)
        {
            for (int x = 0; x < Chunk::Width; ++x)
            {
                const float wave =
                    std::sin(static_cast<float>(x) * 0.35f) +
                    std::cos(static_cast<float>(z) * 0.35f);
                const int height = 6 + static_cast<int>(std::lround(wave * 2.0f));

                for (int y = 0; y < height; ++y)
                    chunk.SetBlock(x, y, z, BlockType::Solid);
            }
        }
    }
}

TEST_CASE("An empty chunk produces no geometry")
{
    const Chunk chunk;
    const ChunkMeshData mesh = ChunkMesher::Build(chunk);

    CHECK(mesh.Vertices.empty());
    CHECK(mesh.Indices.empty());
}

TEST_CASE("A lone block is meshed as six quads")
{
    Chunk chunk;
    chunk.SetBlock(8, 8, 8, BlockType::Solid);

    const ChunkMeshData mesh = ChunkMesher::Build(chunk);

    RequireWellFormed(mesh);
    CHECK(MeshedFaceCount(mesh) == 6);
    CHECK(mesh.Vertices.size() == 24);
    CHECK(mesh.Indices.size() == 36);
}

TEST_CASE("Touching blocks do not mesh the faces between them")
{
    Chunk chunk;
    chunk.SetBlock(8, 8, 8, BlockType::Solid);
    chunk.SetBlock(9, 8, 8, BlockType::Solid);

    const ChunkMeshData mesh = ChunkMesher::Build(chunk);

    RequireWellFormed(mesh);
    CHECK(MeshedFaceCount(mesh) == 10);
}

TEST_CASE("A fully solid chunk meshes only its outer shell")
{
    // Pins the current rule that out-of-bounds neighbours count as air, so a
    // chunk's border is always meshed. Neighbour-aware meshing must change this
    // number deliberately rather than silently.
    Chunk chunk;

    for (int z = 0; z < Chunk::Depth; ++z)
        for (int y = 0; y < Chunk::Height; ++y)
            for (int x = 0; x < Chunk::Width; ++x)
                chunk.SetBlock(x, y, z, BlockType::Solid);

    const ChunkMeshData mesh = ChunkMesher::Build(chunk);

    RequireWellFormed(mesh);
    CHECK(MeshedFaceCount(mesh) == 6 * Chunk::Width * Chunk::Height);
    CHECK(MeshedFaceCount(mesh) == 1536);
}

TEST_CASE("Meshed faces match an independent count for varied terrain")
{
    Chunk chunk;

    SUBCASE("single block")
    {
        chunk.SetBlock(0, 0, 0, BlockType::Solid);
    }

    SUBCASE("floor slab")
    {
        for (int z = 0; z < Chunk::Depth; ++z)
            for (int x = 0; x < Chunk::Width; ++x)
                chunk.SetBlock(x, 0, z, BlockType::Solid);
    }

    SUBCASE("column through the chunk")
    {
        for (int y = 0; y < Chunk::Height; ++y)
            chunk.SetBlock(3, y, 3, BlockType::Solid);
    }

    SUBCASE("hollow shell around a buried block")
    {
        for (int z = 4; z <= 6; ++z)
            for (int y = 4; y <= 6; ++y)
                for (int x = 4; x <= 6; ++x)
                    chunk.SetBlock(x, y, z, BlockType::Solid);
    }

    SUBCASE("rolling terrain")
    {
        BuildTestTerrain(chunk);
    }

    const ChunkMeshData mesh = ChunkMesher::Build(chunk);

    RequireWellFormed(mesh);
    CHECK(MeshedFaceCount(mesh) == CountExposedFaces(chunk));
}

TEST_CASE("The sandbox test terrain meshes to its known size")
{
    Chunk chunk;
    BuildTestTerrain(chunk);

    const ChunkMeshData mesh = ChunkMesher::Build(chunk);

    CHECK(MeshedFaceCount(mesh) == 1122);
    CHECK(mesh.Vertices.size() == 4488);
    CHECK(mesh.Indices.size() == 6732);
}

TEST_CASE("A buried block contributes no geometry")
{
    Chunk chunk;
    chunk.SetBlock(8, 8, 8, BlockType::Solid);

    const std::size_t before = MeshedFaceCount(ChunkMesher::Build(chunk));

    // Encasing the block removes its six faces and adds the shell's own faces.
    chunk.SetBlock(7, 8, 8, BlockType::Solid);
    chunk.SetBlock(9, 8, 8, BlockType::Solid);
    chunk.SetBlock(8, 7, 8, BlockType::Solid);
    chunk.SetBlock(8, 9, 8, BlockType::Solid);
    chunk.SetBlock(8, 8, 7, BlockType::Solid);
    chunk.SetBlock(8, 8, 9, BlockType::Solid);

    const ChunkMeshData mesh = ChunkMesher::Build(chunk);

    REQUIRE(before == 6);
    RequireWellFormed(mesh);
    CHECK(MeshedFaceCount(mesh) == CountExposedFaces(chunk));
    CHECK(MeshedFaceCount(mesh) == 30);
}
