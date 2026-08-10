#include "cub.h"

#include "Cubit/Voxel/TerrainGen.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace
{
    using namespace MapBlocks;

    // Fort placement, shared by the fort footprint test and the builder so they
    // never diverge.
    constexpr int FortEdgeOffset = 8; // fort centre distance from the x edge
    constexpr int FortRadius = 5;     // half-footprint

    std::size_t Index(const VoxModel& m, int x, int y, int z)
    {
        return static_cast<std::size_t>(x) +
            static_cast<std::size_t>(m.Size.x) *
            (static_cast<std::size_t>(y) +
             static_cast<std::size_t>(m.Size.y) * static_cast<std::size_t>(z));
    }

    void Set(VoxModel& m, int x, int y, int z, BlockId id)
    {
        if (x < 0 || x >= m.Size.x || y < 0 || y >= m.Size.y || z < 0 || z >= m.Size.z)
            return;
        m.Voxels[Index(m, x, y, z)] = id;
    }

    BlockId Get(const VoxModel& m, int x, int y, int z)
    {
        if (x < 0 || x >= m.Size.x || y < 0 || y >= m.Size.y || z < 0 || z >= m.Size.z)
            return 0;
        return m.Voxels[Index(m, x, y, z)];
    }

    //Folds x across the mirror plane so all generation is symmetric by construction.
    int Fold(const TerrainConfig& c, int x) { return std::min(x, c.Size.x - 1 - x); }

    std::uint32_t Hash(int x, int z, unsigned seed)
    {
        std::uint32_t h = seed + 374761393u;
        h += static_cast<std::uint32_t>(x) * 3266489917u;
        h ^= h >> 15; h *= 2246822519u;
        h += static_cast<std::uint32_t>(z) * 668265263u;
        h ^= h >> 13; h *= 3266489917u;
        h ^= h >> 16;
        return h;
    }

    float ValueAt(int xi, int zi, unsigned seed)
    {
        return (Hash(xi, zi, seed) & 0xFFFF) / 65535.0f; // [0,1]
    }

    float SmoothNoise(float x, float z, unsigned seed)
    {
        const int x0 = static_cast<int>(std::floor(x));
        const int z0 = static_cast<int>(std::floor(z));
        float tx = x - x0, tz = z - z0;
        tx = tx * tx * (3.0f - 2.0f * tx);
        tz = tz * tz * (3.0f - 2.0f * tz);
        const float v00 = ValueAt(x0, z0, seed), v10 = ValueAt(x0 + 1, z0, seed);
        const float v01 = ValueAt(x0, z0 + 1, seed), v11 = ValueAt(x0 + 1, z0 + 1, seed);
        const float a = v00 + (v10 - v00) * tx;
        const float b = v01 + (v11 - v01) * tx;
        return a + (b - a) * tz; // [0,1]
    }

    float Fbm(float x, float z, unsigned seed)
    {
        float sum = 0, amp = 0.5f, freq = 1.0f, norm = 0;
        for (int o = 0; o < 4; ++o)
        {
            sum += amp * SmoothNoise(x * freq, z * freq, seed + o * 101u);
            norm += amp; amp *= 0.5f; freq *= 2.0f;
        }
        return sum / norm; // [0,1]
    }

    //Final surface height of a column, symmetric in x. Base terrain only here;
    //Task 3 adds the mountain ridge.
    int SurfaceHeight(const TerrainConfig& c, int x, int z)
    {
        const int fx = Fold(c, x);
        const float hills = Fbm(fx * 0.06f, z * 0.06f, c.Seed);
        int h = c.GroundBase + static_cast<int>(std::lround(hills * 8.0f - 2.0f));

        // Ridge rising toward the z=0 and z=max flanks (0 at mid-z, 1 at edges).
        const float zc = (c.Size.z > 1) ? z / static_cast<float>(c.Size.z - 1) : 0.5f;
        const float edge = std::pow(std::abs(zc - 0.5f) * 2.0f, 2.0f);
        const float ridge = Fbm(fx * 0.03f, z * 0.03f, c.Seed + 7u);
        h += static_cast<int>(std::lround(c.MountainHeight * edge * ridge));

        return std::clamp(h, 1, c.Size.y - 1);
    }

    //Fills one column with stone/dirt/grass up to its surface, snow-capping peaks.
    void FillColumn(VoxModel& m, const TerrainConfig& c, int x, int z)
    {
        const int top = SurfaceHeight(c, x, z);
        const bool dark = (Hash(Fold(c, x), z, c.Seed + 5u) & 1u) != 0;
        const bool snowy = (top - 1) >= c.SnowLine;
        for (int y = 0; y < top; ++y)
        {
            BlockId id;
            if (y == top - 1)
                id = snowy ? Snow : (dark ? GrassDark : Grass);
            else if (y >= top - 4)
                id = snowy ? StoneDark : Dirt;
            else
                id = Stone;
            Set(m, x, y, z, id);
        }
    }

    void ApplyTerrain(VoxModel& m, const TerrainConfig& c)
    {
        for (int z = 0; z < c.Size.z; ++z)
            for (int x = 0; x < c.Size.x; ++x)
                FillColumn(m, c, x, z);
    }

    void CarveRiver(VoxModel& m, const TerrainConfig& c)
    {
        const int bed = c.WaterLevel - 2;               // river bottom
        for (int z = 0; z < c.Size.z; ++z)
        {
            // Half-width waves along z (symmetric: centreline stays on the mirror plane).
            const float w = SmoothNoise(z * 0.10f, 0.0f, c.Seed + 31u);
            const int halfWidth = 3 + static_cast<int>(std::lround(w * 3.0f)); // 3..6

            for (int x = 0; x < c.Size.x; ++x)
            {
                // Distance from the mirror plane, symmetric in x: 0 at the centre columns.
                const int dist = (c.Size.x - 1) / 2 - Fold(c, x);

                if (dist <= halfWidth)
                {
                    // Inside the channel: clear above the bed, fill water to water level.
                    for (int y = bed + 1; y < c.Size.y; ++y)
                        Set(m, x, y, z, 0);
                    for (int y = bed + 1; y <= c.WaterLevel; ++y)
                        Set(m, x, y, z, MapBlocks::Water);
                    // VoxModel is palette-less, so a cell is either empty or not:
                    // presence is the only question that can be asked here.
                    if (Get(m, x, bed, z) == 0)
                        Set(m, x, bed, z, MapBlocks::Stone); // guarantee a floor
                }
                else if (dist <= halfWidth + 2)
                {
                    // Bank: turn the existing surface block to sand.
                    const int top = SurfaceHeight(c, x, z) - 1;
                    if (IsPresent(Get(m, x, top, z)))
                        Set(m, x, top, z, MapBlocks::Sand);
                }
            }
        }
    }

    void PlaceTree(VoxModel& m, const TerrainConfig& c, int x, int baseY, int z)
    {
        const std::uint32_t r = Hash(Fold(c, x), z, c.Seed + 71u);
        const int trunkH = 4 + static_cast<int>(r % 4u); // 4..7
        for (int i = 0; i < trunkH; ++i)
            Set(m, x, baseY + i, z, MapBlocks::Wood);

        const int cy = baseY + trunkH;      // canopy centre
        const int rad = 2;
        for (int dz = -rad; dz <= rad; ++dz)
            for (int dy = -rad; dy <= rad; ++dy)
                for (int dx = -rad; dx <= rad; ++dx)
                {
                    if (dx * dx + dy * dy + dz * dz > rad * rad + 1)
                        continue;
                    // Colour by absolute (folded) position, not the tree's local
                    // offset, so overlapping canopies dither identically regardless
                    // of placement order and the result stays mirror-symmetric.
                    const bool light =
                        ((Fold(c, x + dx) + (cy + dy) + (z + dz)) & 1) != 0;
                    // Never overwrite trunk wood: a canopy that clipped a nearby
                    // tree's trunk would split it (a wood block left above a leaf).
                    if (Get(m, x + dx, cy + dy, z + dz) == MapBlocks::Wood)
                        continue;
                    Set(m, x + dx, cy + dy, z + dz,
                        light ? MapBlocks::LeavesLight : MapBlocks::Leaves);
                }
    }

    //Reports whether a column sits within (or just around) a fort footprint, so
    //forests leave the forts clear. Symmetric because the two fort centres mirror.
    bool InFortFootprint(const TerrainConfig& c, int x, int z)
    {
        const int cz = c.Size.z / 2;
        const int margin = FortRadius + 2;
        if (std::abs(z - cz) > margin)
            return false;
        const int left = FortEdgeOffset;
        const int right = c.Size.x - 1 - FortEdgeOffset;
        return std::abs(x - left) <= margin || std::abs(x - right) <= margin;
    }

    void PlantForests(VoxModel& m, const TerrainConfig& c)
    {
        for (int z = 2; z < c.Size.z - 2; ++z)
            for (int x = 2; x < c.Size.x - 2; ++x)
            {
                if (InFortFootprint(c, x, z))
                    continue;                              // keep the forts clear
                const int top = SurfaceHeight(c, x, z) - 1; // surface block y
                const std::uint8_t surface = Get(m, x, top, z);
                if (surface != MapBlocks::Grass && surface != MapBlocks::GrassDark)
                    continue;                              // only on grass
                if (top <= c.WaterLevel)
                    continue;                              // not in wet lowland
                const float roll = (Hash(Fold(c, x), z, c.Seed + 91u) & 0xFFFF) / 65535.0f;
                if (roll < c.ForestDensity)
                    PlaceTree(m, c, x, top + 1, z);
            }
    }

    void BuildForts(VoxModel& m, const TerrainConfig& c)
    {
        const int half = c.Size.x / 2;
        const int cz = c.Size.z / 2;         // fort centre in z
        const int radius = FortRadius;       // half-footprint
        const int wallH = 5;

        // Two mirrored footprints: left (x≈FortEdgeOffset) and right (mirror).
        const int centres[2] = { FortEdgeOffset, c.Size.x - 1 - FortEdgeOffset };
        for (int cxi : centres)
        {
            for (int z = cz - radius; z <= cz + radius; ++z)
                for (int x = cxi - radius; x <= cxi + radius; ++x)
                {
                    if (x < 0 || x >= c.Size.x || z < 0 || z >= c.Size.z)
                        continue;
                    const int ground = SurfaceHeight(c, x, z); // floor sits here
                    const BlockId team = (x < half) ? MapBlocks::RedBase
                                                    : MapBlocks::BlueBase;

                    // Floor slab.
                    Set(m, x, ground, z, team);

                    // Perimeter walls, with an opening on the field-facing side.
                    const bool edge =
                        x == cxi - radius || x == cxi + radius ||
                        z == cz - radius || z == cz + radius;
                    const bool opening =
                        (z == cz) && ((x < half) ? (x == cxi + radius)
                                                  : (x == cxi - radius));
                    if (edge && !opening)
                        for (int i = 1; i <= wallH; ++i)
                            Set(m, x, ground + i, z, team);
                }
        }
    }
}

