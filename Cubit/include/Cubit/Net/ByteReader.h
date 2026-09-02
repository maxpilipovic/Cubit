#pragma once

#include <glm/glm.hpp>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

//Reads a little-endian byte buffer produced by ByteWriter.
//
//Fails safe rather than throwing or reading past the end. This is the first
//data in the project that arrives from a socket: a truncated or hostile packet
//is a routine condition, not a bug in the caller, so every reader returns a
//zero value and latches Ok() to false.
//
//Ok() is sticky. Once a read has failed it stays failed, so a caller that
//checks once after decoding a whole message cannot be fooled by a later small
//read that happens to fit inside the remaining bytes.
class ByteReader
{
public:
    explicit ByteReader(std::span<const std::uint8_t> bytes)
        : m_Bytes(bytes) {}

    std::uint8_t U8()
    {
        if (!Take(1)) return 0;
        return m_Bytes[m_Offset - 1];
    }

    std::uint16_t U16()
    {
        if (!Take(2)) return 0;
        return static_cast<std::uint16_t>(
            static_cast<std::uint16_t>(m_Bytes[m_Offset - 2]) |
            static_cast<std::uint16_t>(m_Bytes[m_Offset - 1]) << 8);
    }

    std::uint32_t U32()
    {
        if (!Take(4)) return 0;
        std::uint32_t value = 0;
        for (int i = 0; i < 4; ++i)
            value |= static_cast<std::uint32_t>(m_Bytes[m_Offset - 4 + i]) << (i * 8);
        return value;
    }

    std::uint64_t U64()
    {
        if (!Take(8)) return 0;
        std::uint64_t value = 0;
        for (int i = 0; i < 8; ++i)
            value |= static_cast<std::uint64_t>(m_Bytes[m_Offset - 8 + i]) << (i * 8);
        return value;
    }

    std::int32_t I32() { return static_cast<std::int32_t>(U32()); }

    float F32() { return std::bit_cast<float>(U32()); }

    bool Bool() { return U8() != 0; }

    glm::vec3 Vec3()
    {
        const float x = F32();
        const float y = F32();
        const float z = F32();
        return glm::vec3(x, y, z);
    }

    glm::ivec3 IVec3()
    {
        const std::int32_t x = I32();
        const std::int32_t y = I32();
        const std::int32_t z = I32();
        return glm::ivec3(x, y, z);
    }

    //Refuses a length the buffer cannot satisfy without allocating for it.
    //A small packet claiming a huge string is the most obvious hostile input
    //there is, and reserving on its word is how that becomes a denial of
    //service rather than a rejected packet.
    std::string String()
    {
        const std::uint16_t length = U16();
        if (!m_Ok || length > Remaining())
        {
            m_Ok = false;
            return {};
        }

        const char* begin = reinterpret_cast<const char*>(m_Bytes.data() + m_Offset);
        m_Offset += length;
        return std::string(begin, begin + length);
    }

    bool Ok() const { return m_Ok; }

    std::size_t Remaining() const
    {
        return m_Ok ? m_Bytes.size() - m_Offset : 0;
    }

private:
    //Advances by `count` when the buffer allows it, latching failure when not.
    bool Take(std::size_t count)
    {
        if (!m_Ok || m_Bytes.size() - m_Offset < count)
        {
            m_Ok = false;
            return false;
        }

        m_Offset += count;
        return true;
    }

    std::span<const std::uint8_t> m_Bytes;
    std::size_t m_Offset = 0;
    bool m_Ok = true;
};
