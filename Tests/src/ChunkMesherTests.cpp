#include <doctest.h>

#include "Cubit/Voxel/Chunk.h"
#include "Cubit/Voxel/ChunkMesher.h"
#include "Cubit/Voxel/SkyLight.h"
#include "Cubit/Voxel/World.h"

#include <chrono>
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

    //Quads actually emitted. Changes with the merge rule, so it is only asserted
    //where the expected value is hand-checkable.
    std::size_t QuadCount(const ChunkMeshData& mesh)
    {
        return (mesh.Opaque.Vertices.size() + mesh.Transparent.Vertices.size()) / 4;
    }

    //The number of block faces a mesh covers: per quad, the product of its two
    //non-zero extents. Merging changes how faces are grouped into quads but not
    //how much surface they cover, so this holds under any merge rule — which is
    //what makes it the oracle CountExposedFaces can be compared against.
    std::size_t CoveredArea(const MeshGeometry& geometry)
    {
        std::size_t area = 0;

        for (std::size_t quad = 0; quad * 4 < geometry.Vertices.size(); ++quad)
        {
            const VoxelVertex* corners = &geometry.Vertices[quad * 4];

            glm::vec3 low = corners[0].Position;
            glm::vec3 high = corners[0].Position;
            for (int i = 1; i < 4; ++i)
            {
                low = glm::min(low, corners[i].Position);
                high = glm::max(high, corners[i].Position);
            }

            const glm::vec3 extent = high - low;

            // A quad is flat along its normal, so that axis contributes nothing
            // and the other two multiply out to the face count it covers.
            std::size_t cells = 1;
            for (int axis = 0; axis < 3; ++axis)
                if (extent[axis] > 0.0f)
                    cells *= static_cast<std::size_t>(std::lround(extent[axis]));

            area += cells;
        }

        return area;
    }

    std::size_t CoveredArea(const ChunkMeshData& mesh)
    {
        return CoveredArea(mesh.Opaque) + CoveredArea(mesh.Transparent);
    }

    //Fails when a mesh is not made of well-formed indexed quads. Says nothing
    //about how many quads there should be — that is the merge rule's business.
    void RequireWellFormed(const ChunkMeshData& mesh)
    {
        REQUIRE(mesh.Opaque.Vertices.size() % 4 == 0);
        REQUIRE(mesh.Opaque.Indices.size() % 6 == 0);
        REQUIRE(mesh.Opaque.Indices.size() == (mesh.Opaque.Vertices.size() / 4) * 6);

        for (const std::uint32_t index : mesh.Opaque.Indices)
            REQUIRE(index < mesh.Opaque.Vertices.size());

        REQUIRE(mesh.Transparent.Vertices.size() % 4 == 0);
        REQUIRE(mesh.Transparent.Indices.size() % 6 == 0);
        REQUIRE(mesh.Transparent.Indices.size() ==
            (mesh.Transparent.Vertices.size() / 4) * 6);

        for (const std::uint32_t index : mesh.Transparent.Indices)
            REQUIRE(index < mesh.Transparent.Vertices.size());
    }

    //Fills every block of one chunk in the world.
    void FillChunk(World& world, int chunkX, int chunkY, int chunkZ)
    {
        const glm::ivec3 origin = World::GetChunkOrigin(chunkX, chunkY, chunkZ);

        for (int z = 0; z < Chunk::Depth; ++z)
            for (int y = 0; y < Chunk::Height; ++y)
                for (int x = 0; x < Chunk::Width; ++x)
                    world.SetBlock(
                        origin.x + x, origin.y + y, origin.z + z, BlockId{1});
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
                    world.SetBlock(x, y, z, BlockId{1});
            }
        }
    }
}

TEST_CASE("An empty chunk produces no geometry")
{
    const World world(1, 1, 1);
    const ChunkMeshData mesh = ChunkMesher::Build(world, 0, 0, 0);

    CHECK(mesh.Opaque.Vertices.empty());
    CHECK(mesh.Opaque.Indices.empty());
}

TEST_CASE("A lone block is meshed as six quads")
{
    World world(1, 1, 1);
    world.SetBlock(8, 8, 8, BlockId{1});

    const ChunkMeshData mesh = ChunkMesher::Build(world, 0, 0, 0);

    RequireWellFormed(mesh);
    CHECK(QuadCount(mesh) == 6);
    CHECK(mesh.Opaque.Vertices.size() == 24);
    CHECK(mesh.Opaque.Indices.size() == 36);
}

TEST_CASE("Touching blocks do not mesh the faces between them")
{
    World world(1, 1, 1);
    world.SetBlock(8, 8, 8, BlockId{1});
    world.SetBlock(9, 8, 8, BlockId{1});

    const ChunkMeshData mesh = ChunkMesher::Build(world, 0, 0, 0);

    RequireWellFormed(mesh);
    CHECK(CoveredArea(mesh) == 10);
}

