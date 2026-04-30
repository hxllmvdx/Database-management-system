#include <cstdint>

#include <gtest/gtest.h>

#include "common/error.h"
#include "index/key_encoder.h"

namespace {

TEST(KeyEncoderTest, NullRoundTripUsesEmptyPayload) {
    const db::ByteBuffer encoded = db::KeyEncoder::Encode(db::Value::Null());
    EXPECT_TRUE(encoded.empty());

    const db::Value decoded = db::KeyEncoder::Decode(encoded, db::ValueType::kNull);
    EXPECT_EQ(decoded, db::Value::Null());
}

TEST(KeyEncoderTest, IntRoundTripPreservesValue) {
    const db::Value original = db::Value::Int(-123456789);

    const db::ByteBuffer encoded = db::KeyEncoder::Encode(original);
    ASSERT_EQ(encoded.size(), sizeof(std::int64_t));

    const db::Value decoded = db::KeyEncoder::Decode(encoded, db::ValueType::kInt);
    EXPECT_EQ(decoded, original);
}

TEST(KeyEncoderTest, StringRoundTripPreservesValue) {
    const db::Value original = db::Value::String("hello-index");

    const db::ByteBuffer encoded = db::KeyEncoder::Encode(original);
    ASSERT_EQ(encoded.size(), 11U);

    const db::Value decoded = db::KeyEncoder::Decode(encoded, db::ValueType::kString);
    EXPECT_EQ(decoded, original);
}

TEST(KeyEncoderTest, EmptyStringRoundTripPreservesValue) {
    const db::Value original = db::Value::String("");

    const db::ByteBuffer encoded = db::KeyEncoder::Encode(original);
    EXPECT_TRUE(encoded.empty());

    const db::Value decoded = db::KeyEncoder::Decode(encoded, db::ValueType::kString);
    EXPECT_EQ(decoded, original);
}

TEST(KeyEncoderTest, BoolRoundTripPreservesValue) {
    const db::Value original = db::Value::Bool(true);

    const db::ByteBuffer encoded = db::KeyEncoder::Encode(original);
    ASSERT_EQ(encoded.size(), 1U);
    EXPECT_EQ(encoded[0], 1U);

    const db::Value decoded = db::KeyEncoder::Decode(encoded, db::ValueType::kBool);
    EXPECT_EQ(decoded, original);
}

TEST(KeyEncoderTest, DecodeIntRejectsWrongSize) {
    const db::ByteBuffer encoded = {1U, 2U, 3U};
    EXPECT_THROW(
        static_cast<void>(db::KeyEncoder::Decode(encoded, db::ValueType::kInt)),
        db::DbError);
}

TEST(KeyEncoderTest, DecodeBoolRejectsWrongSize) {
    const db::ByteBuffer encoded = {0U, 1U};
    EXPECT_THROW(
        static_cast<void>(db::KeyEncoder::Decode(encoded, db::ValueType::kBool)),
        db::DbError);
}

TEST(KeyEncoderTest, DecodeBoolRejectsInvalidByte) {
    const db::ByteBuffer encoded = {2U};
    EXPECT_THROW(
        static_cast<void>(db::KeyEncoder::Decode(encoded, db::ValueType::kBool)),
        db::DbError);
}

TEST(KeyEncoderTest, DecodeNullRejectsNonEmptyPayload) {
    const db::ByteBuffer encoded = {0U};
    EXPECT_THROW(
        static_cast<void>(db::KeyEncoder::Decode(encoded, db::ValueType::kNull)),
        db::DbError);
}

}  // namespace
