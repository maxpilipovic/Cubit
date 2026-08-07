#include "cub.h"

#include "Cubit/Voxel/ChunkMesher.h"

#include "Cubit/Voxel/Chunk.h"
#include "Cubit/Voxel/SkyLight.h"
#include "Cubit/Voxel/World.h"

#include "Core/CoreLogger.h"
#include "Voxel/Neighbourhood.h"

#include <array>

namespace
{
    //Reads straight from the world, for the callers that hand one over rather
    //than meshing a whole chunk.
    struct WorldCells
    {
        const World& Cells;

        bool IsSolid(const glm::ivec3& cell) const
        {
            return Cells.IsBlockSolid(cell.x, cell.y, cell.z);
        }

        bool IsOpaque(const glm::ivec3& cell) const
        {
            return Cells.IsBlockOpaque(cell.x, cell.y, cell.z);
        }

        int Light(const glm::ivec3& cell) const
        {
            return Cells.GetSkyLight(cell.x, cell.y, cell.z);
        }
    };

    //How exposed one corner is. Written against anything that can answer
    //IsSolid and Light, so the chunk cache and a bare world share one rule
    //rather than drifting apart as two copies. A cell is whatever that source
    //addresses cells by — a flat index for the cache, a position for the world
    //— and a side is a step in the same terms.
    template <typename Cells, typename Cell, typename Side>
    int CornerAo(const Cells& cells, const Cell& airCell,
        const Side& sideA, const Side& sideB)
    {
        const bool solidA = cells.IsOpaque(airCell + sideA);
        const bool solidB = cells.IsOpaque(airCell + sideB);

        // Two walls meeting at a right angle seal the corner completely, so what
        // sits diagonally behind them cannot lighten it.
        if (solidA && solidB)
            return 0;

        const bool solidCorner = cells.IsOpaque(airCell + sideA + sideB);

        return 3
            - static_cast<int>(solidA)
            - static_cast<int>(solidB)
            - static_cast<int>(solidCorner);
    }

    //The light sitting at one corner: the mean of the open cells touching it.
    template <typename Cells, typename Cell, typename Side>
    float CornerLight(const Cells& cells, const Cell& airCell,
        const Side& sideA, const Side& sideB)
    {
        const Cell corners[4] =
        {
            airCell,
            airCell + sideA,
            airCell + sideB,
            airCell + sideA + sideB,
        };

        int total = 0;
        int counted = 0;

        for (const Cell& cell : corners)
        {
            if (cells.IsOpaque(cell))
                continue;

            total += cells.Light(cell);
            ++counted;
        }

        // A corner boxed in on every side has nowhere for light to sit; it is
        // not sampled by any visible face, but guard the division anyway.
        if (counted == 0)
            return 0.0f;

        return static_cast<float>(total) /
            (static_cast<float>(counted) * SkyLight::Max);
    }

    //Adds two triangles referencing the four vertices most recently appended.
    //A quad can be split along either diagonal; flipping picks the other one.
    void AddFaceIndices(MeshGeometry& mesh, bool flip)
    {
        CB_CORE_ASSERT(
            mesh.Vertices.size() >= 4,
            "A face must append its four vertices before its indices");

        const std::uint32_t firstVertex =
            static_cast<std::uint32_t>(mesh.Vertices.size()) - 4;

        if (flip)
        {
            mesh.Indices.push_back(firstVertex + 1);
            mesh.Indices.push_back(firstVertex + 2);
            mesh.Indices.push_back(firstVertex + 3);
            mesh.Indices.push_back(firstVertex + 3);
            mesh.Indices.push_back(firstVertex + 0);
            mesh.Indices.push_back(firstVertex + 1);
        }
        else
        {
            mesh.Indices.push_back(firstVertex + 0);
            mesh.Indices.push_back(firstVertex + 1);
            mesh.Indices.push_back(firstVertex + 2);
            mesh.Indices.push_back(firstVertex + 2);
            mesh.Indices.push_back(firstVertex + 3);
            mesh.Indices.push_back(firstVertex + 0);
        }
    }

    //Per-face brightness, so a solid-coloured block still reads as a cube.
    //Roughly the shading a single overhead light would give.
    constexpr float TopShade = 1.00f;
    constexpr float RightShade = 0.92f;
    constexpr float FrontShade = 0.86f;
    constexpr float LeftShade = 0.80f;
    constexpr float BackShade = 0.72f;
    constexpr float BottomShade = 0.60f;

