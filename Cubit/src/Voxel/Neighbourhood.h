#pragma once

#include "Cubit/Voxel/Block.h"
#include "Cubit/Voxel/Chunk.h"
#include "Cubit/Voxel/World.h"

#include <glm/glm.hpp>
#include <cstdint>

//A chunk plus the one-block shell around it, copied out of the world once
//per mesh. Occlusion and light sampling reach exactly one cell past a block
//on each axis, so one block of shell is enough to mesh without touching the
//world again — which matters because resolving a world position to a chunk
//and an offset, tens of thousands of times a chunk, was most of what
//meshing cost.
//
//Reads outside the world go through World on the way in, so the shell keeps
//its answers: air, and open sky.
class Neighbourhood
{
public:
    static constexpr int Shell = 1;
    static constexpr int SpanX = Chunk::Width + 2 * Shell;
    static constexpr int SpanY = Chunk::Height + 2 * Shell;
    static constexpr int SpanZ = Chunk::Depth + 2 * Shell;
    static constexpr int Count = SpanX * SpanY * SpanZ;

    Neighbourhood(const World& world, const glm::ivec3& chunkOrigin)
    {
        //The middle of the neighbourhood is exactly one chunk, which can be
        //read by local coordinates; only the shell has to go through the
        //world and pay for the conversion.
        const Chunk& chunk = world.GetChunk(
            chunkOrigin.x / Chunk::Width,
            chunkOrigin.y / Chunk::Height,
            chunkOrigin.z / Chunk::Depth);

        const glm::ivec3 base = chunkOrigin - glm::ivec3(Shell);
        int index = 0;

        for (int z = 0; z < SpanZ; ++z)
        {
            const bool insideZ = z >= Shell && z < Shell + Chunk::Depth;

            for (int y = 0; y < SpanY; ++y)
            {
                const bool insideY =
                    insideZ && y >= Shell && y < Shell + Chunk::Height;

                for (int x = 0; x < SpanX; ++x, ++index)
                {
                    if (insideY && x >= Shell && x < Shell + Chunk::Width)
                    {
                        m_Blocks[index] = chunk.GetBlock(
                            x - Shell, y - Shell, z - Shell);
                        m_Light[index] = chunk.GetSkyLight(
                            x - Shell, y - Shell, z - Shell);
                        m_Opaque[index] = world.IsIdOpaque(m_Blocks[index]);
                        continue;
                    }

                    const glm::ivec3 cell = base + glm::ivec3(x, y, z);
                    m_Blocks[index] =
                        world.GetBlock(cell.x, cell.y, cell.z);
                    m_Light[index] =
                        world.GetSkyLight(cell.x, cell.y, cell.z);
                    m_Opaque[index] = world.IsIdOpaque(m_Blocks[index]);
                }
            }
        }
    }

    //The flat index of a block at chunk-local coordinates.
    static int At(int localX, int localY, int localZ)
    {
        return (localX + Shell) +
            SpanX * ((localY + Shell) + SpanY * (localZ + Shell));
    }

    //The flat offset matching a step of v. Linear in v, so scaling a face's
    //tangent axis scales its offset the same way — which is what lets a
    //corner be addressed by adding two integers.
    static int Step(const glm::ivec3& v)
    {
        return v.x + SpanX * v.y + SpanX * SpanY * v.z;
    }

    BlockId Block(int cell) const { return m_Blocks[cell]; }
    bool IsSolid(int cell) const { return ::IsSolid(m_Blocks[cell]); }
    bool IsOpaque(int cell) const { return m_Opaque[cell]; }
    int Light(int cell) const { return m_Light[cell]; }

private:
    //Raw arrays rather than std::array: meshing subscripts these tens of
    //thousands of times per chunk, and a debug build checks every
    //std::array subscript. Every index comes from At plus a Step, so it
    //stays inside the shell by construction.
    BlockId m_Blocks[Count];
    std::uint8_t m_Light[Count];
    bool m_Opaque[Count];
};
