#include "cub.h"

#include "Cubit/Net/MapHash.h"

#include <fstream>

namespace
{
    constexpr std::uint64_t FnvBasis = 0xCBF29CE484222325ull;
    constexpr std::uint64_t FnvPrime = 0x00000100000001B3ull;
}

std::uint64_t HashBytes(std::span<const std::uint8_t> bytes)
{
    std::uint64_t hash = FnvBasis;

    for (const std::uint8_t byte : bytes)
    {
        hash ^= byte;
        hash *= FnvPrime;
    }

    return hash;
}

std::uint64_t HashMapFile(const std::string& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
        return 0;

    std::uint64_t hash = FnvBasis;

    //Streamed in blocks rather than read whole: the shipped map is 23.8 MB and
    //there is no reason to hold a second copy of it to hash it.
    std::array<char, 64 * 1024> buffer{};
    while (file.read(buffer.data(), static_cast<std::streamsize>(buffer.size())) || file.gcount() > 0)
    {
        const std::streamsize read = file.gcount();
        for (std::streamsize i = 0; i < read; ++i)
        {
            hash ^= static_cast<std::uint8_t>(buffer[static_cast<std::size_t>(i)]);
            hash *= FnvPrime;
        }
    }

    return hash;
}
