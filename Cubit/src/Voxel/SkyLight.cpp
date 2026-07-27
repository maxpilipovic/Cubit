#include "cub.h"

#include "Cubit/Voxel/SkyLight.h"

#include "Cubit/Voxel/Chunk.h"
#include "Cubit/Voxel/World.h"

#include <glm/glm.hpp>
#include <algorithm>
#include <deque>
#include <vector>

namespace
{
    //The six directions light can travel. Index 5 is straight down, which is
    //the one direction that can be free.
    constexpr glm::ivec3 Directions[6] =
    {
        {  1,  0,  0 },
        { -1,  0,  0 },
        {  0,  0,  1 },
        {  0,  0, -1 },
        {  0,  1,  0 },
        {  0, -1,  0 },
    };

    constexpr int DownIndex = 5;

    //Spreads light outward from every cell already in the queue until nothing
    //can be brightened. A cell is only enqueued when its value actually rises,
    //so this terminates: each cell can rise at most Max times.
    void Flood(World& world, std::deque<glm::ivec3>& queue)
    {
        while (!queue.empty())
        {
            const glm::ivec3 cell = queue.front();
            queue.pop_front();

            const int level = world.GetSkyLight(cell.x, cell.y, cell.z);
            if (level <= 1)
                continue; // Nothing left to give a neighbour.

            for (int d = 0; d < 6; ++d)
            {
                const glm::ivec3 next = cell + Directions[d];

                if (!world.IsInBounds(next.x, next.y, next.z))
                    continue;
                if (world.IsBlockSolid(next.x, next.y, next.z))
                    continue;

                // Full-strength sky light falls without dimming, which is what
                // makes a shaft lit all the way down. Light that has already
                // spread sideways pays a level to fall, like any other step.
                const int value = (d == DownIndex && level == SkyLight::Max)
                    ? SkyLight::Max
                    : level - 1;

                if (world.GetSkyLight(next.x, next.y, next.z) >= value)
                    continue;

                world.SetSkyLight(
                    next.x, next.y, next.z, static_cast<std::uint8_t>(value));
                queue.push_back(next);
            }
        }
    }
}

void SkyLight::PropagateAll(World& world)
{
    for (int z = 0; z < world.GetDepth(); ++z)
        for (int y = 0; y < world.GetHeight(); ++y)
            for (int x = 0; x < world.GetWidth(); ++x)
                world.SetSkyLight(x, y, z, 0);

    std::deque<glm::ivec3> queue;
    const int top = world.GetHeight() - 1;

    for (int z = 0; z < world.GetDepth(); ++z)
    {
        for (int x = 0; x < world.GetWidth(); ++x)
        {
            if (world.IsBlockSolid(x, top, z))
                continue;

            world.SetSkyLight(x, top, z, Max);
            queue.push_back(glm::ivec3(x, top, z));
        }
    }

    Flood(world, queue);
}

void SkyLight::Repropagate(World& world, int x, int y, int z)
{
    (void)y; // The box always spans the full height, so the edit's y is unused.

    const int radius = Max;
    const int minX = std::max(0, x - radius);
    const int maxX = std::min(world.GetWidth() - 1, x + radius);
    const int minZ = std::max(0, z - radius);
    const int maxZ = std::min(world.GetDepth() - 1, z + radius);
    const int top = world.GetHeight() - 1;

    // Remember what the box held, so only genuinely changed chunks are marked.
    std::vector<std::uint8_t> before;
    before.reserve(
        static_cast<std::size_t>(maxX - minX + 1) *
        (maxZ - minZ + 1) * (top + 1));

    for (int cz = minZ; cz <= maxZ; ++cz)
        for (int cy = 0; cy <= top; ++cy)
            for (int cx = minX; cx <= maxX; ++cx)
                before.push_back(world.GetSkyLight(cx, cy, cz));

    for (int cz = minZ; cz <= maxZ; ++cz)
        for (int cy = 0; cy <= top; ++cy)
            for (int cx = minX; cx <= maxX; ++cx)
                world.SetSkyLight(cx, cy, cz, 0);

    std::deque<glm::ivec3> queue;

    // Seed one: the open sky above the box.
    for (int cz = minZ; cz <= maxZ; ++cz)
    {
        for (int cx = minX; cx <= maxX; ++cx)
        {
            if (world.IsBlockSolid(cx, top, cz))
                continue;

            world.SetSkyLight(cx, top, cz, Max);
            queue.push_back(glm::ivec3(cx, top, cz));
        }
    }

    // Seed two: the ring of cells just outside the box. They kept their values
    // through the clear, and light flows from them back in. Enqueuing them is
    // safe because Flood only ever raises a cell, and nothing outside the box
    // can be too dark.
    for (int cy = 0; cy <= top; ++cy)
    {
        for (int cz = minZ - 1; cz <= maxZ + 1; ++cz)
        {
            for (int cx = minX - 1; cx <= maxX + 1; ++cx)
            {
                const bool insideBox =
                    cx >= minX && cx <= maxX && cz >= minZ && cz <= maxZ;

                if (insideBox)
                    continue;
                if (!world.IsInBounds(cx, cy, cz))
                    continue;
                if (world.IsBlockSolid(cx, cy, cz))
                    continue;
                if (world.GetSkyLight(cx, cy, cz) == 0)
                    continue;

                queue.push_back(glm::ivec3(cx, cy, cz));
            }
        }
    }

    Flood(world, queue);

    std::size_t index = 0;
    for (int cz = minZ; cz <= maxZ; ++cz)
    {
        for (int cy = 0; cy <= top; ++cy)
        {
            for (int cx = minX; cx <= maxX; ++cx)
            {
                if (world.GetSkyLight(cx, cy, cz) != before[index])
                    world.MarkChunkDirtyAt(cx, cy, cz);

                ++index;
            }
        }
    }
}
