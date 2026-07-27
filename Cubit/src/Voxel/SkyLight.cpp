#include "cub.h"

#include "Cubit/Voxel/SkyLight.h"

#include "Cubit/Voxel/Chunk.h"
#include "Cubit/Voxel/World.h"

#include <glm/glm.hpp>
#include <deque>

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
