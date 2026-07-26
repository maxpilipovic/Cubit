#include <doctest.h>

#include "Cubit/Voxel/Chunk.h"
#include "Cubit/Voxel/ChunkMesher.h"
#include "Cubit/Voxel/World.h"

namespace
{
    //The cell just above a block at (8, 8, 8): the air side of its top face.
    constexpr glm::ivec3 AirAboveBlock{ 8, 9, 8 };

    //The two tangent offsets picking out one corner of that top face, pointing
    //toward +X and +Z.
    constexpr glm::ivec3 SideX{ 1, 0, 0 };
    constexpr glm::ivec3 SideZ{ 0, 0, 1 };
}

TEST_CASE("An unobstructed corner is fully open")
{
    World world(1, 1, 1);
    world.SetBlock(8, 8, 8, BlockId{1});

    CHECK(ChunkMesher::CornerAoLevel(world, AirAboveBlock, SideX, SideZ) == 3);
}

TEST_CASE("A block diagonally across a corner occludes it by one")
{
    World world(1, 1, 1);
    world.SetBlock(8, 8, 8, BlockId{1});

    // Diagonally opposite the corner: touches it, but neither side.
    world.SetBlock(9, 9, 9, BlockId{1});

    CHECK(ChunkMesher::CornerAoLevel(world, AirAboveBlock, SideX, SideZ) == 2);
}

TEST_CASE("A block on one side of a corner occludes it by one")
{
    World world(1, 1, 1);
    world.SetBlock(8, 8, 8, BlockId{1});
    world.SetBlock(9, 9, 8, BlockId{1});

    CHECK(ChunkMesher::CornerAoLevel(world, AirAboveBlock, SideX, SideZ) == 2);
}

TEST_CASE("A corner with one side and the diagonal filled is occluded by two")
{
    World world(1, 1, 1);
    world.SetBlock(8, 8, 8, BlockId{1});
    world.SetBlock(9, 9, 8, BlockId{1});
    world.SetBlock(9, 9, 9, BlockId{1});

    CHECK(ChunkMesher::CornerAoLevel(world, AirAboveBlock, SideX, SideZ) == 1);
}

TEST_CASE("Both sides filled pins a corner fully dark regardless of the diagonal")
{
    // Two walls meeting at a right angle. The diagonal behind them cannot make
    // the corner any lighter, and must not be allowed to.
    World world(1, 1, 1);
    world.SetBlock(8, 8, 8, BlockId{1});
    world.SetBlock(9, 9, 8, BlockId{1});
    world.SetBlock(8, 9, 9, BlockId{1});

    CHECK(ChunkMesher::CornerAoLevel(world, AirAboveBlock, SideX, SideZ) == 0);

    world.SetBlock(9, 9, 9, BlockId{1});
    CHECK(ChunkMesher::CornerAoLevel(world, AirAboveBlock, SideX, SideZ) == 0);
}

TEST_CASE("Occlusion outside the world reads as open sky")
{
    // A block on the world's top face has nothing above or beside it, so its
    // top corners must be fully open rather than dark.
    World world(1, 1, 1);
    const int top = world.GetHeight() - 1;
    world.SetBlock(8, top, 8, BlockId{1});

    const glm::ivec3 air{ 8, top + 1, 8 };
    CHECK(ChunkMesher::CornerAoLevel(world, air, SideX, SideZ) == 3);
}

TEST_CASE("The occlusion shade table darkens monotonically")
{
    CHECK(ChunkMesher::AoShade[3] == doctest::Approx(1.00f));
    CHECK(ChunkMesher::AoShade[2] < ChunkMesher::AoShade[3]);
    CHECK(ChunkMesher::AoShade[1] < ChunkMesher::AoShade[2]);
    CHECK(ChunkMesher::AoShade[0] < ChunkMesher::AoShade[1]);
    CHECK(ChunkMesher::AoShade[0] > 0.0f);
}

#include <algorithm>
#include <vector>

namespace
{
    //The colour a fully open top face has: the palette colour at full top shade
    //and no occlusion. Any darker vertex on a top face is occluded.
    glm::vec3 OpenTopColor(const World& world)
    {
        return world.GetBlockColor(BlockId{1}) * ChunkMesher::AoShade[3];
    }

    //Collects the vertices of every quad lying flat in one plane — all four of
    //its corners sharing the same value on the given axis. Filtering by plane
    //alone would also catch the edges of side faces that merely touch it,
    //because the mesher emits each face's four vertices separately.
    std::vector<VoxelVertex> QuadsInPlane(
        const ChunkMeshData& mesh, int axis, float value)
    {
        std::vector<VoxelVertex> found;

        for (std::size_t quad = 0; quad * 4 < mesh.Vertices.size(); ++quad)
        {
            const VoxelVertex* corners = &mesh.Vertices[quad * 4];

            bool flat = true;
            for (int i = 0; i < 4; ++i)
                if (corners[i].Position[axis] != value)
                    flat = false;

            if (!flat)
                continue;

            for (int i = 0; i < 4; ++i)
                found.push_back(corners[i]);
        }

        return found;
    }

    //The darkest red channel among a set of vertices.
    float Darkest(const std::vector<VoxelVertex>& vertices)
    {
        float darkest = 1.0f;
        for (const VoxelVertex& vertex : vertices)
            darkest = std::min(darkest, vertex.Color.r);

        return darkest;
    }
}

