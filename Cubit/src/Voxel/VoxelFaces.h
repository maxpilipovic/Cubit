#pragma once

#include <glm/glm.hpp>

#include <cstdint>

#include "Cubit/Voxel/ChunkMesher.h"

#include "Core/CoreLogger.h"

//What a block face looks like, shared by every mesher that walks a voxel
//grid. One table, one shading formula, one occlusion rule and one way of
//splitting a quad, so a second mesher cannot drift from the first by a
//different winding, a different shade, a different corner occlusion, or a
//different diagonal split.
namespace VoxelFaces
{
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
    struct Face
    {
        glm::ivec3 Normal;
        glm::vec3 Corner[4];
        glm::ivec3 U;
        glm::ivec3 V;
        int CornerU[4];
        int CornerV[4];
        float Shade;
    };

    constexpr Face All[6] =
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

    //One vertex's colour: the block's colour dimmed by which way the face
    //points, how enclosed this corner is, and how much light reaches it.
    //
    //AoShade and LightFloor stay on ChunkMesher, which is public and which
    //ChunkMesherTests names directly. This header reads them rather than
    //holding a second copy - two tables that must agree are a drift waiting to
    //happen, and the whole point of this file is that there is one of each.
    //
    //Shading scales the colour channels only. Alpha is the block's opacity and
    //has nothing to do with how lit the face is.
    inline glm::vec4 ShadeVertex(const glm::vec4& blockColor, float faceShade, int ao, float light)
    {
        const float lit = faceShade * ChunkMesher::AoShade[ao] * light;
        const glm::vec3 shaded = glm::vec3(blockColor)
            * (ChunkMesher::LightFloor + (1.0f - ChunkMesher::LightFloor) * lit);
        return glm::vec4(shaded, blockColor.a);
    }

    //How exposed one corner is. Written against anything that can answer
    //IsOpaque, so the chunk cache, a bare world and a standalone model share one
    //rule rather than drifting apart as three copies. A cell is whatever that
    //source addresses cells by — a flat index for the cache, a position for the
    //world — and a side is a step in the same terms.
    template <typename Cells, typename Cell, typename Side>
    int CornerAo(const Cells& cells, const Cell& openCell,
        const Side& sideA, const Side& sideB)
    {
        const bool opaqueA = cells.IsOpaque(openCell + sideA);
        const bool opaqueB = cells.IsOpaque(openCell + sideB);

        // Two walls meeting at a right angle seal the corner completely, so what
        // sits diagonally behind them cannot lighten it.
        if (opaqueA && opaqueB)
            return 0;

        const bool opaqueCorner = cells.IsOpaque(openCell + sideA + sideB);

        return 3
            - static_cast<int>(opaqueA)
            - static_cast<int>(opaqueB)
            - static_cast<int>(opaqueCorner);
    }

    //Adds two triangles referencing the four vertices most recently appended,
    //taking the same four corner occlusion levels those vertices were shaded by.
    //
    //A quad can be split along either diagonal. Splitting it along its darker
    //one keeps the shading gradient smooth; splitting the other way leaves a
    //visible seam across it. Which diagonal that is falls out of the corner
    //levels, so the choice is made here rather than asked of every caller.
    inline void AddFaceIndices(MeshGeometry& mesh, const int (&ao)[4])
    {
        CB_CORE_ASSERT(
            mesh.Vertices.size() >= 4,
            "A face must append its four vertices before its indices");

        const std::uint32_t firstVertex =
            static_cast<std::uint32_t>(mesh.Vertices.size()) - 4;

        if (ao[0] + ao[2] > ao[1] + ao[3])
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
}
