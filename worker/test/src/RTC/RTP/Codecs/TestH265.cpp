#include "common.hpp"
#include "RTC/RTP/Codecs/H265.hpp"
#include <catch2/catch_test_macros.hpp>

using namespace RTC::RTP::Codecs;

namespace
{
	std::unique_ptr<H265::PayloadDescriptor> parse(const uint8_t* buffer, size_t len)
	{
		DependencyDescriptor* dependencyDescriptor{ nullptr };

		return std::unique_ptr<H265::PayloadDescriptor>(H265::Parse(buffer, len, dependencyDescriptor));
	}
} // namespace

SCENARIO("H265 payload descriptor", "[rtp][codecs][h265]")
{
	// NAL unit header (RFC 7798 section 1.1.4):
	//   byte 0: F(1) | Type(6) | LayerId high bit(1)
	//   byte 1: LayerId low bits(5) | TID(3)

	SECTION("single NAL unit packet with IDR_W_RADL is key frame")
	{
		// clang-format off
		uint8_t buffer[] =
		{
			0x26, 0x01, 0xAF, 0x00
		};
		// clang-format on

		auto payloadDescriptor = parse(buffer, sizeof(buffer));

		REQUIRE(payloadDescriptor);
		REQUIRE(payloadDescriptor->isKeyFrame == true);
		REQUIRE(payloadDescriptor->temporalLayer == 0);
	}

	SECTION("single NAL unit packet with CRA_NUT is key frame")
	{
		// clang-format off
		uint8_t buffer[] =
		{
			0x2A, 0x01, 0xAF, 0x00
		};
		// clang-format on

		auto payloadDescriptor = parse(buffer, sizeof(buffer));

		REQUIRE(payloadDescriptor);
		REQUIRE(payloadDescriptor->isKeyFrame == true);
	}

	SECTION("single NAL unit packet with VPS is key frame")
	{
		// clang-format off
		uint8_t buffer[] =
		{
			0x40, 0x01, 0x0C, 0x01
		};
		// clang-format on

		auto payloadDescriptor = parse(buffer, sizeof(buffer));

		REQUIRE(payloadDescriptor);
		REQUIRE(payloadDescriptor->isKeyFrame == true);
	}

	SECTION("single NAL unit packet with TRAIL_R is not key frame and reads TID")
	{
		// clang-format off
		uint8_t buffer[] =
		{
			// TRAIL_R with TID = 3 (temporal layer 2).
			0x02, 0x03, 0xAF, 0x00
		};
		// clang-format on

		auto payloadDescriptor = parse(buffer, sizeof(buffer));

		REQUIRE(payloadDescriptor);
		REQUIRE(payloadDescriptor->isKeyFrame == false);
		REQUIRE(payloadDescriptor->temporalLayer == 2);
	}

	SECTION("aggregation packet containing SPS is key frame")
	{
		// clang-format off
		uint8_t buffer[] =
		{
			// AP payload header.
			0x60, 0x01,
			// NALU 1 size + TRAIL_R.
			0x00, 0x03, 0x02, 0x01, 0xAA,
			// NALU 2 size + SPS.
			0x00, 0x03, 0x42, 0x01, 0xBB
		};
		// clang-format on

		auto payloadDescriptor = parse(buffer, sizeof(buffer));

		REQUIRE(payloadDescriptor);
		REQUIRE(payloadDescriptor->isKeyFrame == true);
	}

	SECTION("aggregation packet without key frame NAL units is not key frame")
	{
		// clang-format off
		uint8_t buffer[] =
		{
			// AP payload header.
			0x60, 0x01,
			// NALU 1 size + TRAIL_R.
			0x00, 0x03, 0x02, 0x01, 0xAA,
			// NALU 2 size + TRAIL_R.
			0x00, 0x03, 0x02, 0x01, 0xBB
		};
		// clang-format on

		auto payloadDescriptor = parse(buffer, sizeof(buffer));

		REQUIRE(payloadDescriptor);
		REQUIRE(payloadDescriptor->isKeyFrame == false);
	}

	SECTION("aggregation packet with wrong NALU size does not read out of bounds")
	{
		// clang-format off
		uint8_t buffer[] =
		{
			// AP payload header.
			0x60, 0x01,
			// NALU 1 size (too big) + TRAIL_R.
			0x00, 0xFF, 0x02, 0x01, 0xAA,
			// SPS that must not be reached.
			0x00, 0x03, 0x42, 0x01, 0xBB
		};
		// clang-format on

		auto payloadDescriptor = parse(buffer, sizeof(buffer));

		REQUIRE(payloadDescriptor);
		REQUIRE(payloadDescriptor->isKeyFrame == false);
	}

	SECTION("fragmentation unit start of IDR is key frame")
	{
		// clang-format off
		uint8_t buffer[] =
		{
			// FU payload header.
			0x62, 0x01,
			// FU header: S = 1, FuType = IDR_W_RADL.
			0x93,
			0xAF, 0x00
		};
		// clang-format on

		auto payloadDescriptor = parse(buffer, sizeof(buffer));

		REQUIRE(payloadDescriptor);
		REQUIRE(payloadDescriptor->isKeyFrame == true);
	}

	SECTION("fragmentation unit continuation of IDR is not key frame")
	{
		// clang-format off
		uint8_t buffer[] =
		{
			// FU payload header.
			0x62, 0x01,
			// FU header: S = 0, FuType = IDR_W_RADL.
			0x13,
			0xAF, 0x00
		};
		// clang-format on

		auto payloadDescriptor = parse(buffer, sizeof(buffer));

		REQUIRE(payloadDescriptor);
		REQUIRE(payloadDescriptor->isKeyFrame == false);
	}

	SECTION("fragmentation unit start of TRAIL_R is not key frame")
	{
		// clang-format off
		uint8_t buffer[] =
		{
			// FU payload header with TID = 2 (temporal layer 1).
			0x62, 0x02,
			// FU header: S = 1, FuType = TRAIL_R.
			0x81,
			0xAF, 0x00
		};
		// clang-format on

		auto payloadDescriptor = parse(buffer, sizeof(buffer));

		REQUIRE(payloadDescriptor);
		REQUIRE(payloadDescriptor->isKeyFrame == false);
		REQUIRE(payloadDescriptor->temporalLayer == 1);
	}

	SECTION("fragmentation unit without FU header is not key frame")
	{
		// clang-format off
		uint8_t buffer[] =
		{
			0x62, 0x01
		};
		// clang-format on

		auto payloadDescriptor = parse(buffer, sizeof(buffer));

		REQUIRE(payloadDescriptor);
		REQUIRE(payloadDescriptor->isKeyFrame == false);
	}

	SECTION("too short payload is not key frame")
	{
		// clang-format off
		uint8_t buffer[] =
		{
			0x26
		};
		// clang-format on

		auto payloadDescriptor = parse(buffer, sizeof(buffer));

		REQUIRE(payloadDescriptor);
		REQUIRE(payloadDescriptor->isKeyFrame == false);
		REQUIRE(payloadDescriptor->temporalLayer == 0);
	}

	SECTION("forbidden TID = 0 is read as temporal layer 0")
	{
		// clang-format off
		uint8_t buffer[] =
		{
			0x02, 0x00, 0xAF, 0x00
		};
		// clang-format on

		auto payloadDescriptor = parse(buffer, sizeof(buffer));

		REQUIRE(payloadDescriptor);
		REQUIRE(payloadDescriptor->temporalLayer == 0);
	}
}