TEST_CASE("A solid chunk at the world edge meshes its whole shell")
{
    // Outside the world is air, so a lone chunk still meshes all six sides.
    World world(1, 1, 1);
    FillChunk(world, 0, 0, 0);

    const ChunkMeshData mesh = ChunkMesher::Build(world, 0, 0, 0);

    RequireWellFormed(mesh);
    CHECK(CoveredArea(mesh) == 6 * Chunk::Width * Chunk::Height);
    CHECK(CoveredArea(mesh) == 1536);
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
    CHECK(CoveredArea(left) == 5 * faceOfAChunk);
    CHECK(CoveredArea(right) == 5 * faceOfAChunk);
}

TEST_CASE("A block is hidden by a solid block in the next chunk")
{
    World world(2, 1, 1);

    // A lone block on the last column of chunk 0.
    world.SetBlock(Chunk::Width - 1, 8, 8, BlockId{1});

    const ChunkMeshData alone = ChunkMesher::Build(world, 0, 0, 0);
    CHECK(CoveredArea(alone) == 6);

    // Its neighbour is the first column of chunk 1, so the two hide one face
    // from each other across the seam.
    world.SetBlock(Chunk::Width, 8, 8, BlockId{1});

    CHECK(CoveredArea(ChunkMesher::Build(world, 0, 0, 0)) == 5);

    // Building the far chunk matters on its own. A mesher that looked neighbours
    // up in chunk-local coordinates would hide the opposite face here and still
    // report five, so the count alone does not prove much unless the world is
    // asymmetric about the seam. It is: nothing else in either chunk is solid.
    CHECK(CoveredArea(ChunkMesher::Build(world, 1, 0, 0)) == 5);
}

