#include "Cubit/Voxel/TerrainGen.h"
#include "Cubit/Voxel/VoxWriter.h"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

//Offline tool: generate the battlefield map and write it as .vox. Pass an output
//path as argv[1], e.g. "Sandbox/assets/maps/battlefield.vox".
int main(int argc, char** argv)
{
    std::string out = "battlefield.vox";
    TerrainConfig config;
    config.Size = glm::ivec3(256, 64, 256);

    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];

        if (arg == "--size")
        {
            // Cubit axes: width, Y-up height, depth. Anything larger than 256
            // on an axis is written as several stitched models.
            if (i + 3 >= argc)
            {
                std::cerr << "--size needs three values: W H D\n";
                return 1;
            }
            config.Size = glm::ivec3(
                std::atoi(argv[i + 1]), std::atoi(argv[i + 2]), std::atoi(argv[i + 3]));
            i += 3;

            if (config.Size.x < 1 || config.Size.y < 1 || config.Size.z < 1)
            {
                std::cerr << "--size values must be positive\n";
                return 1;
            }
        }
        else
        {
            out = arg;
        }
    }

    const VoxModel model = TerrainGen::Generate(config);
    const std::vector<std::uint8_t> bytes = VoxWriter::Write(model);

    std::ofstream file(out, std::ios::binary);
    if (!file)
    {
        std::cerr << "Cannot open output: " << out << "\n";
        return 1;
    }
    file.write(reinterpret_cast<const char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size()));

    std::cout << "Wrote " << out << " ("
        << config.Size.x << "x" << config.Size.y << "x" << config.Size.z
        << ", " << bytes.size() << " bytes)\n";
    return 0;
}