Palette TerrainGen::MapPalette()
{
    Palette p = DefaultPalette();
    p[Grass]       = glm::vec4(glm::vec3(70, 145, 55) / 255.0f, 1.0f);
    p[GrassDark]   = glm::vec4(glm::vec3(55, 115, 45) / 255.0f, 1.0f);
    p[Dirt]        = glm::vec4(glm::vec3(120, 85, 50) / 255.0f, 1.0f);
    p[Stone]       = glm::vec4(glm::vec3(130, 130, 135) / 255.0f, 1.0f);
    p[StoneDark]   = glm::vec4(glm::vec3(80, 80, 85) / 255.0f, 1.0f);
    p[Sand]        = glm::vec4(glm::vec3(210, 195, 140) / 255.0f, 1.0f);
    //Transparent, so the riverbed shows through. Everything else stays opaque.
    p[Water]       = glm::vec4(glm::vec3(55, 110, 200) / 255.0f, 0.55f);
    p[Wood]        = glm::vec4(glm::vec3(95, 65, 40) / 255.0f, 1.0f);
    p[Leaves]      = glm::vec4(glm::vec3(45, 110, 45) / 255.0f, 1.0f);
    p[LeavesLight] = glm::vec4(glm::vec3(70, 140, 60) / 255.0f, 1.0f);
    p[Snow]        = glm::vec4(glm::vec3(235, 240, 245) / 255.0f, 1.0f);
    p[RedBase]     = glm::vec4(glm::vec3(190, 45, 45) / 255.0f, 1.0f);
    p[BlueBase]    = glm::vec4(glm::vec3(55, 80, 200) / 255.0f, 1.0f);
    return p;
}

VoxModel TerrainGen::Generate(const TerrainConfig& config)
{
    VoxModel m;
    m.Size = config.Size;
    m.Colors = MapPalette();
    m.Voxels.assign(
        static_cast<std::size_t>(config.Size.x) *
        static_cast<std::size_t>(config.Size.y) *
        static_cast<std::size_t>(config.Size.z), 0);

    ApplyTerrain(m, config);
    CarveRiver(m, config);
    PlantForests(m, config);
    BuildForts(m, config);
    return m;
}
