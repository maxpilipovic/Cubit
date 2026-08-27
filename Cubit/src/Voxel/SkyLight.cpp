#include "cub.h"

#include "Cubit/Voxel/SkyLight.h"

#include "Cubit/Voxel/Chunk.h"
#include "Cubit/Voxel/World.h"
#include "Cubit/Profiler.h"

#include <glm/glm.hpp>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <utility>
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

    //The four directions a column can lose light to. Vertical neighbours need
    //no entry: below a lit cell is another lit cell or the opaque block the
    //scan stopped on, and above the topmost is outside the world.
    constexpr glm::ivec2 HorizontalSteps[4] =
    {
        {  1,  0 }, { -1,  0 }, {  0,  1 }, {  0, -1 },
    };

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

    //The one rule for how sky light spreads, over whichever source of cells it
    //is handed. A cell is only enqueued when its value actually rises, so this
    //terminates: each cell can rise at most Max times.
    //
    //A template rather than two copies for the reason P7 gave when the mesher
    //grew a second way to reach a cell: the rule is subtle - the free fall in
    //the middle of it is the whole difference between sky light and a plain
    //flood fill - and two copies of a subtle rule drift. Here one caller
    //addresses cells through World and the other through a flat array, and
    //only the addressing differs.
    template <typename Cells>
    void Flood(Cells& cells, std::vector<typename Cells::Cell>& queue)
    {
        //Indexing rather than popping: the queue only grows, so a cursor over
        //it is the same traversal order with no per-cell deallocation. It also
        //keeps every cell visited around, which is what makes this cheap in a
        //debug build, where a std::deque pop is several checked operations.
        for (std::size_t head = 0; head < queue.size(); ++head)
        {
            //By value, before any push_back below can reallocate the queue.
            const typename Cells::Cell cell = queue[head];

            const int level = cells.Get(cell);
            if (level <= 1)
                continue; // Nothing left to give a neighbour.

            for (int d = 0; d < 6; ++d)
            {
                const typename Cells::Cell next = cells.Step(cell, d);

                if (cells.IsOpaque(next))
                    continue;

                // Full-strength sky light falls without dimming, which is what
                // makes a shaft lit all the way down. Light that has already
                // spread sideways pays a level to fall, like any other step.
                const int value = (d == DownIndex && level == SkyLight::Max)
                    ? SkyLight::Max
                    : level - 1;

                if (cells.Get(next) >= value)
                    continue;

                cells.Set(next, value);
                queue.push_back(next);
            }
        }
    }

    //Cells addressed through World. The edit path uses this: it settles a
    //handful of cells and would rather not build a copy of the world to do it.
    class WorldCells
    {
    public:
        using Cell = glm::ivec3;

        WorldCells(World& world, LightRecorder* recorder)
            : m_World(world), m_Recorder(recorder) {}

        Cell Step(const Cell& cell, int d) const { return cell + Directions[d]; }

        //Outside the world reads as opaque, so the flood stops at the edge.
        //That is the same thing the separate bounds check used to do, folded
        //into the question the flood was going to ask anyway.
        bool IsOpaque(const Cell& cell) const
        {
            return !m_World.IsInBounds(cell.x, cell.y, cell.z) ||
                m_World.IsBlockOpaque(cell.x, cell.y, cell.z);
        }

        int Get(const Cell& cell) const
        {
            return m_World.GetSkyLight(cell.x, cell.y, cell.z);
        }

        void Set(const Cell& cell, int level)
        {
            SetLight(m_World, m_Recorder, cell, level);
        }

    private:
        World& m_World;
        LightRecorder* m_Recorder;
    };

    //The whole world's opacity and sky light, each copied into one flat array,
    //so a neighbour is reached by adding a constant instead of by a bounds
    //check, three divides and three modulos through World.
    //
    //Every side is padded by one cell, and the padding is opaque. The flood
    //skips an opaque neighbour anyway, so a border that is always opaque stops
    //it at the world edge without a bounds check ever being written - the same
    //trick, for the same reason, as ChunkMesher's 18-cube neighbourhood.
    //
    //Cells are laid out y-fastest, so a column is contiguous. That is the
    //opposite of how a Chunk stores its cells, and deliberately: the scan
    //walks every column of the world top to bottom and is the pass worth
    //laying out for, while the flood only ever visits what borders darkness.
    class LightField
    {
    public:
        using Cell = std::int32_t;

        explicit LightField(const World& world)
            : m_Width(world.GetWidth())
            , m_Height(world.GetHeight())
            , m_Depth(world.GetDepth())
            , m_StrideX(world.GetHeight() + 2)
            , m_StrideZ((world.GetHeight() + 2) * (world.GetWidth() + 2))
        {
            const std::size_t count = static_cast<std::size_t>(m_StrideZ) *
                static_cast<std::size_t>(m_Depth + 2);

            //Opaque everywhere to begin with, so the border needs no separate
            //pass - only the interior is overwritten below.
            m_Opaque.assign(count, 1);
            m_Light.assign(count, 0);

            m_Origin = 1 + m_StrideX + m_StrideZ;

            //Ordered to match Directions, so index 5 is still straight down.
            m_Offsets[0] =  m_StrideX;  m_Offsets[1] = -m_StrideX;
            m_Offsets[2] =  m_StrideZ;  m_Offsets[3] = -m_StrideZ;
            m_Offsets[4] =  1;          m_Offsets[5] = -1;

            Gather(world);
        }

        //The flat index of a world cell.
        Cell At(int x, int y, int z) const
        {
            return m_Origin + y + x * m_StrideX + z * m_StrideZ;
        }

        Cell Step(Cell cell, int d) const { return cell + m_Offsets[d]; }
        bool IsOpaque(Cell cell) const { return m_Opaque[cell] != 0; }
        int Get(Cell cell) const { return m_Light[cell]; }
        void Set(Cell cell, int level)
        {
            m_Light[cell] = static_cast<std::uint8_t>(level);
        }

        //Writes the light every column gets straight from the sky, and records
        //in skyBottom the lowest cell each column reaches that way.
        //
        //Only the lit run is written. Everything from the first opaque block
        //down is already 0 from construction, where the old scan had to blank
        //it explicitly because it was writing over the previous load's light.
        void ScanColumns(std::vector<int>& skyBottom)
        {
            CB_PROFILE_SCOPE("PropagateAll/Scan");

            for (int z = 0; z < m_Depth; ++z)
            {
                for (int x = 0; x < m_Width; ++x)
                {
                    const std::size_t base = static_cast<std::size_t>(At(x, 0, z));
                    const std::uint8_t* opaque = m_Opaque.data() + base;
                    std::uint8_t* light = m_Light.data() + base;

                    int y = m_Height - 1;

                    for (; y >= 0 && opaque[y] == 0; --y)
                        light[y] = SkyLight::Max;

                    skyBottom[static_cast<std::size_t>(x) +
                        static_cast<std::size_t>(m_Width) *
                        static_cast<std::size_t>(z)] = y + 1;
                }
            }
        }

        //Copies the settled light back into the world's chunks.
        void ScatterInto(World& world) const
        {
            CB_PROFILE_SCOPE("PropagateAll/Scatter");

            ForEachChunk(world.GetChunksX(), world.GetChunksY(),
                world.GetChunksZ(),
                [&](int cx, int cy, int cz, int ox, int oy, int oz)
                {
                    std::uint8_t* light =
                        world.GetChunkForWrite(cx, cy, cz).SkyLightData();

                    for (int lz = 0; lz < Chunk::Depth; ++lz)
                    {
                        for (int lx = 0; lx < Chunk::Width; ++lx)
                        {
                            std::uint8_t* dst = light + lx +
                                Chunk::Width * Chunk::Height * lz;
                            const std::uint8_t* src = m_Light.data() +
                                At(ox + lx, oy, oz + lz);

                            for (int ly = 0; ly < Chunk::Height; ++ly)
                                dst[ly * Chunk::Width] = src[ly];
                        }
                    }
                });
        }

    private:
        //Walks the chunk grid, handing the body each chunk's grid position and
        //the world position of its minimum corner. Both bulk passes want
        //exactly this, and neither wants to recompute the origin inline.
        template <typename Body>
        static void ForEachChunk(int chunksX, int chunksY, int chunksZ, Body body)
        {
            for (int cz = 0; cz < chunksZ; ++cz)
                for (int cy = 0; cy < chunksY; ++cy)
                    for (int cx = 0; cx < chunksX; ++cx)
                        body(cx, cy, cz,
                            cx * Chunk::Width,
                            cy * Chunk::Height,
                            cz * Chunk::Depth);
        }

        //Copies the world's opacity in, a chunk at a time.
        //
        //The inner loop runs along y so the writes into this field are
        //sequential; that makes the reads out of the chunk stride by
        //Chunk::Width instead, which costs nothing because a chunk's 4096
        //cells are already in cache by the second of them.
        void Gather(const World& world)
        {
            CB_PROFILE_SCOPE("PropagateAll/Gather");

            //Resolving opacity per cell would mean 16.8M calls into World for
            //an answer with only 256 possible questions.
            std::uint8_t opaqueById[256];
            for (int id = 0; id < 256; ++id)
            {
                opaqueById[id] =
                    world.IsIdOpaque(static_cast<BlockId>(id)) ? 1 : 0;
            }

            ForEachChunk(world.GetChunksX(), world.GetChunksY(),
                world.GetChunksZ(),
                [&](int cx, int cy, int cz, int ox, int oy, int oz)
                {
                    const BlockId* blocks =
                        world.GetChunk(cx, cy, cz).BlockData();

                    for (int lz = 0; lz < Chunk::Depth; ++lz)
                    {
                        for (int lx = 0; lx < Chunk::Width; ++lx)
                        {
                            const BlockId* src = blocks + lx +
                                Chunk::Width * Chunk::Height * lz;
                            std::uint8_t* dst = m_Opaque.data() +
                                At(ox + lx, oy, oz + lz);

                            for (int ly = 0; ly < Chunk::Height; ++ly)
                                dst[ly] = opaqueById[src[ly * Chunk::Width]];
                        }
                    }
                });
        }

        int m_Width = 0;
        int m_Height = 0;
        int m_Depth = 0;
        int m_StrideX = 0;
        int m_StrideZ = 0;
        Cell m_Origin = 0;

        Cell m_Offsets[6]{};

        std::vector<std::uint8_t> m_Opaque;
        std::vector<std::uint8_t> m_Light;
    };

    //Seeds only the lit cells that border an unlit one. A lit cell whose every
    //neighbour is lit or opaque can brighten nothing, so queueing it would
    //cost six neighbour tests to discover it has nothing to do.
    void SeedBoundaries(const LightField& field, int width, int depth,
        const std::vector<int>& skyBottom, std::vector<LightField::Cell>& queue)
    {
        CB_PROFILE_SCOPE("PropagateAll/Seed");

        for (int z = 0; z < depth; ++z)
        {
            for (int x = 0; x < width; ++x)
            {
                const int lit = skyBottom[static_cast<std::size_t>(x) +
                    static_cast<std::size_t>(width) *
                    static_cast<std::size_t>(z)];

                //How far down a neighbouring column stays dark. Taking the
                //deepest of the four gives the union of their ranges in one
                //span: every range starts at this column's own lit floor, so
                //they nest.
                int deepest = lit;

                for (const glm::ivec2& step : HorizontalSteps)
                {
                    const int nx = x + step.x;
                    const int nz = z + step.y;

                    //Outside the world contributes no darkness, matching the
                    //opaque border the flood itself stops on.
                    if (nx < 0 || nz < 0 || nx >= width || nz >= depth)
                        continue;

                    deepest = std::max(deepest,
                        skyBottom[static_cast<std::size_t>(nx) +
                            static_cast<std::size_t>(width) *
                            static_cast<std::size_t>(nz)]);
                }

                for (int y = lit; y < deepest; ++y)
                    queue.push_back(field.At(x, y, z));
            }
        }
    }

    //Takes light back out of the region a newly solid cell used to light, and
    //collects the still-lit cells bordering that region into readd, so the
    //caller can let them spread back in. Clearing a cell that some other source
    //still reaches is harmless: the flood that follows only raises values, and
    //that source is in readd.
    void Unflood(World& world, LightRecorder& recorder,
        const glm::ivec3& origin, std::vector<glm::ivec3>& readd)
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
    CB_PROFILE_SCOPE("SkyLight::PropagateAll");

    const int width = world.GetWidth();
    const int depth = world.GetDepth();

    LightField field(world);

    //The lowest cell in each column that sky light reaches directly, or height
    //when the column is closed at the very top and reaches nothing.
    std::vector<int> skyBottom(
        static_cast<std::size_t>(width) * static_cast<std::size_t>(depth),
        world.GetHeight());

    field.ScanColumns(skyBottom);

    std::vector<LightField::Cell> queue;
    SeedBoundaries(field, width, depth, skyBottom, queue);

    {
        CB_PROFILE_SCOPE("PropagateAll/Flood");
        Flood(field, queue);
    }

    field.ScatterInto(world);
}

void SkyLight::Repropagate(World& world, int x, int y, int z)
{
    const glm::ivec3 edit(x, y, z);

    LightRecorder recorder(world);
    std::vector<glm::ivec3> queue;

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

    WorldCells cells(world, &recorder);
    Flood(cells, queue);
    recorder.MarkChangedChunks();
}
