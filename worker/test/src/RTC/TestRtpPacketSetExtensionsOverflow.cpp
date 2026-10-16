#include "common.hpp"
#include "RTC/RtpPacket.hpp"
#include <catch2/catch_test_macros.hpp>
#include <cstring> // std::memset(), std::memcpy()
#include <vector>

using namespace RTC;

// ASAN repro for a suspected out-of-bounds write in RtpPacket::SetExtensions().
//
// SetExtensions() computes the number of bytes the payload must be shifted to
// make room for the new header extension block and stores it in an `int16_t`:
//
//     int16_t shift{ 0 };
//     ...
//     shift = static_cast<int16_t>(extensionsTotalSize - GetHeaderExtensionLength());
//     ...
//     shift = 4 + static_cast<int16_t>(extensionsTotalSize);
//
// `extensionsTotalSize` is a `size_t`. Casting it (or the size delta) to a
// 16-bit signed integer truncates/wraps once the value leaves the
// [-32768, 32767] range, producing a bogus (often large negative) `shift`. The
// subsequent `std::memmove(this->payload + shift, ...)` and the extension write
// loop then read/write outside the packet's backing buffer.
//
// Separately, SetExtensions() never checks the new total packet size against
// the capacity of the buffer the packet lives in, so a sufficiently large
// extension set overflows an undersized (e.g. MTU-sized) receive buffer even
// when `shift` does not overflow.
//
// Both scenarios are tagged "[.]" (hidden) so they do NOT run as part of the
// normal `invoke test` / CI suite (they intentionally trigger a fatal ASAN
// report). Run them explicitly against the ASAN build, one at a time, e.g.:
//
//     ./mediasoup-worker-test-asan "[rtppacket_setextensions_overflow_dest]"
//     ./mediasoup-worker-test-asan "[rtppacket_setextensions_overflow_shift]"

// Builds a minimal valid RTP packet (12-byte fixed header, no CSRC, no header
// extension) with the given payload length into `dst`.
static size_t BuildMinimalRtpPacket(uint8_t* dst, size_t payloadLen)
{
	std::memset(dst, 0, 12 + payloadLen);

	dst[0] = 0b10000000; // Version 2, no padding, no extension, no CSRC.
	dst[1] = 0b00000000; // No marker, payload type 0.
	// Sequence number.
	dst[2] = 0x00;
	dst[3] = 0x01;
	// Timestamp.
	dst[4] = dst[5] = dst[6] = dst[7] = 0x00;
	// SSRC.
	dst[8] = dst[9] = dst[10] = dst[11] = 0x01;

	for (size_t i = 0; i < payloadLen; ++i)
	{
		dst[12 + i] = static_cast<uint8_t>(0xA0 + (i & 0x0F));
	}

	return 12 + payloadLen;
}

SCENARIO(
  "RtpPacket::SetExtensions() overflows an undersized destination buffer",
  "[.][rtppacket_setextensions_overflow_dest]")
{
	// Mimic the receive path: the packet lives in a fixed, MTU-sized buffer with
	// a little slack (see Producer::HandleNotification, RTC::MtuSize + 100).
	constexpr size_t kBufferSize{ RTC::MtuSize + 100 }; // 1600 bytes.

	// Heap allocation so ASAN red-zones surround it and catch the overflow.
	auto* packetBuffer = new uint8_t[kBufferSize];

	const size_t len = BuildMinimalRtpPacket(packetBuffer, /*payloadLen=*/8);

	RtpPacket* packet = RtpPacket::Parse(packetBuffer, len);

	REQUIRE(packet != nullptr);

	// Build a One-Byte extension set whose total size (~2 KB) comfortably
	// exceeds the 1600-byte backing buffer but stays well within int16_t range,
	// isolating the "missing destination-capacity check" from the shift
	// truncation.
	std::vector<RtpPacket::GenericExtension> extensions;
	uint8_t value[16];
	std::memset(value, 0x5A, sizeof(value));

	// Each One-Byte element costs (1 + len) bytes; len is capped at 16.
	// ~130 elements * 17 bytes ≈ 2210 bytes > 1600.
	for (int i = 0; i < 130; ++i)
	{
		// Valid One-Byte ids are 1..14; duplicates are allowed and all are
		// written to the buffer.
		extensions.emplace_back(static_cast<uint8_t>((i % 14) + 1), uint8_t{ 16 }, value);
	}

	// Expected: ASAN reports a heap-buffer-overflow here (write past the
	// 1600-byte buffer) because SetExtensions() never validates the resulting
	// packet size against the buffer capacity.
	packet->SetExtensions(1, extensions);

	delete packet;
	delete[] packetBuffer;
}

