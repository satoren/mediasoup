#include "RTC/RTP/Codecs/FuzzerH265.hpp"
#include "RTC/RTP/Codecs/H265.hpp"

void FuzzerRtcRtpCodecsH265::Fuzz(const uint8_t* data, size_t len)
{
	RTC::RTP::Codecs::DependencyDescriptor* dependencyDescriptor{ nullptr };

	const RTC::RTP::Codecs::H265::PayloadDescriptor* descriptor =
	  RTC::RTP::Codecs::H265::Parse(data, len, dependencyDescriptor);

	if (!descriptor)
	{
		return;
	}

	delete descriptor;
}
