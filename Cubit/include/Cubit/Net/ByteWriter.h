#pragma once

#include <glm/glm.hpp>

#include <bit>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

//Builds a little-endian byte buffer for the wire.
//
//Header-only and unexported on purpose: it is small enough to inline, it is on
//the per-tick path, and keeping it off the DLL boundary avoids the rule that an
//exported class must define every member it declares.
//
//Fixed-width, little-endian, no varints and no bit packing. There is no
//bandwidth problem to solve here, and a hand-rolled codec's bugs should be as
//boring as possible to find.
class ByteWriter
{
public:
    void U8(std::uint8_t value) { m_Bytes.push_back(value); }

    void U16(std::uint16_t value)
    {
        m_Bytes.push_back(static_cast<std::uint8_t>(value));
        m_Bytes.push_back(static_cast<std::uint8_t>(value >> 8));
    }

    void U32(std::uint32_t value)
    {
        for (int shift = 0; shift < 32; shift += 8)
            m_Bytes.push_back(static_cast<std::uint8_t>(value >> shift));
    }

    void U64(std::uint64_t value)
    {
        for (int shift = 0; shift < 64; shift += 8)
            m_Bytes.push_back(static_cast<std::uint8_t>(value >> shift));
    }

    void I32(std::int32_t value) { U32(static_cast<std::uint32_t>(value)); }

    //Written as its IEEE-754 bit pattern rather than memcpy'd, so the byte
    //order is the one this class promises rather than the one the host uses.
    void F32(float value) { U32(std::bit_cast<std::uint32_t>(value)); }

    void Bool(bool value) { U8(value ? 1u : 0u); }

    void Vec3(const glm::vec3& value)
    {
        F32(value.x);
        F32(value.y);
        F32(value.z);
    }

    void IVec3(const glm::ivec3& value)
    {
        I32(value.x);
        I32(value.y);
        I32(value.z);
    }

    //A u16 length followed by the bytes. Longer than 65535 is a programming
    //error rather than a wire condition, so it truncates rather than throwing
    //into the middle of a send.
    void String(std::string_view value)
    {
        const std::size_t length =
            value.size() > 0xFFFFu ? std::size_t{ 0xFFFFu } : value.size();

        U16(static_cast<std::uint16_t>(length));
        m_Bytes.insert(m_Bytes.end(), value.begin(), value.begin() + length);
    }

    const std::vector<std::uint8_t>& Bytes() const { return m_Bytes; }

    std::span<const std::uint8_t> Span() const { return m_Bytes; }

private:
    std::vector<std::uint8_t> m_Bytes;
};
