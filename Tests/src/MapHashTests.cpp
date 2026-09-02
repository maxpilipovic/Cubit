#include <doctest.h>

#include "Cubit/Net/MapHash.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

TEST_CASE("Hashing is stable and order-sensitive")
{
    const std::vector<std::uint8_t> a{ 1, 2, 3 };
    const std::vector<std::uint8_t> b{ 3, 2, 1 };

    CHECK(HashBytes(a) == HashBytes(a));
    CHECK(HashBytes(a) != HashBytes(b));
}

TEST_CASE("A one-byte difference changes the hash")
{
    //The case this exists for: two maps that differ in a single block. A hash
    //that missed that would let the desync it is meant to catch straight
    //through.
    std::vector<std::uint8_t> original(4096, 7);
    std::vector<std::uint8_t> altered = original;
    altered[2048] = 8;

    CHECK(HashBytes(original) != HashBytes(altered));
}

TEST_CASE("The empty buffer hashes to the FNV-1a basis")
{
    CHECK(HashBytes(std::vector<std::uint8_t>{}) == 0xCBF29CE484222325ull);
}

TEST_CASE("A single byte matches the published FNV-1a-64 test vector for 'a'")
{
    //0xaf63dc4c8601ec8c is the standard published FNV-1a-64 test vector for
    //the one-byte input "a" (see Fowler/Noll/Vo's fnv test suite). Pinning it
    //here catches a wrong-but-internally-consistent implementation - e.g. XOR
    //and multiply swapped, or the FNV-1 (non-"a") variant - that every
    //round-trip and order-sensitivity check above would still pass.
    const std::vector<std::uint8_t> a{ 'a' };
    CHECK(HashBytes(a) == 0xAF63DC4C8601EC8Cull);
}

TEST_CASE("An unreadable file hashes to zero rather than throwing")
{
    //A missing map is a startup condition to report, not an exception to
    //unwind a server through.
    CHECK(HashMapFile("no/such/map.vox") == 0);
}

TEST_CASE("HashMapFile matches HashBytes for the same content, read in binary")
{
    //Writes and deletes its own temp file rather than depending on a repo
    //asset: Tests.exe runs from bin/Debug-windows-x86_64/Tests, which has no
    //assets/ directory copied into it, and a real .vox is slow to hash for
    //what this checks. The bytes include a CR-LF pair and a lone LF - if the
    //file were opened in text mode, Windows would translate '\r\n' on read
    //and this content-vs-bytes comparison would go red without HashMapFile
    //itself doing anything wrong.
    const std::vector<std::uint8_t> content{ 'C', 'u', 'b', 'i', 't', '\r', '\n', 0, 255, 128, '\n' };

    const std::filesystem::path path = std::filesystem::temp_directory_path() / "cubit_maphash_test.bin";
    {
        std::ofstream out(path, std::ios::binary);
        out.write(reinterpret_cast<const char*>(content.data()), static_cast<std::streamsize>(content.size()));
    }

    CHECK(HashMapFile(path.string()) == HashBytes(content));

    std::filesystem::remove(path);
}
