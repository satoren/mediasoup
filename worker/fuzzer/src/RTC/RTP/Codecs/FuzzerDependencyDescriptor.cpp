#include "RTC/RTP/Codecs/FuzzerDependencyDescriptor.hpp"

void FuzzerRtcRtpCodecsDependencyDescriptor::Fuzz(const uint8_t* data, size_t len)
{
	std::shared_ptr<RTC::RTP::Codecs::DependencyDescriptor::TemplateDependencyStructure>
	  templateDependencyStructure;

	auto* descriptor =
	  RTC::RTP::Codecs::DependencyDescriptor::Parse(data, len, templateDependencyStructure);

	if (!descriptor)
	{
		return;
	}

	// Rewrite it.
	static thread_local uint8_t buffer[RTC::Consts::TwoBytesRtpExtensionMaxLength];

	RTC::RTP::Codecs::DependencyDescriptor::RewriteParams rewriteParams;

	rewriteParams.frameNumberDelta           = 1;
	rewriteParams.templateIdDelta            = 1;
	rewriteParams.activeDecodeTargetsBitmask = descriptor->GetDecodeTargetsBitmask(0, 0);

	descriptor->GetMaxRewrittenLength();
	descriptor->Write(rewriteParams, buffer);

	delete descriptor;
}