TEST_CASE("A lone block's top face is unoccluded on every corner")
{
    World world(1, 1, 1);
    world.SetBlock(8, 8, 8, BlockId{1});

    const ChunkMeshData mesh = ChunkMesher::Build(world, 0, 0, 0);
    const std::vector<VoxelVertex> top = QuadsInPlane(mesh, 1, 9.0f);

    REQUIRE(top.size() == 4);

    for (const VoxelVertex& vertex : top)
        CHECK(vertex.Color.r == doctest::Approx(OpenTopColor(world).r));
}

TEST_CASE("An inside corner darkens the floor beside it")
{
    // A floor slab with a wall standing on it. The floor vertices that touch
    // the wall must come out darker than the open floor away from it.
    World world(1, 1, 1);

    for (int z = 0; z < Chunk::Depth; ++z)
        for (int x = 0; x < Chunk::Width; ++x)
            world.SetBlock(x, 0, z, BlockId{1});

    const ChunkMeshData open = ChunkMesher::Build(world, 0, 0, 0);
    const float openFloor = Darkest(QuadsInPlane(open, 1, 1.0f));

    for (int z = 0; z < Chunk::Depth; ++z)
        world.SetBlock(8, 1, z, BlockId{1});

    const ChunkMeshData walled = ChunkMesher::Build(world, 0, 0, 0);

    CHECK(openFloor == doctest::Approx(OpenTopColor(world).r));
    CHECK(Darkest(QuadsInPlane(walled, 1, 1.0f)) < openFloor);
}

TEST_CASE("Ambient occlusion changes no geometry counts")
{
    // AO only recolours vertices and may reorder a quad's indices. The mesh
    // must stay the same size, so the sandbox terrain's known totals hold.
    World world(1, 1, 1);
    world.SetBlock(8, 8, 8, BlockId{1});
    world.SetBlock(9, 9, 9, BlockId{1});
    world.SetBlock(7, 9, 8, BlockId{1});

    const ChunkMeshData mesh = ChunkMesher::Build(world, 0, 0, 0);

    REQUIRE(mesh.Vertices.size() % 4 == 0);
    CHECK(mesh.Indices.size() == (mesh.Vertices.size() / 4) * 6);

    for (const std::uint32_t index : mesh.Indices)
        CHECK(index < mesh.Vertices.size());
}

TEST_CASE("A flipped quad still references each of its four vertices")
{
    // Whichever diagonal a quad is split along, both triangles together must
    // cover all four corners, or the quad renders with a missing wedge.
    World world(1, 1, 1);
    world.SetBlock(8, 8, 8, BlockId{1});
    world.SetBlock(9, 9, 8, BlockId{1});

    const ChunkMeshData mesh = ChunkMesher::Build(world, 0, 0, 0);

    for (std::size_t quad = 0; quad < mesh.Vertices.size() / 4; ++quad)
    {
        const std::uint32_t first = static_cast<std::uint32_t>(quad) * 4;
        bool used[4] = { false, false, false, false };

        for (std::size_t i = quad * 6; i < quad * 6 + 6; ++i)
        {
            REQUIRE(mesh.Indices[i] >= first);
            REQUIRE(mesh.Indices[i] < first + 4);
            used[mesh.Indices[i] - first] = true;
        }

        CHECK(used[0]);
        CHECK(used[1]);
        CHECK(used[2]);
        CHECK(used[3]);
    }
}

TEST_CASE("Occlusion works the same with a vertical tangent axis")
{
    // Every earlier case pairs two horizontal offsets. A side face's corners
    // pair a horizontal axis with a vertical one, so exercise that too.
    World world(1, 1, 1);
    world.SetBlock(8, 8, 8, BlockId{1});

    const glm::ivec3 air{ 9, 8, 8 };
    const glm::ivec3 sideZ{ 0, 0, 1 };
    const glm::ivec3 sideY{ 0, 1, 0 };

    CHECK(ChunkMesher::CornerAoLevel(world, air, sideZ, sideY) == 3);

    world.SetBlock(9, 9, 9, BlockId{1}); // diagonal across the corner
    CHECK(ChunkMesher::CornerAoLevel(world, air, sideZ, sideY) == 2);

    world.SetBlock(9, 8, 9, BlockId{1}); // one side
    CHECK(ChunkMesher::CornerAoLevel(world, air, sideZ, sideY) == 1);

    world.SetBlock(9, 9, 8, BlockId{1}); // the other side seals it
    CHECK(ChunkMesher::CornerAoLevel(world, air, sideZ, sideY) == 0);
}

TEST_CASE("Every face of a lone block carries its own shade")
{
    // Pins the face table's shade column to the right geometry: each of the six
    // planes around a lone block holds exactly one quad, and nothing occludes
    // it, so a mismatched shade cannot hide behind occlusion.
    World world(1, 1, 1);
    world.SetBlock(8, 8, 8, BlockId{1});

    const ChunkMeshData mesh = ChunkMesher::Build(world, 0, 0, 0);
    const float base = world.GetBlockColor(BlockId{1}).r;

    struct ExpectedFace { int Axis; float Value; float Shade; };
    const ExpectedFace faces[6] =
    {
        { 1, 9.0f, 1.00f }, // top
        { 1, 8.0f, 0.60f }, // bottom
        { 0, 9.0f, 0.92f }, // right
        { 0, 8.0f, 0.80f }, // left
        { 2, 9.0f, 0.86f }, // front
        { 2, 8.0f, 0.72f }, // back
    };

    for (const ExpectedFace& face : faces)
    {
        const std::vector<VoxelVertex> quad =
            QuadsInPlane(mesh, face.Axis, face.Value);

        REQUIRE(quad.size() == 4);

        for (const VoxelVertex& vertex : quad)
            CHECK(vertex.Color.r == doctest::Approx(base * face.Shade));
    }
}
