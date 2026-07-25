#include "Cubit/Voxel/TerrainGen.h"
#include "Cubit/Voxel/VoxWriter.h"

#include <fstream>
#include <iostream>
#include <string>
#include <vector>

//Offline tool: generate the battlefield map and write it as .vox. Pass an output
//path as argv[1], e.g. "Sandbox/assets/maps/battlefield.vox".
int main(int argc, char** argv)
{
    const std::string out = (argc > 1) ? argv[1] : "battlefield.vox";

    TerrainConfig config;
    config.Size = glm::ivec3(256, 64, 256);
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

    std::cout << "Wrote " << out << " (" << bytes.size() << " bytes)\n";
    return 0;
}