    //One block face, described rather than hand-written. Corner holds the four
    //vertex offsets from the block's minimum corner, in the winding order the
    //face is emitted in. U and V are the two axes spanning the face, and
    //CornerU/CornerV give each vertex's sign along them — which is what lets a
    //corner's two occluding neighbours be found without a switch per face.
    struct FaceGeometry
    {
        glm::ivec3 Normal;
        glm::vec3 Corner[4];
        glm::ivec3 U;
        glm::ivec3 V;
        int CornerU[4];
        int CornerV[4];
        float Shade;
    };

    constexpr FaceGeometry Faces[6] =
    {
        // Front (+Z)
        { {  0,  0,  1 },
          { { 0.0f, 0.0f, 1.0f }, { 1.0f, 0.0f, 1.0f },
            { 1.0f, 1.0f, 1.0f }, { 0.0f, 1.0f, 1.0f } },
          { 1, 0, 0 }, { 0, 1, 0 },
          { -1, +1, +1, -1 }, { -1, -1, +1, +1 },
          FrontShade },

        // Back (-Z)
        { {  0,  0, -1 },
          { { 1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f },
            { 0.0f, 1.0f, 0.0f }, { 1.0f, 1.0f, 0.0f } },
          { 1, 0, 0 }, { 0, 1, 0 },
          { +1, -1, -1, +1 }, { -1, -1, +1, +1 },
          BackShade },

        // Right (+X)
        { {  1,  0,  0 },
          { { 1.0f, 0.0f, 1.0f }, { 1.0f, 0.0f, 0.0f },
            { 1.0f, 1.0f, 0.0f }, { 1.0f, 1.0f, 1.0f } },
          { 0, 0, 1 }, { 0, 1, 0 },
          { +1, -1, -1, +1 }, { -1, -1, +1, +1 },
          RightShade },

        // Left (-X)
        { { -1,  0,  0 },
          { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f },
            { 0.0f, 1.0f, 1.0f }, { 0.0f, 1.0f, 0.0f } },
          { 0, 0, 1 }, { 0, 1, 0 },
          { -1, +1, +1, -1 }, { -1, -1, +1, +1 },
          LeftShade },

        // Top (+Y)
        { {  0,  1,  0 },
          { { 0.0f, 1.0f, 1.0f }, { 1.0f, 1.0f, 1.0f },
            { 1.0f, 1.0f, 0.0f }, { 0.0f, 1.0f, 0.0f } },
          { 1, 0, 0 }, { 0, 0, 1 },
          { -1, +1, +1, -1 }, { +1, +1, -1, -1 },
          TopShade },

