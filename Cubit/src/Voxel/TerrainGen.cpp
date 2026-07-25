#include "cub.h"

#include "Cubit/Voxel/TerrainGen.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace
{
    using namespace MapBlocks;

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
        return std::clamp(h, 1, c.Size.y - 1);
    }

    //Fills one column with stone/dirt/grass up to its surface. Task 3 adds snow.
    void FillColumn(VoxModel& m, const TerrainConfig& c, int x, int z)
    {
        const int top = SurfaceHeight(c, x, z);
        const bool dark = (Hash(Fold(c, x), z, c.Seed + 5u) & 1u) != 0;
        for (int y = 0; y < top; ++y)
        {
            BlockId id;
            if (y == top - 1)      id = dark ? GrassDark : Grass;
            else if (y >= top - 4) id = Dirt;
            else                   id = Stone;
            Set(m, x, y, z, id);
        }
    }

    void ApplyTerrain(VoxModel& m, const TerrainConfig& c)
    {
        for (int z = 0; z < c.Size.z; ++z)
            for (int x = 0; x < c.Size.x; ++x)
                FillColumn(m, c, x, z);
    }

    // Filled in by later tasks. No-ops for now.
    void CarveRiver(VoxModel&, const TerrainConfig&) {}
    void PlantForests(VoxModel&, const TerrainConfig&) {}
    void BuildForts(VoxModel&, const TerrainConfig&) {}
}

Palette TerrainGen::MapPalette()
{
    Palette p = DefaultPalette();
    p[Grass]       = glm::vec3(70, 145, 55) / 255.0f;
    p[GrassDark]   = glm::vec3(55, 115, 45) / 255.0f;
    p[Dirt]        = glm::vec3(120, 85, 50) / 255.0f;
    p[Stone]       = glm::vec3(130, 130, 135) / 255.0f;
    p[StoneDark]   = glm::vec3(80, 80, 85) / 255.0f;
    p[Sand]        = glm::vec3(210, 195, 140) / 255.0f;
    p[Water]       = glm::vec3(55, 110, 200) / 255.0f;
    p[Wood]        = glm::vec3(95, 65, 40) / 255.0f;
    p[Leaves]      = glm::vec3(45, 110, 45) / 255.0f;
    p[LeavesLight] = glm::vec3(70, 140, 60) / 255.0f;
    p[Snow]        = glm::vec3(235, 240, 245) / 255.0f;
    p[RedBase]     = glm::vec3(190, 45, 45) / 255.0f;
    p[BlueBase]    = glm::vec3(55, 80, 200) / 255.0f;
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