TEST_CASE("Meshed faces match an independent count for varied terrain")
{
    World world(1, 1, 1);

    SUBCASE("single block")
    {
        world.SetBlock(0, 0, 0, BlockId{1});
    }

    SUBCASE("floor slab")
    {
        for (int z = 0; z < Chunk::Depth; ++z)
            for (int x = 0; x < Chunk::Width; ++x)
                world.SetBlock(x, 0, z, BlockId{1});
    }

    SUBCASE("column through the chunk")
    {
        for (int y = 0; y < Chunk::Height; ++y)
            world.SetBlock(3, y, 3, BlockId{1});
    }

    SUBCASE("hollow shell around a buried block")
    {
        for (int z = 4; z <= 6; ++z)
            for (int y = 4; y <= 6; ++y)
                for (int x = 4; x <= 6; ++x)
                    world.SetBlock(x, y, z, BlockId{1});
    }

    SUBCASE("rolling terrain")
    {
        BuildTestTerrain(world);
    }

    const ChunkMeshData mesh = ChunkMesher::Build(world, 0, 0, 0);

    RequireWellFormed(mesh);
    CHECK(CoveredArea(mesh) == CountExposedFaces(world, 0, 0, 0));
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
                REQUIRE(CoveredArea(mesh) ==
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

    // The surface this terrain exposes is fixed. How many quads cover it is the
    // merge rule's business, so it is not asserted here.
    CHECK(CoveredArea(mesh) == 1122);
}

TEST_CASE("A buried block contributes no geometry")
{
    World world(1, 1, 1);
    world.SetBlock(8, 8, 8, BlockId{1});

    const std::size_t before = CoveredArea(ChunkMesher::Build(world, 0, 0, 0));

    // Encasing the block removes its six faces and adds the shell's own faces.
    world.SetBlock(7, 8, 8, BlockId{1});
    world.SetBlock(9, 8, 8, BlockId{1});
    world.SetBlock(8, 7, 8, BlockId{1});
    world.SetBlock(8, 9, 8, BlockId{1});
    world.SetBlock(8, 8, 7, BlockId{1});
    world.SetBlock(8, 8, 9, BlockId{1});

    const ChunkMeshData mesh = ChunkMesher::Build(world, 0, 0, 0);

    REQUIRE(before == 6);
    RequireWellFormed(mesh);
    CHECK(CoveredArea(mesh) == CountExposedFaces(world, 0, 0, 0));
    CHECK(CoveredArea(mesh) == 30);
}

TEST_CASE("Meshing a chunk costs little more than reading its blocks")
{
    // Build used to sample every ambient-occlusion and light cell straight out
    // of World, which turns a world position into a chunk and an offset on
    // every single read â€” tens of thousands of times per chunk. Now that
    // relighting an edit is cheap, remeshing is what an edit costs, so a chunk
    // rebuild has to stay near the cost of walking its blocks once.
    World world(4, 1, 4);
    BuildTestTerrain(world);
    SkyLight::PropagateAll(world);

    const auto start = std::chrono::steady_clock::now();

    std::size_t faces = 0;
    for (int chunkZ = 0; chunkZ < world.GetChunksZ(); ++chunkZ)
        for (int chunkX = 0; chunkX < world.GetChunksX(); ++chunkX)
            faces += CoveredArea(ChunkMesher::Build(world, chunkX, 0, chunkZ));

    const auto elapsed = std::chrono::steady_clock::now() - start;
    const double ms =
        std::chrono::duration<double, std::milli>(elapsed).count();

    REQUIRE(faces > 0); // Meshing nothing would pass any budget.
    INFO("meshing ", world.GetChunksX() * world.GetChunksZ(),
        " chunks (", faces, " faces) took ", ms, " ms");

    // Sampling through World cost about 98 ms here; reading a cached
    // neighbourhood by flat index costs about 31, drifting a couple of ms
    // either way between runs. The bound is set well clear of that spread
    // rather than against it: this is a Debug build on whatever machine runs
    // it, so the test is here to catch a return to the old order of magnitude,
    // not to hold a stopwatch to the current one.
    CHECK(ms < 50.0);
}

namespace
{
    //A world whose palette makes id 2 transparent, for the opacity cases below.
    World TransparentPaletteWorld(int cx, int cy, int cz)
    {
        World world(cx, cy, cz);
        Palette palette = DefaultPalette();
        palette[2] = glm::vec4(0.2f, 0.4f, 0.8f, 0.5f);
        world.SetPalette(palette);
        return world;
    }
}

TEST_CASE("An opaque block facing a transparent one is meshed")
{
    // The riverbed case: stone under water must produce a face, where today the
    // water hides it.
    World world = TransparentPaletteWorld(1, 1, 1);
    world.SetBlock(8, 8, 8, BlockId{1}); // opaque
    world.SetBlock(8, 9, 8, BlockId{2}); // transparent, directly above

    const ChunkMeshData mesh = ChunkMesher::Build(world, 0, 0, 0);

    // 6 faces for the opaque block (its top is now exposed) + 5 for the
    // transparent one (its bottom faces the opaque block and is hidden).
    CHECK(mesh.Opaque.Indices.size() / 6 == 6);       // the stone block
    CHECK(mesh.Transparent.Indices.size() / 6 == 5);  // the water block
}

TEST_CASE("Two touching transparent blocks share no face")
{
    // Internal faces inside a body of water would blend against each other and
    // darken it in bands.
    World world = TransparentPaletteWorld(1, 1, 1);
    world.SetBlock(8, 8, 8, BlockId{2});
    world.SetBlock(8, 9, 8, BlockId{2});

    const ChunkMeshData mesh = ChunkMesher::Build(world, 0, 0, 0);

    CHECK(mesh.Opaque.Indices.empty());
    CHECK(mesh.Transparent.Indices.size() / 6 == 10); // 12 minus the two touching faces
}

TEST_CASE("A transparent block against air is meshed")
{
    World world = TransparentPaletteWorld(1, 1, 1);
    world.SetBlock(8, 8, 8, BlockId{2});

    const ChunkMeshData mesh = ChunkMesher::Build(world, 0, 0, 0);

    CHECK(mesh.Opaque.Indices.empty());
    CHECK(mesh.Transparent.Indices.size() / 6 == 6);
}

TEST_CASE("Two touching opaque blocks of different ids share no face")
{
    // The pre-existing rule, restated so the new id comparison cannot break it:
    // different opaque blocks still hide each other.
    World world = TransparentPaletteWorld(1, 1, 1);
    world.SetBlock(8, 8, 8, BlockId{1});
    world.SetBlock(8, 9, 8, BlockId{3});

    const ChunkMeshData mesh = ChunkMesher::Build(world, 0, 0, 0);

    CHECK(CoveredArea(mesh) == 10);
}

TEST_CASE("Faces are split by the opacity of the block they belong to")
{
    World world = TransparentPaletteWorld(1, 1, 1);
    world.SetBlock(2, 2, 2, BlockId{1}); // opaque, isolated
    world.SetBlock(8, 8, 8, BlockId{2}); // transparent, isolated

    const ChunkMeshData mesh = ChunkMesher::Build(world, 0, 0, 0);

    CHECK(mesh.Opaque.Indices.size() / 6 == 6);
    CHECK(mesh.Transparent.Indices.size() / 6 == 6);

    // Each set's indices must address its own vertices, not the other's.
    for (const std::uint32_t index : mesh.Opaque.Indices)
        REQUIRE(index < mesh.Opaque.Vertices.size());
    for (const std::uint32_t index : mesh.Transparent.Indices)
        REQUIRE(index < mesh.Transparent.Vertices.size());
}
