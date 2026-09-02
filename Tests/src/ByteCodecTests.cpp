#include <doctest.h>

#include "Cubit/Net/ByteReader.h"
#include "Cubit/Net/ByteWriter.h"

#include <glm/glm.hpp>
#include <string>

TEST_CASE("Every field type survives a round trip")
{
    ByteWriter writer;
    writer.U8(0xAB);
    writer.U16(0x1234);
    writer.U32(0xDEADBEEF);
    writer.U64(0x0123456789ABCDEFull);
    writer.F32(-4.25f);
    writer.Vec3(glm::vec3(1.5f, -2.5f, 3.5f));
    writer.IVec3(glm::ivec3(-7, 8, -9));
    writer.String("battlefield512.vox");
    writer.Bool(true);

    ByteReader reader(writer.Bytes());
    CHECK(reader.U8() == 0xAB);
    CHECK(reader.U16() == 0x1234);
    CHECK(reader.U32() == 0xDEADBEEF);
    CHECK(reader.U64() == 0x0123456789ABCDEFull);
    CHECK(reader.F32() == doctest::Approx(-4.25f));
    CHECK(reader.Vec3() == glm::vec3(1.5f, -2.5f, 3.5f));
    CHECK(reader.IVec3() == glm::ivec3(-7, 8, -9));
    CHECK(reader.String() == "battlefield512.vox");
    CHECK(reader.Bool());
    CHECK(reader.Ok());
    CHECK(reader.Remaining() == 0);
}

TEST_CASE("Bytes are little-endian regardless of the host")
{
    //Pinned rather than assumed. A codec that happens to match the host today
    //is a codec that breaks the day anything reads it elsewhere, and the bug
    //looks like corrupt positions rather than a byte-order fault.
    ByteWriter writer;
    writer.U32(0x01020304);

    const std::vector<std::uint8_t>& bytes = writer.Bytes();
    REQUIRE(bytes.size() == 4);
    CHECK(bytes[0] == 0x04);
    CHECK(bytes[1] == 0x03);
    CHECK(bytes[2] == 0x02);
    CHECK(bytes[3] == 0x01);
}

TEST_CASE("Reading past the end fails safe instead of reading rubbish")
{
    const std::uint8_t bytes[] = { 0x01, 0x02 };
    ByteReader reader(bytes);

    CHECK(reader.U8() == 0x01);
    CHECK(reader.Ok());

    //Two bytes remain-1; a U32 cannot be satisfied.
    CHECK(reader.U32() == 0);
    CHECK_FALSE(reader.Ok());

    //Once broken it stays broken, so a caller that checks Ok() once at the end
    //cannot be fooled by a later read that happens to fit.
    CHECK(reader.U8() == 0);
    CHECK_FALSE(reader.Ok());
}

TEST_CASE("A string length longer than the buffer is refused")
{
    //The classic hostile packet: a small buffer claiming a huge payload. It
    //must not allocate and must not read.
    ByteWriter writer;
    writer.U16(60000);
    writer.U8('x');

    ByteReader reader(writer.Bytes());
    CHECK(reader.String().empty());
    CHECK_FALSE(reader.Ok());
}
