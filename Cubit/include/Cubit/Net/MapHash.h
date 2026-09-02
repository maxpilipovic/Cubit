#pragma once

#include "Cubit/Core.h"

#include <cstdint>
#include <span>
#include <string>

//FNV-1a, 64-bit. Not cryptographic and does not need to be: this catches two
//machines running different files, not somebody forging one.
CB_API std::uint64_t HashBytes(std::span<const std::uint8_t> bytes);

//Hashes a map file's raw bytes. Returns 0 when the file cannot be read, which
//callers treat as "no map" rather than as a hash.
//
//The file, not the loaded World: it is the identity the server names in
//Welcome, and hashing the bytes means a client and server that read the same
//file agree without depending on the loader producing identical structures.
CB_API std::uint64_t HashMapFile(const std::string& path);
