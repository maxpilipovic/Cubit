#include <doctest.h>

#include "Cubit/Voxel/Chunk.h"
#include "Cubit/Voxel/ChunkMesher.h"
#include "Cubit/Voxel/SkyLight.h"
#include "Cubit/Voxel/World.h"

#include <algorithm>
#include <vector>

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

namespace
{
    //The colour a fully open, fully lit top face has: the palette colour at full
    //top shade, no occlusion, and full sky light.
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

    //Fills every cell with full daylight, so lighting contributes exactly 1.0
    //and ambient occlusion is the only thing varying a face's colour. Real
    //propagation would also shade the floor beside a wall and the underside of
    //a block, which would let these tests pass even if occlusion were broken.
    void FloodFullDaylight(World& world)
    {
        for (int z = 0; z < world.GetDepth(); ++z)
            for (int y = 0; y < world.GetHeight(); ++y)
                for (int x = 0; x < world.GetWidth(); ++x)
                    world.SetSkyLight(x, y, z, SkyLight::Max);
    }
}

TEST_CASE("A lone block's top face is unoccluded on every corner")
{
    World world(1, 1, 1);
    world.SetBlock(8, 8, 8, BlockId{1});
    FloodFullDaylight(world);

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

    FloodFullDaylight(world);
    const ChunkMeshData open = ChunkMesher::Build(world, 0, 0, 0);
    const float openFloor = Darkest(QuadsInPlane(open, 1, 1.0f));

    for (int z = 0; z < Chunk::Depth; ++z)
        world.SetBlock(8, 1, z, BlockId{1});

    // World::SetBlock does not touch light, so this is technically redundant
    // with the flood above — but being explicit keeps the test readable.
    FloodFullDaylight(world);
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
    FloodFullDaylight(world);

    const ChunkMeshData mesh = ChunkMesher::Build(world, 0, 0, 0);
    const float base = world.GetBlockColor(BlockId{1}).r;

    // Full daylight and no occlusion, so face.Shade is the only variable — but
    // the floor now applies to the finished product, not to light alone, so
    // the expected multiplier is LightFloor + (1 - LightFloor) * face.Shade,
    // not face.Shade directly.
    const float floor = ChunkMesher::LightFloor;
    const auto remapped = [floor](float shade) { return floor + (1.0f - floor) * shade; };

    struct ExpectedFace { int Axis; float Value; float Shade; };
    const ExpectedFace faces[6] =
    {
        { 1, 9.0f, remapped(1.00f) }, // top
        { 1, 8.0f, remapped(0.60f) }, // bottom
        { 0, 9.0f, remapped(0.92f) }, // right
        { 0, 8.0f, remapped(0.80f) }, // left
        { 2, 9.0f, remapped(0.86f) }, // front
        { 2, 8.0f, remapped(0.72f) }, // back
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

TEST_CASE("Unlit faces are darker than lit ones but never black")
{
    World world(1, 4, 1);

    // A slab with a roofed pocket cut under it: the pocket's floor is lit only
    // by whatever creeps in, the open slab top is under full sky.
    const int floor = 4;
    for (int z = 0; z < Chunk::Depth; ++z)
        for (int x = 0; x < Chunk::Width; ++x)
            world.SetBlock(x, floor, z, BlockId{1});

    SkyLight::PropagateAll(world);

    const ChunkMeshData lit = ChunkMesher::Build(world, 0, 0, 0);
    const float openTop = Darkest(QuadsInPlane(lit, 1, static_cast<float>(floor + 1)));

    // Now roof the whole world well above the slab, cutting the sky off.
    for (int z = 0; z < world.GetDepth(); ++z)
        for (int x = 0; x < world.GetWidth(); ++x)
            world.SetBlock(x, 40, z, BlockId{1});

    SkyLight::PropagateAll(world);

    const ChunkMeshData dark = ChunkMesher::Build(world, 0, 0, 0);
    const float roofedTop = Darkest(QuadsInPlane(dark, 1, static_cast<float>(floor + 1)));

    CHECK(roofedTop < openTop);

    // The floor now applies to the whole finished shade product, not to light
    // alone, so the guaranteed minimum is base * LightFloor regardless of
    // face shade or occlusion — the sampled faces are top faces (shade 1.0),
    // but the floor term isn't multiplied by face shade at all, so this bound
    // holds unconditionally, not just because these faces happen to be 1.0.
    const float base = world.GetBlockColor(BlockId{1}).r;
    CHECK(roofedTop >= base * ChunkMesher::LightFloor * 0.99f);
}

TEST_CASE("The light shade curve spans the raw 0 to 1 range")
{
    World world(1, 1, 1);

    // A cell under full sky averages to 1.0; a fully dark one to 0.0. The
    // floor is no longer applied here — it is applied once, to the finished
    // shading, in AddFace.
    world.SetSkyLight(8, 9, 8, SkyLight::Max);
    const glm::ivec3 air{ 8, 9, 8 };
    const glm::ivec3 sideX{ 1, 0, 0 };
    const glm::ivec3 sideZ{ 0, 0, 1 };

    // Every sampled cell is air at Max, so the average is Max.
    world.SetSkyLight(9, 9, 8, SkyLight::Max);
    world.SetSkyLight(8, 9, 9, SkyLight::Max);
    world.SetSkyLight(9, 9, 9, SkyLight::Max);

    CHECK(ChunkMesher::CornerLightShade(world, air, sideX, sideZ)
        == doctest::Approx(1.0f));

    world.SetSkyLight(8, 9, 8, 0);
    world.SetSkyLight(9, 9, 8, 0);
    world.SetSkyLight(8, 9, 9, 0);
    world.SetSkyLight(9, 9, 9, 0);

    CHECK(ChunkMesher::CornerLightShade(world, air, sideX, sideZ)
        == doctest::Approx(0.0f));
}

TEST_CASE("Corner light ignores solid cells when averaging")
{
    // A solid neighbour holds light 0, but it is not a place light could be —
    // averaging it in would darken the surface for no reason.
    World world(1, 1, 1);
    const glm::ivec3 air{ 8, 9, 8 };
    const glm::ivec3 sideX{ 1, 0, 0 };
    const glm::ivec3 sideZ{ 0, 0, 1 };

    world.SetSkyLight(8, 9, 8, SkyLight::Max);
    world.SetSkyLight(9, 9, 8, SkyLight::Max);
    world.SetSkyLight(8, 9, 9, SkyLight::Max);

    world.SetBlock(9, 9, 9, BlockId{1});
    world.SetSkyLight(9, 9, 9, 0);

    CHECK(ChunkMesher::CornerLightShade(world, air, sideX, sideZ)
        == doctest::Approx(1.0f));
}

namespace
{
    //Locates the quad (a contiguous run of 4 vertices) whose corners exactly
    //match the given four positions, in any order. QuadsInPlane alone isn't
    //enough to isolate one specific face: two different faces (e.g. a block's
    //top and a neighbouring block's bottom) can share the same plane, so
    //filtering by plane can return more than one quad's vertices interleaved.
    std::size_t FindQuadByCorners(
        const ChunkMeshData& mesh, const glm::vec3 (&corners)[4])
    {
        for (std::size_t quad = 0; quad * 4 < mesh.Vertices.size(); ++quad)
        {
            bool matched[4] = { false, false, false, false };

            for (int i = 0; i < 4; ++i)
                for (int e = 0; e < 4; ++e)
                    if (mesh.Vertices[quad * 4 + i].Position == corners[e])
                        matched[e] = true;

            if (matched[0] && matched[1] && matched[2] && matched[3])
                return quad;
        }

        return static_cast<std::size_t>(-1);
    }
}

TEST_CASE("Ambient occlusion darkens exactly the occluded corner, not its neighbours")
{
    // Every existing AO test that goes through Build is permutation-invariant:
    // the lone-block tests have all four corners equal, and the inside-corner
    // test only checks the darkest value across a whole plane. Nothing pins
    // FaceGeometry's CornerU/CornerV signs to a specific vertex, so permuting
    // them would still pass every other test. This one binds the dark corner
    // to its exact position.
    World world(1, 1, 1);
    world.SetBlock(8, 8, 8, BlockId{1});
    world.SetBlock(9, 9, 9, BlockId{1}); // Diagonally across one top corner.
    FloodFullDaylight(world);

    const ChunkMeshData mesh = ChunkMesher::Build(world, 0, 0, 0);

    const glm::vec3 corners[4] =
    {
        { 8.0f, 9.0f, 9.0f }, { 9.0f, 9.0f, 9.0f },
        { 9.0f, 9.0f, 8.0f }, { 8.0f, 9.0f, 8.0f },
    };
    const std::size_t quad = FindQuadByCorners(mesh, corners);
    REQUIRE(quad != static_cast<std::size_t>(-1));

    const VoxelVertex* top = &mesh.Vertices[quad * 4];

    int darkIndex = -1;
    for (int i = 0; i < 4; ++i)
        if (top[i].Position == glm::vec3(9.0f, 9.0f, 9.0f))
            darkIndex = i;

    REQUIRE(darkIndex >= 0);

    std::vector<float> others;
    for (int i = 0; i < 4; ++i)
        if (i != darkIndex)
            others.push_back(top[i].Color.r);

    REQUIRE(others.size() == 3);
    CHECK(others[0] == doctest::Approx(others[1]));
    CHECK(others[1] == doctest::Approx(others[2]));
    CHECK(top[darkIndex].Color.r < others[0]);
}

TEST_CASE("A quad flips to split along its darker diagonal, at the right vertex")
{
    // With ao = {3, 2, 3, 3} (index 1 is the occluded corner at (9,9,9)),
    // ao[0]+ao[2] = 6 > ao[1]+ao[3] = 5, so the quad must flip: indices
    // first+1,+2,+3,+3,+0,+1 rather than the unflipped 0,1,2,2,3,0.
    World world(1, 1, 1);
    world.SetBlock(8, 8, 8, BlockId{1});
    world.SetBlock(9, 9, 9, BlockId{1});
    FloodFullDaylight(world);

    const ChunkMeshData mesh = ChunkMesher::Build(world, 0, 0, 0);

    const glm::vec3 corners[4] =
    {
        { 8.0f, 9.0f, 9.0f }, { 9.0f, 9.0f, 9.0f },
        { 9.0f, 9.0f, 8.0f }, { 8.0f, 9.0f, 8.0f },
    };
    const std::size_t quad = FindQuadByCorners(mesh, corners);
    REQUIRE(quad != static_cast<std::size_t>(-1));

    const std::uint32_t first = static_cast<std::uint32_t>(quad) * 4;
    const std::size_t indexBase = quad * 6;

    CHECK(mesh.Indices[indexBase + 0] == first + 1);
    CHECK(mesh.Indices[indexBase + 1] == first + 2);
    CHECK(mesh.Indices[indexBase + 2] == first + 3);
    CHECK(mesh.Indices[indexBase + 3] == first + 3);
    CHECK(mesh.Indices[indexBase + 4] == first + 0);
    CHECK(mesh.Indices[indexBase + 5] == first + 1);
}

namespace
{
    //Finds the vertex at one corner of a face, among vertices already narrowed
    //to that face. A corner position on its own is shared by the three faces
    //meeting there, so the caller picks the face first.
    const VoxelVertex& CornerAt(
        const std::vector<VoxelVertex>& face, float x, float z)
    {
        const VoxelVertex* found = nullptr;

        for (const VoxelVertex& vertex : face)
            if (vertex.Position.x == x && vertex.Position.z == z)
            {
                REQUIRE(found == nullptr); // Ambiguous, so the test is wrong.
                found = &vertex;
            }

        REQUIRE(found != nullptr);
        return *found;
    }

    //The red channel a top-face vertex ends up with, worked out from the shading
    //rule rather than read back from the mesher: the block's colour scaled by
    //the floor plus the finished shade, which for a top face is occlusion times
    //light.
    float ExpectedTopRed(const World& world, int ao, float lightShade)
    {
        const float lit = ChunkMesher::AoShade[ao] * lightShade;

        return world.GetBlockColor(BlockId{1}) .r *
            (ChunkMesher::LightFloor +
                (1.0f - ChunkMesher::LightFloor) * lit);
    }
}

TEST_CASE("Occlusion samples blocks across a chunk boundary")
{
    // A block on the last column of chunk 0, with its occluder sitting in
    // chunk 1. Meshing chunk 0 has to reach across the seam to find it: the
    // two corners on that side are occluded, the two on the far side are open.
    World world(2, 1, 1);
    const int edge = Chunk::Width - 1;

    world.SetBlock(edge, 8, 8, BlockId{1});
    world.SetBlock(Chunk::Width, 9, 8, BlockId{1}); // in the next chunk
    FloodFullDaylight(world);

    const ChunkMeshData mesh = ChunkMesher::Build(world, 0, 0, 0);
    const std::vector<VoxelVertex> top = QuadsInPlane(mesh, 1, 9.0f);
    REQUIRE(top.size() == 4);

    // The top face's four corners, named by the x they sit at.
    const float nearX = static_cast<float>(edge);      // 15, away from the seam
    const float farX = static_cast<float>(edge + 1);   // 16, against the seam

    CHECK(CornerAt(top, farX, 9.0f).Color.r ==
        doctest::Approx(ExpectedTopRed(world, 2, 1.0f)));
    CHECK(CornerAt(top, farX, 8.0f).Color.r ==
        doctest::Approx(ExpectedTopRed(world, 2, 1.0f)));

    CHECK(CornerAt(top, nearX, 9.0f).Color.r ==
        doctest::Approx(ExpectedTopRed(world, 3, 1.0f)));
    CHECK(CornerAt(top, nearX, 8.0f).Color.r ==
        doctest::Approx(ExpectedTopRed(world, 3, 1.0f)));
}

TEST_CASE("Corner light samples cells across a chunk boundary")
{
    // Same seam, but darkening cells in the next chunk instead of filling them.
    // Each corner averages the four cells around it, so the two corners against
    // the seam pick up the darkness in proportion to how many of their cells
    // lie beyond it.
    World world(2, 1, 1);
    const int edge = Chunk::Width - 1;

    world.SetBlock(edge, 8, 8, BlockId{1});
    FloodFullDaylight(world);

    // Two of the three cells the seam-side corners sample, blacked out.
    world.SetSkyLight(Chunk::Width, 9, 8, 0);
    world.SetSkyLight(Chunk::Width, 9, 9, 0);

    const ChunkMeshData mesh = ChunkMesher::Build(world, 0, 0, 0);
    const std::vector<VoxelVertex> top = QuadsInPlane(mesh, 1, 9.0f);
    REQUIRE(top.size() == 4);

    const float nearX = static_cast<float>(edge);
    const float farX = static_cast<float>(edge + 1);

    // Corner at (16, 9, 9) averages (15,9,8), (16,9,8), (15,9,9), (16,9,9):
    // two lit, two dark.
    CHECK(CornerAt(top, farX, 9.0f).Color.r ==
        doctest::Approx(ExpectedTopRed(world, 3, 0.5f)));

    // Corner at (16, 9, 8) averages (15,9,8), (16,9,8), (15,9,7), (16,9,7):
    // only the one dark cell.
    CHECK(CornerAt(top, farX, 8.0f).Color.r ==
        doctest::Approx(ExpectedTopRed(world, 3, 0.75f)));

    // The far side never crosses the seam, so it stays fully lit.
    CHECK(CornerAt(top, nearX, 9.0f).Color.r ==
        doctest::Approx(ExpectedTopRed(world, 3, 1.0f)));
    CHECK(CornerAt(top, nearX, 8.0f).Color.r ==
        doctest::Approx(ExpectedTopRed(world, 3, 1.0f)));
}
