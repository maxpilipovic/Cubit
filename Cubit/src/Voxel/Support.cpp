#include "cub.h"

#include "Cubit/Voxel/Support.h"

#include "Cubit/Voxel/World.h"

#include <cstdint>
#include <unordered_set>

namespace
{
    const glm::ivec3 Neighbours[6] = {
        glm::ivec3(1, 0, 0), glm::ivec3(-1, 0, 0),
        glm::ivec3(0, 1, 0), glm::ivec3(0, -1, 0),
        glm::ivec3(0, 0, 1), glm::ivec3(0, 0, -1)
    };

    //The order a search pushes its neighbours in, and the reason it is fast. The
    //anchor is straight down, and the search takes the cell it pushed LAST
    //first, so the cell below goes on last and is followed first: the search
    //dives to the bottom layer in about as many steps as the piece is tall,
    //rather than walking everything within reach of the dig on the way.
    //
    //Both mistakes here were measured, on one dig into the shipped map: spreading
    //out evenly cost 2.4 ms, and this list the other way round - which climbs
    //instead of diving - cost 17 ms. It changes no answer, only the cost: which
    //cells a piece holds does not depend on the order they are found in.
    const glm::ivec3 Diving[6] = {
        glm::ivec3(0, 1, 0),
        glm::ivec3(1, 0, 0), glm::ivec3(-1, 0, 0),
        glm::ivec3(0, 0, 1), glm::ivec3(0, 0, -1),
        glm::ivec3(0, -1, 0)
    };

    //One cell as a single key. A world is at most a few thousand cells on a
    //side, so 21 bits an axis is room to spare, and packing beats hashing three
    //ints in a search that visits thousands of cells.
    std::uint64_t Key(const glm::ivec3& cell)
    {
        return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(cell.x)) << 42)
            ^ (static_cast<std::uint64_t>(static_cast<std::uint32_t>(cell.y)) << 21)
            ^ static_cast<std::uint64_t>(static_cast<std::uint32_t>(cell.z));
    }
}

std::vector<glm::ivec3> FindUnsupported(const World& world,
    std::span<const glm::ivec3> emptied, std::size_t maxSearch)
{
    std::vector<glm::ivec3> loose;

    //Cells already known to be held up - by an earlier search in this call, or
    //by one that gave up at maxSearch. Two neighbours of one emptied cell are
    //usually the same piece, and this is what stops it being walked twice.
    std::unordered_set<std::uint64_t> settled;

    std::unordered_set<std::uint64_t> piece;

    //Every cell of the piece found so far, and the ones whose neighbours have
    //still to be looked at. Kept apart because the answer needs all of them and
    //the search needs a stack.
    std::vector<glm::ivec3> found;
    std::vector<glm::ivec3> pending;

    for (const glm::ivec3& cell : emptied)
    {
        for (const glm::ivec3& direction : Neighbours)
        {
            const glm::ivec3 start = cell + direction;

            if (!world.IsBlockSolid(start.x, start.y, start.z))
                continue;
            if (settled.contains(Key(start)))
                continue;

            //Walk this piece through touching solid cells, stopping the moment a
            //cell on the bottom layer turns up, since that holds all of it up.
            piece.clear();
            found.clear();
            pending.clear();
            piece.insert(Key(start));
            found.push_back(start);
            pending.push_back(start);

            bool anchored = start.y == 0;
            while (!pending.empty() && !anchored)
            {
                const glm::ivec3 at = pending.back();
                pending.pop_back();

                if (found.size() > maxSearch)
                {
                    anchored = true;
                    break;
                }

                for (const glm::ivec3& step : Diving)
                {
                    const glm::ivec3 into = at + step;

                    if (!world.IsBlockSolid(into.x, into.y, into.z))
                        continue;
                    if (!piece.insert(Key(into)).second)
                        continue;

                    found.push_back(into);

                    if (into.y == 0)
                    {
                        anchored = true;
                        break;
                    }

                    pending.push_back(into);
                }
            }

            if (anchored)
            {
                //Every cell walked is held up by whatever stopped the search -
                //the bottom layer, or the cap - so none of them is asked again.
                settled.insert(piece.begin(), piece.end());
                continue;
            }

            loose.insert(loose.end(), found.begin(), found.end());

            //Cells that come loose are not asked again either: they are already
            //in the answer, and a later neighbour reaching them would repeat
            //them.
            settled.insert(piece.begin(), piece.end());
        }
    }

    return loose;
}
