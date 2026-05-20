#include <cstdint>

#include <gtest/gtest.h>

#include "index/key_encoder.h"

namespace {

TEST(KeyEncoderTest, NullRoundTripUsesEmptyPayload) {
    db::ByteBuffer encoded;
    ASSERT_TRUE(db::KeyEncoder::Encode(db::Value::Null(), &encoded).ok());
    EXPECT_TRUE(encoded.empty());

    db::Value decoded;
    ASSERT_TRUE(db::KeyEncoder::Decode(encoded, db::ValueType::kNull, &decoded).ok());
    EXPECT_EQ(decoded, db::Value::Null());
}

TEST(KeyEncoderTest, IntRoundTripPreservesValue) {
    const db::Value original = db::Value::Int(-123456789);

    db::ByteBuffer encoded;
    ASSERT_TRUE(db::KeyEncoder::Encode(original, &encoded).ok());
    ASSERT_EQ(encoded.size(), sizeof(std::int64_t));

    db::Value decoded;
    ASSERT_TRUE(db::KeyEncoder::Decode(encoded, db::ValueType::kInt, &decoded).ok());
    EXPECT_EQ(decoded, original);
}

TEST(KeyEncoderTest, StringRoundTripPreservesValue) {
    const db::Value original = db::Value::String("hello-index");

    db::ByteBuffer encoded;
    ASSERT_TRUE(db::KeyEncoder::Encode(original, &encoded).ok());
    ASSERT_EQ(encoded.size(), 11U);

    db::Value decoded;
    ASSERT_TRUE(db::KeyEncoder::Decode(encoded, db::ValueType::kString, &decoded).ok());
    EXPECT_EQ(decoded, original);
}

TEST(KeyEncoderTest, EmptyStringRoundTripPreservesValue) {
    const db::Value original = db::Value::String("");

    db::ByteBuffer encoded;
    ASSERT_TRUE(db::KeyEncoder::Encode(original, &encoded).ok());
    EXPECT_TRUE(encoded.empty());

    db::Value decoded;
    ASSERT_TRUE(db::KeyEncoder::Decode(encoded, db::ValueType::kString, &decoded).ok());
    EXPECT_EQ(decoded, original);
}

TEST(KeyEncoderTest, BoolRoundTripPreservesValue) {
    const db::Value original = db::Value::Bool(true);

    db::ByteBuffer encoded;
    ASSERT_TRUE(db::KeyEncoder::Encode(original, &encoded).ok());
    ASSERT_EQ(encoded.size(), 1U);
    EXPECT_EQ(encoded[0], 1U);

    db::Value decoded;
    ASSERT_TRUE(db::KeyEncoder::Decode(encoded, db::ValueType::kBool, &decoded).ok());
    EXPECT_EQ(decoded, original);
}

TEST(KeyEncoderTest, DecodeIntRejectsWrongSize) {
    const db::ByteBuffer encoded = {1U, 2U, 3U};
    db::Value decoded;
    const db::Status status = db::KeyEncoder::Decode(encoded, db::ValueType::kInt, &decoded);
    EXPECT_FALSE(status.ok());
}

TEST(KeyEncoderTest, DecodeBoolRejectsWrongSize) {
    const db::ByteBuffer encoded = {0U, 1U};
    db::Value decoded;
    const db::Status status = db::KeyEncoder::Decode(encoded, db::ValueType::kBool, &decoded);
    EXPECT_FALSE(status.ok());
}

TEST(KeyEncoderTest, DecodeBoolRejectsInvalidByte) {
    const db::ByteBuffer encoded = {2U};
    db::Value decoded;
    const db::Status status = db::KeyEncoder::Decode(encoded, db::ValueType::kBool, &decoded);
    EXPECT_FALSE(status.ok());
}

TEST(KeyEncoderTest, DecodeNullRejectsNonEmptyPayload) {
    const db::ByteBuffer encoded = {0U};
    db::Value decoded;
    const db::Status status = db::KeyEncoder::Decode(encoded, db::ValueType::kNull, &decoded);
    EXPECT_FALSE(status.ok());
}

}  // namespace