        // Bottom (-Y)
        { {  0, -1,  0 },
          { { 0.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f },
            { 1.0f, 0.0f, 1.0f }, { 0.0f, 0.0f, 1.0f } },
          { 1, 0, 0 }, { 0, 0, 1 },
          { -1, +1, +1, -1 }, { -1, -1, +1, +1 },
          BottomShade },
    };

    //Emits one face: four vertices shaded by their own corner occlusion, then
    //the two triangles joining them.
    //A face's normal and tangent axes as flat neighbourhood offsets, worked out
    //once per mesh instead of per block.
    struct FaceSteps
    {
        int Normal;
        int U;
        int V;
    };

    //The index of the one non-zero component of a unit axis vector: 0 is x,
    //1 is y, 2 is z. Every Normal, U and V in the Faces table is a unit axis.
    constexpr int AxisIndex(const glm::ivec3& axis)
    {
        return axis.x != 0 ? 0 : (axis.y != 0 ? 1 : 2);
    }

    //How many blocks a chunk spans along one axis.
    constexpr int AxisExtent(int axis)
    {
        return axis == 0 ? Chunk::Width : (axis == 1 ? Chunk::Height : Chunk::Depth);
    }

    //The four corner shades of one face, before they are turned into colours.
    struct FaceCorners
    {
        int Ao[4];
        float Light[4];
    };

    //Samples occlusion and light at a face's four corners.
    FaceCorners CornerShades(
        const Neighbourhood& cells,
        int airCell,
        const FaceGeometry& face,
        const FaceSteps& steps)
    {
        FaceCorners corners{};

        for (int i = 0; i < 4; ++i)
        {
            const int sideA = steps.U * face.CornerU[i];
            const int sideB = steps.V * face.CornerV[i];

            corners.Ao[i] = CornerAo(cells, airCell, sideA, sideB);
            corners.Light[i] = CornerLight(cells, airCell, sideA, sideB);
        }

        return corners;
    }

    //The colour one corner ends up wearing.
    glm::vec4 ShadeCorner(
        const glm::vec4& blockColor, float faceShade, int ao, float light)
    {
        const float lit = faceShade * ChunkMesher::AoShade[ao] * light;

        // The floor applies to the finished shading, not to light alone: the
        // three factors multiply, so flooring only the light term still lets an
        // occluded ceiling underside reach near-black.
        //
        // Shading scales the colour channels only. Alpha is the block's opacity
        // and has nothing to do with how lit the face is.
        const glm::vec3 shaded = glm::vec3(blockColor)
            * (ChunkMesher::LightFloor
                + (1.0f - ChunkMesher::LightFloor) * lit);

        return glm::vec4(shaded, blockColor.a);
    }

    //Emits one quad spanning w cells along the face's U axis and h along V.
    //A 1x1 quad is the w = h = 1 case, so merged and unmerged faces share one
    //emission path rather than drifting apart as two.
    void AddQuad(
        MeshGeometry& mesh,
        const FaceGeometry& face,
        const glm::vec3& origin,
        int w,
        int h,
        const glm::vec4 (&cornerColors)[4],
        bool flip)
    {
        for (int i = 0; i < 4; ++i)
        {
            // Corner already carries the unit step, so only the corners on the
            // far side of each tangent axis move, and only by the extra extent.
            const glm::vec3 stretched =
                glm::vec3(face.U) * static_cast<float>(face.CornerU[i] > 0 ? w - 1 : 0) +
                glm::vec3(face.V) * static_cast<float>(face.CornerV[i] > 0 ? h - 1 : 0);

            mesh.Vertices.push_back(
                { origin + face.Corner[i] + stretched, cornerColors[i] });
        }

        AddFaceIndices(mesh, flip);
    }

    //One cell of a face plane while it is being merged. Cells that hold no
    //face, and faces too varied to merge, are simply absent: those are emitted
    //as they are found rather than being carried through the merge pass.
    struct MaskCell
    {
        bool Present = false;
        bool Opaque = false;
        BlockId Block = 0;
        glm::vec4 Color{ 0.0f };
    };

    //Two cells merge only when they are the same block wearing the same colour.
    //Equal colour is what makes the merged quad's flat shading truthful; equal
    //block id costs one compare and keeps the rule readable.
    bool Mergeable(const MaskCell& cell, const MaskCell& key)
    {
        return cell.Present && cell.Block == key.Block && cell.Color == key.Color;
    }

    //Every chunk face plane is the same size, so one mask array serves all six
    //directions.
    static_assert(
        Chunk::Width == Chunk::Height && Chunk::Height == Chunk::Depth,
        "The face-plane mask assumes cubic chunks");

    constexpr int PlaneCells = Chunk::Width * Chunk::Width;

    //Emits every face pointing one direction, plane by plane. Walking planes
    //rather than blocks is what lets coplanar faces meet each other; a
    //block-major walk never has two faces of the same plane in hand at once.
    void MeshFacePlanes(
        ChunkMeshData& mesh,
        const Neighbourhood& cells,
        const Palette& palette,
        const FaceGeometry& face,
        const FaceSteps& steps)
    {
        const int normalAxis = AxisIndex(face.Normal);
        const int uAxis = AxisIndex(face.U);
        const int vAxis = AxisIndex(face.V);

        const int sliceCount = AxisExtent(normalAxis);
        const int uCount = AxisExtent(uAxis);
        const int vCount = AxisExtent(vAxis);

        // The flat index is linear in (slice, u, v), so it is carried forward
        // by addition instead of rebuilt from local coordinates on every cell.
        // steps.Normal is negative for the -X/-Y/-Z faces, but slice always
        // counts up from 0 along the axis, so the step that advances it has
        // to be the magnitude.
        const int planeOrigin = Neighbourhood::At(0, 0, 0);
        const int normalStep = steps.Normal < 0 ? -steps.Normal : steps.Normal;

        MaskCell mask[PlaneCells];

        for (int slice = 0; slice < sliceCount; ++slice)
        {
            const int sliceBase = planeOrigin + slice * normalStep;

            for (int v = 0; v < vCount; ++v)
            {
                int cell = sliceBase + v * steps.V;

                for (int u = 0; u < uCount; ++u, cell += steps.U)
                {
                    MaskCell& entry = mask[v * uCount + u];
                    entry = MaskCell{};

                    glm::ivec3 local(0);
                    local[normalAxis] = slice;
                    local[uAxis] = u;
                    local[vAxis] = v;

                    if (!cells.IsSolid(cell))
                        continue;

                    const BlockId self = cells.Block(cell);
                    const int neighbourCell = cell + steps.Normal;

                    // A face is worth drawing when what is beyond it does not
                    // hide it, and is not more of the same block: two water
                    // cells meet at a face that would only blend against itself.
                    if (cells.IsOpaque(neighbourCell) ||
                        cells.Block(neighbourCell) == self)
                        continue;

                    // A block's own opacity decides which pass draws it; the
                    // faces of one block never span both.
                    const bool opaque = cells.IsOpaque(cell);
                    MeshGeometry& target = opaque
                        ? mesh.Opaque
                        : mesh.Transparent;

                    const glm::vec4 blockColor = palette[self];
                    const FaceCorners corners =
                        CornerShades(cells, neighbourCell, face, steps);

                    const bool uniform =
                        corners.Ao[0] == corners.Ao[1] &&
                        corners.Ao[1] == corners.Ao[2] &&
                        corners.Ao[2] == corners.Ao[3] &&
                        corners.Light[0] == corners.Light[1] &&
                        corners.Light[1] == corners.Light[2] &&
                        corners.Light[2] == corners.Light[3];

                    if (!uniform)
                    {
                        // Corners that disagree cannot survive being stretched
                        // across a merged quad, because the shading in between
                        // is interpolated linearly from them. Emit it alone.
                        glm::vec4 colors[4];
                        for (int i = 0; i < 4; ++i)
                            colors[i] = ShadeCorner(blockColor, face.Shade,
                                corners.Ao[i], corners.Light[i]);

                        const bool flip = corners.Ao[0] + corners.Ao[2]
                            > corners.Ao[1] + corners.Ao[3];

                        AddQuad(target, face, glm::vec3(local), 1, 1,
                            colors, flip);
                        continue;
                    }

                    entry.Present = true;
                    entry.Opaque = opaque;
                    entry.Block = self;
                    entry.Color = ShadeCorner(blockColor, face.Shade,
                        corners.Ao[0], corners.Light[0]);
                }
            }

            for (int v = 0; v < vCount; ++v)
            {
                for (int u = 0; u < uCount; ++u)
                {
                    const MaskCell key = mask[v * uCount + u];
                    if (!key.Present)
                        continue;

                    // Widen along U first, then grow along V by whole rows of
                    // that width. Taking the widest row first is what makes the
                    // result a maximal rectangle rather than a ragged strip.
                    int w = 1;
                    while (u + w < uCount &&
                        Mergeable(mask[v * uCount + u + w], key))
                        ++w;

                    int h = 1;
                    while (v + h < vCount)
                    {
                        bool wholeRow = true;
                        for (int i = 0; i < w && wholeRow; ++i)
                            wholeRow =
                                Mergeable(mask[(v + h) * uCount + u + i], key);

                        if (!wholeRow)
                            break;

                        ++h;
                    }

                    for (int dv = 0; dv < h; ++dv)
                        for (int du = 0; du < w; ++du)
                            mask[(v + dv) * uCount + u + du].Present = false;

                    glm::ivec3 local(0);
                    local[normalAxis] = slice;
                    local[uAxis] = u;
                    local[vAxis] = v;

                    // A uniform quad's corners are equal, so neither diagonal
                    // is the darker one and the split never shows.
                    const glm::vec4 colors[4] =
                        { key.Color, key.Color, key.Color, key.Color };

                    AddQuad(key.Opaque ? mesh.Opaque : mesh.Transparent,
                        face, glm::vec3(local), w, h, colors, false);
                }
            }
        }
    }
}

ChunkMeshData ChunkMesher::Build(const World& world, int chunkX, int chunkY, int chunkZ)
{
    ChunkMeshData mesh;
    const glm::ivec3 origin = World::GetChunkOrigin(chunkX, chunkY, chunkZ);

    const Neighbourhood cells(world, origin);
    const Palette& palette = world.GetPalette();

    for (int f = 0; f < 6; ++f)
    {
        const FaceGeometry& face = Faces[f];
        const FaceSteps steps = {
            Neighbourhood::Step(face.Normal),
            Neighbourhood::Step(face.U),
            Neighbourhood::Step(face.V) };

        MeshFacePlanes(mesh, cells, palette, face, steps);
    }

    return mesh;
}

int ChunkMesher::CornerAoLevel(
    const World& world,
    const glm::ivec3& airCell,
    const glm::ivec3& sideA,
    const glm::ivec3& sideB)
{
    return CornerAo(WorldCells{ world }, airCell, sideA, sideB);
}

float ChunkMesher::CornerLightShade(
    const World& world,
    const glm::ivec3& airCell,
    const glm::ivec3& sideA,
    const glm::ivec3& sideB)
{
    return CornerLight(WorldCells{ world }, airCell, sideA, sideB);
}
