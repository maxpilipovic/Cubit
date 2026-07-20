#include <doctest.h>

#include "Cubit/Voxel/Chunk.h"
#include "Cubit/Voxel/ChunkMesher.h"
#include "Cubit/Voxel/World.h"

#include <cmath>
#include <cstddef>

namespace
{
    //Counts air-exposed faces directly from the world, without using the mesher.
    //This is the independent oracle the mesher is checked against.
    std::size_t CountExposedFaces(const World& world, int chunkX, int chunkY, int chunkZ)
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

        const glm::ivec3 origin = World::GetChunkOrigin(chunkX, chunkY, chunkZ);
        std::size_t faces = 0;

        for (int z = 0; z < Chunk::Depth; ++z)
        {
            for (int y = 0; y < Chunk::Height; ++y)
            {
                for (int x = 0; x < Chunk::Width; ++x)
                {
                    const glm::ivec3 position = origin + glm::ivec3(x, y, z);
                    if (!world.IsBlockSolid(position.x, position.y, position.z))
                        continue;

                    for (const auto& offset : offsets)
                        if (!world.IsBlockSolid(
                            position.x + offset[0],
                            position.y + offset[1],
                            position.z + offset[2]))
                        {
                            ++faces;
                        }
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

    //Fills every block of one chunk in the world.
    void FillChunk(World& world, int chunkX, int chunkY, int chunkZ)
    {
        const glm::ivec3 origin = World::GetChunkOrigin(chunkX, chunkY, chunkZ);

        for (int z = 0; z < Chunk::Depth; ++z)
            for (int y = 0; y < Chunk::Height; ++y)
                for (int x = 0; x < Chunk::Width; ++x)
                    world.SetBlock(
                        origin.x + x, origin.y + y, origin.z + z, BlockType::Solid);
    }

    //Fills a world with the rolling test terrain the sandbox renders.
    void BuildTestTerrain(World& world)
    {
        for (int z = 0; z < world.GetDepth(); ++z)
        {
            for (int x = 0; x < world.GetWidth(); ++x)
            {
                const float wave =
                    std::sin(static_cast<float>(x) * 0.35f) +
                    std::cos(static_cast<float>(z) * 0.35f);
                const int height = 6 + static_cast<int>(std::lround(wave * 2.0f));

                for (int y = 0; y < height; ++y)
                    world.SetBlock(x, y, z, BlockType::Solid);
            }
        }
    }
}

TEST_CASE("An empty chunk produces no geometry")
{
    const World world(1, 1, 1);
    const ChunkMeshData mesh = ChunkMesher::Build(world, 0, 0, 0);

    CHECK(mesh.Vertices.empty());
    CHECK(mesh.Indices.empty());
}

TEST_CASE("A lone block is meshed as six quads")
{
    World world(1, 1, 1);
    world.SetBlock(8, 8, 8, BlockType::Solid);

    const ChunkMeshData mesh = ChunkMesher::Build(world, 0, 0, 0);

    RequireWellFormed(mesh);
    CHECK(MeshedFaceCount(mesh) == 6);
    CHECK(mesh.Vertices.size() == 24);
    CHECK(mesh.Indices.size() == 36);
}

TEST_CASE("Touching blocks do not mesh the faces between them")
{
    World world(1, 1, 1);
    world.SetBlock(8, 8, 8, BlockType::Solid);
    world.SetBlock(9, 8, 8, BlockType::Solid);

    const ChunkMeshData mesh = ChunkMesher::Build(world, 0, 0, 0);

    RequireWellFormed(mesh);
    CHECK(MeshedFaceCount(mesh) == 10);
}

TEST_CASE("A solid chunk at the world edge meshes its whole shell")
{
    // Outside the world is air, so a lone chunk still meshes all six sides.
    World world(1, 1, 1);
    FillChunk(world, 0, 0, 0);

    const ChunkMeshData mesh = ChunkMesher::Build(world, 0, 0, 0);

    RequireWellFormed(mesh);
    CHECK(MeshedFaceCount(mesh) == 6 * Chunk::Width * Chunk::Height);
    CHECK(MeshedFaceCount(mesh) == 1536);
}

TEST_CASE("Chunks do not mesh the faces they share with a solid neighbour")
{
    // The seam between two filled chunks is buried, so neither chunk emits it.
    // Each therefore meshes five sides rather than six.
    World world(2, 1, 1);
    FillChunk(world, 0, 0, 0);
    FillChunk(world, 1, 0, 0);

    const ChunkMeshData left = ChunkMesher::Build(world, 0, 0, 0);
    const ChunkMeshData right = ChunkMesher::Build(world, 1, 0, 0);

    RequireWellFormed(left);
    RequireWellFormed(right);

    constexpr std::size_t faceOfAChunk = Chunk::Width * Chunk::Height;
    CHECK(MeshedFaceCount(left) == 5 * faceOfAChunk);
    CHECK(MeshedFaceCount(right) == 5 * faceOfAChunk);
}

TEST_CASE("A block is hidden by a solid block in the next chunk")
{
    World world(2, 1, 1);

    // A lone block on the last column of chunk 0.
    world.SetBlock(Chunk::Width - 1, 8, 8, BlockType::Solid);

    const ChunkMeshData alone = ChunkMesher::Build(world, 0, 0, 0);
    CHECK(MeshedFaceCount(alone) == 6);

    // Its neighbour is the first column of chunk 1, so the two hide one face
    // from each other across the seam.
    world.SetBlock(Chunk::Width, 8, 8, BlockType::Solid);

    CHECK(MeshedFaceCount(ChunkMesher::Build(world, 0, 0, 0)) == 5);

    // Building the far chunk matters on its own. A mesher that looked neighbours
    // up in chunk-local coordinates would hide the opposite face here and still
    // report five, so the count alone does not prove much unless the world is
    // asymmetric about the seam. It is: nothing else in either chunk is solid.
    CHECK(MeshedFaceCount(ChunkMesher::Build(world, 1, 0, 0)) == 5);
}

TEST_CASE("Meshed faces match an independent count for varied terrain")
{
    World world(1, 1, 1);

    SUBCASE("single block")
    {
        world.SetBlock(0, 0, 0, BlockType::Solid);
    }

    SUBCASE("floor slab")
    {
        for (int z = 0; z < Chunk::Depth; ++z)
            for (int x = 0; x < Chunk::Width; ++x)
                world.SetBlock(x, 0, z, BlockType::Solid);
    }

    SUBCASE("column through the chunk")
    {
        for (int y = 0; y < Chunk::Height; ++y)
            world.SetBlock(3, y, 3, BlockType::Solid);
    }

    SUBCASE("hollow shell around a buried block")
    {
        for (int z = 4; z <= 6; ++z)
            for (int y = 4; y <= 6; ++y)
                for (int x = 4; x <= 6; ++x)
                    world.SetBlock(x, y, z, BlockType::Solid);
    }

    SUBCASE("rolling terrain")
    {
        BuildTestTerrain(world);
    }

    const ChunkMeshData mesh = ChunkMesher::Build(world, 0, 0, 0);

    RequireWellFormed(mesh);
    CHECK(MeshedFaceCount(mesh) == CountExposedFaces(world, 0, 0, 0));
}

TEST_CASE("Every chunk of a multi-chunk world matches the independent count")
{
    World world(2, 2, 2);
    BuildTestTerrain(world);

    for (int chunkZ = 0; chunkZ < world.GetChunksZ(); ++chunkZ)
    {
        for (int chunkY = 0; chunkY < world.GetChunksY(); ++chunkY)
        {
            for (int chunkX = 0; chunkX < world.GetChunksX(); ++chunkX)
            {
                const ChunkMeshData mesh =
                    ChunkMesher::Build(world, chunkX, chunkY, chunkZ);

                RequireWellFormed(mesh);
                REQUIRE(MeshedFaceCount(mesh) ==
                    CountExposedFaces(world, chunkX, chunkY, chunkZ));
            }
        }
    }
}

TEST_CASE("The sandbox test terrain meshes to its known size")
{
    World world(1, 1, 1);
    BuildTestTerrain(world);

    const ChunkMeshData mesh = ChunkMesher::Build(world, 0, 0, 0);

    CHECK(MeshedFaceCount(mesh) == 1122);
    CHECK(mesh.Vertices.size() == 4488);
    CHECK(mesh.Indices.size() == 6732);
}

TEST_CASE("A buried block contributes no geometry")
{
    World world(1, 1, 1);
    world.SetBlock(8, 8, 8, BlockType::Solid);

    const std::size_t before = MeshedFaceCount(ChunkMesher::Build(world, 0, 0, 0));

    // Encasing the block removes its six faces and adds the shell's own faces.
    world.SetBlock(7, 8, 8, BlockType::Solid);
    world.SetBlock(9, 8, 8, BlockType::Solid);
    world.SetBlock(8, 7, 8, BlockType::Solid);
    world.SetBlock(8, 9, 8, BlockType::Solid);
    world.SetBlock(8, 8, 7, BlockType::Solid);
    world.SetBlock(8, 8, 9, BlockType::Solid);

    const ChunkMeshData mesh = ChunkMesher::Build(world, 0, 0, 0);

    REQUIRE(before == 6);
    RequireWellFormed(mesh);
    CHECK(MeshedFaceCount(mesh) == CountExposedFaces(world, 0, 0, 0));
    CHECK(MeshedFaceCount(mesh) == 30);
}