// Builds a valid RTP packet that already carries a One-Byte (0xBEDE) header
// extension of `extensionValueSize` bytes (multiple of 4), followed by
// `payloadLen` payload bytes, into `dst`. Returns the total packet length.
static size_t BuildRtpPacketWithHeaderExtension(
  uint8_t* dst, size_t extensionValueSize, size_t payloadLen)
{
	const size_t total = 12 + 4 + extensionValueSize + payloadLen;
	std::memset(dst, 0, total);

	dst[0] = 0b10010000; // Version 2, no padding, extension=1, no CSRC.
	dst[1] = 0b00000000; // No marker, payload type 0.
	dst[2] = 0x00;
	dst[3] = 0x01;
	dst[4] = dst[5] = dst[6] = dst[7] = 0x00;
	dst[8] = dst[9] = dst[10] = dst[11] = 0x01;

	// Header extension: One-Byte profile id (0xBEDE), length in 32-bit words.
	const uint16_t words = static_cast<uint16_t>(extensionValueSize / 4);
	dst[12]              = 0xBE;
	dst[13]              = 0xDE;
	dst[14]              = static_cast<uint8_t>((words >> 8) & 0xFF);
	dst[15]              = static_cast<uint8_t>(words & 0xFF);
	// Extension value left as zeros (treated as padding elements).

	for (size_t i = 0; i < payloadLen; ++i)
	{
		dst[16 + extensionValueSize + i] = static_cast<uint8_t>(0xA0 + (i & 0x0F));
	}

	return total;
}

SCENARIO(
  "RtpPacket::SetExtensions() truncates the payload shift to int16_t",
  "[.][rtppacket_setextensions_overflow_shift]")
{
	// Faithful model of the reported scenario: a RECEIVED packet carrying a huge
	// header extension (> 32 KB), re-written with a small extension set (as
	// Producer::MangleRtpPacket does on every inbound packet).
	//
	// In SetExtensions():
	//     shift = static_cast<int16_t>(extensionsTotalSize - GetHeaderExtensionLength());
	//
	// Here extensionsTotalSize is small (the new set) and GetHeaderExtensionLength()
	// is large (the received extension). The true delta is a large NEGATIVE
	// number (the packet should shrink). Once |delta| exceeds 32768 the int16_t
	// cast wraps it to a large POSITIVE value, so
	//     std::memmove(this->payload + shift, this->payload, ...)
	// moves the payload far FORWARD, writing past the end of the backing buffer.

	// Received header extension of 33000 bytes (> 32768, so the shift wraps).
	constexpr size_t kExtensionValueSize{ 33000 };
	constexpr size_t kPayloadLen{ 8 };

	// Backing buffer sized exactly to the received packet, mimicking a packet
	// that fills (most of) a receive buffer. ASAN red-zones sit immediately
	// after it, so the forward-moved payload write is caught cleanly.
	const size_t len = 12 + 4 + kExtensionValueSize + kPayloadLen;
	auto* packetBuffer = new uint8_t[len];

	const size_t built = BuildRtpPacketWithHeaderExtension(packetBuffer, kExtensionValueSize, kPayloadLen);

	REQUIRE(built == len);

	RtpPacket* packet = RtpPacket::Parse(packetBuffer, len);

	REQUIRE(packet != nullptr);
	REQUIRE(packet->HasOneByteExtensions());
	REQUIRE(packet->GetHeaderExtensionLength() == kExtensionValueSize);

	// A small replacement extension set, as produced on the inbound mangle path.
	std::vector<RtpPacket::GenericExtension> extensions;
	uint8_t value[4] = { 0x01, 0x02, 0x03, 0x04 };
	extensions.emplace_back(uint8_t{ 1 }, uint8_t{ 4 }, value);
	extensions.emplace_back(uint8_t{ 2 }, uint8_t{ 4 }, value);

	// Expected: shift = int16_t(12 - 33000) = int16_t(-32988) = +32548, so
	// std::memmove() moves the payload ~32 KB forward, past the buffer end,
	// which ASAN reports as a heap-buffer-overflow WRITE.
	packet->SetExtensions(1, extensions);

	delete packet;
	delete[] packetBuffer;
}
