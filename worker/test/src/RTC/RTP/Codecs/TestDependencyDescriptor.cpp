#include "common.hpp"
#include "RTC/RTP/Codecs/DependencyDescriptor.hpp"
#include <catch2/catch_test_macros.hpp>
#include <cstring> // std::memcmp()

SCENARIO("Dependency Descriptor", "[rtp][codecs][dependency-descriptor]")
{
	using DependencyDescriptor   = RTC::RTP::Codecs::DependencyDescriptor;
	using DecodeTargetIndication = DependencyDescriptor::DecodeTargetIndication;

	SECTION("parse")
	{
		/**
		 * Taken from https://issues.webrtc.org/issues/42225660.
		 *
		 * {
		 *     "startOfFrame" : true,
		 *     "endOfFrame" : false,
		 *     "frameDependencyTemplateId" : 0,
		 *     "frameNumber" : 303,
		 *     "templateStructure": {
		 *         "templateIdOffset" : 0,
		 *         "templateInfo" : {
		 *           "0" : {
		 *             "spatialId" : 0,
		 *             "temporalId" : 0,
		 *             "dti" : [ "SWITCH", "SWITCH", "SWITCH" ],
		 *             "fdiff" : [],
		 *               "chains" : [0]
		 *           },
		 *           "1" : {
		 *             "spatialId" : 0,
		 *             "temporalId" : 0,
		 *             "dti" : [ "SWITCH", "SWITCH", "SWITCH" ],
		 *             "fdiff" : [4],
		 *             "chains" : [4]
		 *           },
		 *           "2" : {
		 *             "spatialId" : 0,
		 *             "temporalId" : 1,
		 *             "dti" : [ "NOT_PRESENT", "DISCARDABLE", "SWITCH" ],
		 *             "fdiff" : [2],
		 *             "chains" : [2]
		 *           },
		 *           "3" : {
		 *             "spatialId" : 0,
		 *             "temporalId" : 2,
		 *             "dti" : [ "NOT_PRESENT", "NOT_PRESENT", "DISCARDABLE" ],
		 *             "fdiff" : [1],
		 *               "chains" : [1]
		 *           },
		 *           "4" : {
		 *             "spatialId" : 0,
		 *             "temporalId" : 2,
		 *             "dti" : [ "NOT_PRESENT", "NOT_PRESENT", "DISCARDABLE" ],
		 *             "fdiff" : [1],
		 *             "chains" : [3]
		 *           }
		 *         },
		 *         "decodeTargetInfo" : {
		 *           "0" : { "protectedBy" : 0, "spatialId" : 0, "temporalId" : 0 },
		 *           "1" : { "protectedBy" : 0, "spatialId" : 0, "temporalId" : 1 },
		 *           "2" : { "protectedBy" : 0, "spatialId" : 0, "temporalId" : 2 }
		 *         },
		 *         "maxSpatialId" : 0,
		 *         "maxTemporalId" : 2
		 *         }
		 * }
		 */

		// clang-format off
		uint8_t data[] =
		{
			0x80, 0x01, 0x2F, 0x80, 0x02, 0x14, 0xEA, 0xA8,
			0x60, 0x41, 0x4D, 0x14, 0x10, 0x20, 0x84, 0x26
		};

		// clang-format on
		std::shared_ptr<RTC::RTP::Codecs::DependencyDescriptor::TemplateDependencyStructure>
		  templateDependencyStructure;
		auto dependencyDescriptor = std::unique_ptr<RTC::RTP::Codecs::DependencyDescriptor>(
		  RTC::RTP::Codecs::DependencyDescriptor::Parse(data, sizeof(data), templateDependencyStructure));

		REQUIRE(dependencyDescriptor);
		REQUIRE(dependencyDescriptor->startOfFrame == true);
		REQUIRE(dependencyDescriptor->endOfFrame == false);
		REQUIRE(dependencyDescriptor->frameDependencyTemplateId == 0);
		REQUIRE(dependencyDescriptor->frameNumber == 303);

		REQUIRE(dependencyDescriptor->isKeyFrame == true);
		REQUIRE(dependencyDescriptor->templateDependencyStructure == templateDependencyStructure);

		auto* templateStructure = dependencyDescriptor->templateDependencyStructure.get();
		std::vector<RTC::RTP::Codecs::DependencyDescriptor::DecodeTargetIndication> dtis{};
		std::vector<uint8_t> fdiffs{};
		std::vector<uint8_t> fdiffChains{};

		REQUIRE(templateStructure->templateLayers.size() == 5);
		REQUIRE(templateStructure->templateLayers[0].spatialLayer == 0);
		REQUIRE(templateStructure->templateLayers[0].temporalLayer == 0);
		dtis = {
			RTC::RTP::Codecs::DependencyDescriptor::DecodeTargetIndication::SWITCH,
			RTC::RTP::Codecs::DependencyDescriptor::DecodeTargetIndication::SWITCH,
			RTC::RTP::Codecs::DependencyDescriptor::DecodeTargetIndication::SWITCH,
		};
		REQUIRE(templateStructure->templateLayers[0].decodeTargetIndications == dtis);
		fdiffs = {};
		REQUIRE(templateStructure->templateLayers[0].frameDiffs == fdiffs);
		fdiffChains = { 0 };
		REQUIRE(templateStructure->templateLayers[0].frameDiffChains == fdiffChains);

		REQUIRE(templateStructure->templateLayers[1].spatialLayer == 0);
		REQUIRE(templateStructure->templateLayers[1].temporalLayer == 0);
		dtis = {
			RTC::RTP::Codecs::DependencyDescriptor::DecodeTargetIndication::SWITCH,
			RTC::RTP::Codecs::DependencyDescriptor::DecodeTargetIndication::SWITCH,
			RTC::RTP::Codecs::DependencyDescriptor::DecodeTargetIndication::SWITCH,
		};
		REQUIRE(templateStructure->templateLayers[1].decodeTargetIndications == dtis);
		fdiffs = { 4 };
		REQUIRE(templateStructure->templateLayers[1].frameDiffs == fdiffs);
		fdiffChains = { 4 };
		REQUIRE(templateStructure->templateLayers[1].frameDiffChains == fdiffChains);

		REQUIRE(templateStructure->templateLayers[2].spatialLayer == 0);
		REQUIRE(templateStructure->templateLayers[2].temporalLayer == 1);
		dtis = {
			RTC::RTP::Codecs::DependencyDescriptor::DecodeTargetIndication::NOT_PRESENT,
			RTC::RTP::Codecs::DependencyDescriptor::DecodeTargetIndication::DISCARDABLE,
			RTC::RTP::Codecs::DependencyDescriptor::DecodeTargetIndication::SWITCH,
		};
		REQUIRE(templateStructure->templateLayers[2].decodeTargetIndications == dtis);
		fdiffs = { 2 };
		REQUIRE(templateStructure->templateLayers[2].frameDiffs == fdiffs);
		fdiffChains = { 2 };
		REQUIRE(templateStructure->templateLayers[2].frameDiffChains == fdiffChains);

		REQUIRE(templateStructure->templateLayers[3].spatialLayer == 0);
		REQUIRE(templateStructure->templateLayers[3].temporalLayer == 2);
		dtis = {
			RTC::RTP::Codecs::DependencyDescriptor::DecodeTargetIndication::NOT_PRESENT,
			RTC::RTP::Codecs::DependencyDescriptor::DecodeTargetIndication::NOT_PRESENT,
			RTC::RTP::Codecs::DependencyDescriptor::DecodeTargetIndication::DISCARDABLE,
		};
		REQUIRE(templateStructure->templateLayers[3].decodeTargetIndications == dtis);
		fdiffs = { 1 };
		REQUIRE(templateStructure->templateLayers[3].frameDiffs == fdiffs);
		fdiffChains = { 1 };
		REQUIRE(templateStructure->templateLayers[3].frameDiffChains == fdiffChains);

		REQUIRE(templateStructure->templateLayers[4].spatialLayer == 0);
		REQUIRE(templateStructure->templateLayers[4].temporalLayer == 2);
		dtis = {
			RTC::RTP::Codecs::DependencyDescriptor::DecodeTargetIndication::NOT_PRESENT,
			RTC::RTP::Codecs::DependencyDescriptor::DecodeTargetIndication::NOT_PRESENT,
			RTC::RTP::Codecs::DependencyDescriptor::DecodeTargetIndication::DISCARDABLE,
		};
		REQUIRE(templateStructure->templateLayers[4].decodeTargetIndications == dtis);
		fdiffs = { 1 };
		REQUIRE(templateStructure->templateLayers[4].frameDiffs == fdiffs);
		fdiffChains = { 3 };
		REQUIRE(templateStructure->templateLayers[4].frameDiffChains == fdiffChains);

		REQUIRE(templateStructure->decodeTargetCount == 3);
		REQUIRE(templateStructure->chainCount == 1);
		REQUIRE(templateStructure->decodeTargetLayers.size() == 3);
		REQUIRE(templateStructure->decodeTargetLayers[0].spatialLayer == 0);
		REQUIRE(templateStructure->decodeTargetLayers[0].temporalLayer == 0);
		REQUIRE(templateStructure->decodeTargetLayers[1].spatialLayer == 0);
		REQUIRE(templateStructure->decodeTargetLayers[1].temporalLayer == 1);
		REQUIRE(templateStructure->decodeTargetLayers[2].spatialLayer == 0);
		REQUIRE(templateStructure->decodeTargetLayers[2].temporalLayer == 2);
		REQUIRE(templateStructure->activeDecodeTargetsBitmask == 0b111);
		REQUIRE(dependencyDescriptor->activeDecodeTargetsBitmask == 0b111);

		REQUIRE(dependencyDescriptor->GetDecodeTarget(0, 0) == 0);
		REQUIRE(dependencyDescriptor->GetDecodeTarget(0, 1) == 1);
		REQUIRE(dependencyDescriptor->GetDecodeTarget(0, 2) == 2);
		REQUIRE(!dependencyDescriptor->GetDecodeTarget(1, 0).has_value());
		REQUIRE(!dependencyDescriptor->GetDecodeTarget(0, 3).has_value());

		REQUIRE(dependencyDescriptor->GetDecodeTargetsBitmask(0, 0) == 0b001);
		REQUIRE(dependencyDescriptor->GetDecodeTargetsBitmask(0, 1) == 0b011);
		REQUIRE(dependencyDescriptor->GetDecodeTargetsBitmask(0, 2) == 0b111);
		REQUIRE(dependencyDescriptor->GetDecodeTargetsBitmask(2, 5) == 0b111);

		REQUIRE(dependencyDescriptor->GetDecodeTargetIndication(0) == DecodeTargetIndication::SWITCH);
		REQUIRE(dependencyDescriptor->GetDecodeTargetIndication(2) == DecodeTargetIndication::SWITCH);
		REQUIRE(dependencyDescriptor->GetDecodeTargetIndication(3) == DecodeTargetIndication::NOT_PRESENT);
	}

	SECTION("write")
	{
		/**
		 * Key frame with a template dependency structure with 2 decode targets
		 * (L1T2).
		 *
		 * <DependencyDescriptor>
		 * 	startOfFrame: true
		 * 	endOfFrame: false
		 * 	frameDependencyTemplateId: 0
		 * 	frameNumber: 232
		 * 	templateId: 0
		 * 	temporalLayer: 0
		 * 	spatialLayer: 0
		 * 	<TemplateDependencyStructure>
		 * 		spatialLayers: 0
		 * 		temporalLayers: 1
		 * 		templateIdOffset: 0
		 * 		decodeTargetCount: 2
		 * 		<TemplateLayers>
		 * 		  <FrameDependencyTemplate>
		 * 		    spatialLayerId: 0
		 * 		    temporalLayerId: 0
		 * 		    <DecodeTargetIndications> SS </DecodeTargetIndications>
		 * 		    <FrameDiffs>  </FrameDiffs>
		 * 		    <FrameDiffChains> 0 </FrameDiffChains>
		 * 		  <FrameDependencyTemplate>
		 * 		  <FrameDependencyTemplate>
		 * 		    spatialLayerId: 0
		 * 		    temporalLayerId: 0
		 * 		    <DecodeTargetIndications> SS </DecodeTargetIndications>
		 * 		    <FrameDiffs> 2 </FrameDiffs>
		 * 		    <FrameDiffChains> 2 </FrameDiffChains>
		 * 		  <FrameDependencyTemplate>
		 * 		  <FrameDependencyTemplate>
		 * 		    spatialLayerId: 0
		 * 		    temporalLayerId: 1
		 * 		    <DecodeTargetIndications> -D </DecodeTargetIndications>
		 * 		    <FrameDiffs> 1 </FrameDiffs>
		 * 		    <FrameDiffChains> 1 </FrameDiffChains>
		 * 		  <FrameDependencyTemplate>
		 * 		</TemplateLayers>
		 * 		</TemplateDependencyStructure>
		 * </DependencyDescriptor>
		 */
		// clang-format off
		uint8_t keyFrameData[] =
		{
			0x80, 0x00, 0xE8, 0x80,
			0x01, 0x1E, 0xA8, 0x51,
			0x41, 0x01, 0x0C, 0x13,
			0xFC, 0x0B, 0x3C,
		};

		// Mandatory fields only: frameDependencyTemplateId: 2, frameNumber: 233.
		uint8_t data[] =
		{
			0x02, 0x00, 0xE9,
		};
		// clang-format on

		uint8_t buffer[RTC::Consts::TwoBytesRtpExtensionMaxLength];
		uint8_t len;

		std::shared_ptr<DependencyDescriptor::TemplateDependencyStructure> templateDependencyStructure;

		std::unique_ptr<DependencyDescriptor> keyFrameDependencyDescriptor(
		  DependencyDescriptor::Parse(keyFrameData, sizeof(keyFrameData), templateDependencyStructure));

		REQUIRE(keyFrameDependencyDescriptor);
		REQUIRE(keyFrameDependencyDescriptor->isKeyFrame);
		REQUIRE(keyFrameDependencyDescriptor->frameNumber == 232);
		REQUIRE(templateDependencyStructure->decodeTargetCount == 2);

		std::unique_ptr<DependencyDescriptor> dependencyDescriptor(
		  DependencyDescriptor::Parse(data, sizeof(data), templateDependencyStructure));

		REQUIRE(dependencyDescriptor);
		REQUIRE(!dependencyDescriptor->isKeyFrame);
		REQUIRE(dependencyDescriptor->frameNumber == 233);
		REQUIRE(dependencyDescriptor->templateId == 2);
		REQUIRE(dependencyDescriptor->temporalLayer == 1);
		REQUIRE(!dependencyDescriptor->activeDecodeTargetsBitmask.has_value());
		REQUIRE(dependencyDescriptor->GetLength() == sizeof(data));
		REQUIRE(std::memcmp(dependencyDescriptor->GetData(), data, sizeof(data)) == 0);
		// 3 bytes, 5 flag bits and 2 bits of active decode targets.
		REQUIRE(dependencyDescriptor->GetMaxRewrittenLength() == 4);

		// Write without changes.
		len = dependencyDescriptor->Write({}, buffer);

		REQUIRE(len == sizeof(data));
		REQUIRE(std::memcmp(buffer, data, sizeof(data)) == 0);

		len = keyFrameDependencyDescriptor->Write({}, buffer);

		REQUIRE(len == sizeof(keyFrameData));
		REQUIRE(std::memcmp(buffer, keyFrameData, sizeof(keyFrameData)) == 0);

		// Write the active decode targets.
		DependencyDescriptor::RewriteParams rewriteParams;

		rewriteParams.activeDecodeTargetsBitmask = 0b01;

		len = dependencyDescriptor->Write(rewriteParams, buffer);

		REQUIRE(len == 4);

		{
			std::unique_ptr<DependencyDescriptor> rewrittenDependencyDescriptor(
			  DependencyDescriptor::Parse(buffer, len, templateDependencyStructure));

			REQUIRE(rewrittenDependencyDescriptor);
			REQUIRE(rewrittenDependencyDescriptor->frameNumber == 233);
			REQUIRE(rewrittenDependencyDescriptor->templateId == 2);
			REQUIRE(rewrittenDependencyDescriptor->activeDecodeTargetsBitmask == 0b01);
			// The active decode targets told by the sender are kept.
			REQUIRE(templateDependencyStructure->activeDecodeTargetsBitmask == 0b01);

			// Writing it again without changes keeps them.
			uint8_t buffer2[RTC::Consts::TwoBytesRtpExtensionMaxLength];

			REQUIRE(rewrittenDependencyDescriptor->Write({}, buffer2) == len);
			REQUIRE(std::memcmp(buffer, buffer2, len) == 0);

			// And they can be overridden.
			rewriteParams.activeDecodeTargetsBitmask = 0b11;

			REQUIRE(rewrittenDependencyDescriptor->Write(rewriteParams, buffer2) == len);

			std::unique_ptr<DependencyDescriptor> rewrittenDependencyDescriptor2(
			  DependencyDescriptor::Parse(buffer2, len, templateDependencyStructure));

			REQUIRE(rewrittenDependencyDescriptor2);
			REQUIRE(rewrittenDependencyDescriptor2->activeDecodeTargetsBitmask == 0b11);
		}

		// Write the key frame with active decode targets.
		len = keyFrameDependencyDescriptor->Write(rewriteParams, buffer);

		REQUIRE(len == sizeof(keyFrameData));

		{
			std::shared_ptr<DependencyDescriptor::TemplateDependencyStructure> rewrittenTemplateDependencyStructure;
			std::unique_ptr<DependencyDescriptor> rewrittenDependencyDescriptor(
			  DependencyDescriptor::Parse(buffer, len, rewrittenTemplateDependencyStructure));

			REQUIRE(rewrittenDependencyDescriptor);
			REQUIRE(rewrittenDependencyDescriptor->isKeyFrame);
			REQUIRE(rewrittenDependencyDescriptor->frameNumber == 232);
			REQUIRE(rewrittenDependencyDescriptor->activeDecodeTargetsBitmask == 0b11);
			REQUIRE(rewrittenTemplateDependencyStructure->templateLayers.size() == 3);
		}

		// Shift frame numbers and template ids.
		rewriteParams.frameNumberDelta = 65535;
		rewriteParams.templateIdDelta  = 63;
		rewriteParams.activeDecodeTargetsBitmask.reset();

		len = keyFrameDependencyDescriptor->Write(rewriteParams, buffer);

		REQUIRE(len == sizeof(keyFrameData));

		std::shared_ptr<DependencyDescriptor::TemplateDependencyStructure> rewrittenTemplateDependencyStructure;

		{
			std::unique_ptr<DependencyDescriptor> rewrittenDependencyDescriptor(
			  DependencyDescriptor::Parse(buffer, len, rewrittenTemplateDependencyStructure));

			REQUIRE(rewrittenDependencyDescriptor);
			REQUIRE(rewrittenDependencyDescriptor->isKeyFrame);
			REQUIRE(rewrittenDependencyDescriptor->frameNumber == 231);
			REQUIRE(rewrittenDependencyDescriptor->frameDependencyTemplateId == 63);
			REQUIRE(rewrittenDependencyDescriptor->templateId == 0);
			REQUIRE(rewrittenTemplateDependencyStructure->templateIdOffset == 63);
			REQUIRE(rewrittenTemplateDependencyStructure->decodeTargetCount == 2);
			REQUIRE(rewrittenTemplateDependencyStructure->templateLayers.size() == 3);
		}

		len = dependencyDescriptor->Write(rewriteParams, buffer);

		REQUIRE(len == sizeof(data));

		{
			std::unique_ptr<DependencyDescriptor> rewrittenDependencyDescriptor(
			  DependencyDescriptor::Parse(buffer, len, rewrittenTemplateDependencyStructure));

			REQUIRE(rewrittenDependencyDescriptor);
			REQUIRE(rewrittenDependencyDescriptor->frameNumber == 232);
			REQUIRE(rewrittenDependencyDescriptor->frameDependencyTemplateId == 1);
			// Same template as in the original one.
			REQUIRE(rewrittenDependencyDescriptor->templateId == 2);
			REQUIRE(rewrittenDependencyDescriptor->temporalLayer == 1);
		}
	}

	SECTION("a new template dependency structure does not alter previous descriptors")
	{
		// clang-format off
		// L1T2 key frame (see above).
		uint8_t keyFrameData[] =
		{
			0x80, 0x00, 0xE8, 0x80,
			0x01, 0x1E, 0xA8, 0x51,
			0x41, 0x01, 0x0C, 0x13,
			0xFC, 0x0B, 0x3C,
		};

		// L1T3 key frame (see "parse" above).
		uint8_t keyFrameData2[] =
		{
			0x80, 0x01, 0x2F, 0x80, 0x02, 0x14, 0xEA, 0xA8,
			0x60, 0x41, 0x4D, 0x14, 0x10, 0x20, 0x84, 0x26
		};

		uint8_t data[] =
		{
			0x02, 0x00, 0xE9,
		};
		// clang-format on

		std::shared_ptr<DependencyDescriptor::TemplateDependencyStructure> templateDependencyStructure;

		std::unique_ptr<DependencyDescriptor> keyFrameDependencyDescriptor(
		  DependencyDescriptor::Parse(keyFrameData, sizeof(keyFrameData), templateDependencyStructure));

		REQUIRE(keyFrameDependencyDescriptor);

		std::unique_ptr<DependencyDescriptor> dependencyDescriptor(
		  DependencyDescriptor::Parse(data, sizeof(data), templateDependencyStructure));

		REQUIRE(dependencyDescriptor);

		auto* firstTemplateDependencyStructure = templateDependencyStructure.get();

		std::unique_ptr<DependencyDescriptor> keyFrameDependencyDescriptor2(
		  DependencyDescriptor::Parse(keyFrameData2, sizeof(keyFrameData2), templateDependencyStructure));

		REQUIRE(keyFrameDependencyDescriptor2);
		REQUIRE(templateDependencyStructure.get() != firstTemplateDependencyStructure);
		REQUIRE(templateDependencyStructure->decodeTargetCount == 3);
		REQUIRE(
		  dependencyDescriptor->templateDependencyStructure.get() == firstTemplateDependencyStructure);
		REQUIRE(dependencyDescriptor->templateDependencyStructure->decodeTargetCount == 2);
		REQUIRE(dependencyDescriptor->GetDecodeTargetsBitmask(0, 1) == 0b11);
	}

	SECTION("parse fails without template dependency structure")
	{
		// clang-format off
		uint8_t data[] =
		{
			0x02, 0x00, 0xE9,
		};
		// clang-format on

		std::shared_ptr<DependencyDescriptor::TemplateDependencyStructure> templateDependencyStructure;

		const std::unique_ptr<DependencyDescriptor> dependencyDescriptor(
		  DependencyDescriptor::Parse(data, sizeof(data), templateDependencyStructure));

		REQUIRE(!dependencyDescriptor);
		REQUIRE(!templateDependencyStructure);
	}
}
