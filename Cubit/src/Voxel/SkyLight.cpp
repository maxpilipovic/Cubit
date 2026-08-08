#include "cub.h"

#include "Cubit/Voxel/SkyLight.h"

#include "Cubit/Voxel/World.h"

#include <glm/glm.hpp>
#include <deque>
#include <map>
#include <utility>

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

    //Remembers what every cell it writes held beforehand, so that once the
    //light has settled it can mark exactly the chunks whose light really moved.
    //Relighting an edit blanks cells and refills most of them to the value they
    //already had; those cost no remeshing, and only the genuine changes should
    //reach the renderer.
    class LightRecorder
    {
    public:
        explicit LightRecorder(World& world) : m_World(world) {}

        void Set(const glm::ivec3& cell, int level)
        {
            //Only the first write records, so the original survives however
            //many times a cell is rewritten on the way to its final value.
            m_Original.try_emplace(
                cell, m_World.GetSkyLight(cell.x, cell.y, cell.z));

            m_World.SetSkyLight(
                cell.x, cell.y, cell.z, static_cast<std::uint8_t>(level));
        }

        void MarkChangedChunks() const
        {
            for (const auto& entry : m_Original)
            {
                const glm::ivec3& cell = entry.first;

                if (m_World.GetSkyLight(cell.x, cell.y, cell.z) != entry.second)
                    m_World.MarkChunkDirtyAt(cell.x, cell.y, cell.z);
            }
        }

    private:
        World& m_World;
        std::map<glm::ivec3, std::uint8_t, IVec3Less> m_Original;
    };

    //Writes a light value, through the recorder when there is one. A full
    //propagation rewrites the whole world and has nothing to compare against,
    //so it passes none.
    void SetLight(World& world, LightRecorder* recorder,
        const glm::ivec3& cell, int level)
    {
        if (recorder != nullptr)
            recorder->Set(cell, level);
        else
            world.SetSkyLight(
                cell.x, cell.y, cell.z, static_cast<std::uint8_t>(level));
    }

    //Spreads light outward from every cell already in the queue until nothing
    //can be brightened. A cell is only enqueued when its value actually rises,
    //so this terminates: each cell can rise at most Max times.
    void Flood(World& world, std::deque<glm::ivec3>& queue,
        LightRecorder* recorder)
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
                if (world.IsBlockOpaque(next.x, next.y, next.z))
                    continue;

                // Full-strength sky light falls without dimming, which is what
                // makes a shaft lit all the way down. Light that has already
                // spread sideways pays a level to fall, like any other step.
                const int value = (d == DownIndex && level == SkyLight::Max)
                    ? SkyLight::Max
                    : level - 1;

                if (world.GetSkyLight(next.x, next.y, next.z) >= value)
                    continue;

                SetLight(world, recorder, next, value);
                queue.push_back(next);
            }
        }
    }

    //Takes light back out of the region a newly solid cell used to light, and
    //collects the still-lit cells bordering that region into readd, so the
    //caller can let them spread back in. Clearing a cell that some other source
    //still reaches is harmless: the flood that follows only raises values, and
    //that source is in readd.
    void Unflood(World& world, LightRecorder& recorder,
        const glm::ivec3& origin, std::deque<glm::ivec3>& readd)
    {
        //The cell still holds the light it had while it was open, which is
        //exactly the light the new block has just cut off.
        std::deque<std::pair<glm::ivec3, int>> queue;
        queue.emplace_back(origin, world.GetSkyLight(origin.x, origin.y, origin.z));
        recorder.Set(origin, 0);

        while (!queue.empty())
        {
            const glm::ivec3 cell = queue.front().first;
            const int level = queue.front().second;
            queue.pop_front();

            if (level <= 0)
                continue; // Lit nothing, so it darkened nothing.

            for (int d = 0; d < 6; ++d)
            {
                const glm::ivec3 next = cell + Directions[d];

                if (!world.IsInBounds(next.x, next.y, next.z))
                    continue;
                if (world.IsBlockOpaque(next.x, next.y, next.z))
                    continue;

                const int nextLevel = world.GetSkyLight(next.x, next.y, next.z);
                if (nextLevel == 0)
                    continue; // Already dark; nothing to take back.

                // A dimmer neighbour was lit by this cell. So was the cell
                // directly below a free-falling one, which holds the *same*
                // level rather than a lower one — the case that separates sky
                // light from a plain flood fill, and the one a removal that
                // only looks for dimmer neighbours leaves wrongly lit.
                const bool litByThisCell =
                    nextLevel < level ||
                    (d == DownIndex && level == SkyLight::Max &&
                        nextLevel == SkyLight::Max);

                if (litByThisCell)
                {
                    queue.emplace_back(next, nextLevel);
                    recorder.Set(next, 0);
                }
                else
                {
                    // Lit by something the edit did not touch, so it survives
                    // and can fill the space just cleared.
                    readd.push_back(next);
                }
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
            if (world.IsBlockOpaque(x, top, z))
                continue;

            world.SetSkyLight(x, top, z, Max);
            queue.push_back(glm::ivec3(x, top, z));
        }
    }

    Flood(world, queue, nullptr);
}

void SkyLight::Repropagate(World& world, int x, int y, int z)
{
    const glm::ivec3 edit(x, y, z);

    LightRecorder recorder(world);
    std::deque<glm::ivec3> queue;

    if (world.IsBlockOpaque(x, y, z))
    {
        // The edit put something light-stopping here. Take back the light it
        // used to give, keeping whatever still-lit cells border the darkened
        // region.
        Unflood(world, recorder, edit, queue);
    }
    else
    {
        // The edit left this cell passable to light — either it opened it up,
        // or what it placed is transparent. A cell at the very top is open sky
        // and lights itself; anywhere else, the surrounding cells fill it in.
        if (y == world.GetHeight() - 1)
        {
            recorder.Set(edit, Max);
            queue.push_back(edit);
        }

        for (const glm::ivec3& direction : Directions)
        {
            const glm::ivec3 next = edit + direction;

            if (!world.IsInBounds(next.x, next.y, next.z))
                continue;
            if (world.GetSkyLight(next.x, next.y, next.z) == 0)
                continue;

            queue.push_back(next);
        }
    }

    Flood(world, queue, &recorder);
    recorder.MarkChangedChunks();
}
