#include "common.hpp"
#include "RTC/RTCP/SenderReport.hpp"
#include "RTC/RTP/Codecs/AV1.hpp"
#include "RTC/RTP/Codecs/DependencyDescriptor.hpp"
#include "RTC/RTP/Codecs/Tools.hpp"
#include "RTC/RTP/Packet.hpp"
#include "RTC/RTP/RtpStreamRecv.hpp"
#include "RTC/SimulcastProducerStreamManager.hpp"
#include "Utils.hpp"
#include "test/include/RTC/RTP/rtpCommon.hpp"
#include "mocks/include/MockShared.hpp"
#include <catch2/catch_test_macros.hpp>
#include <cstring> // std::memcpy(), std::memcmp()
#include <vector>

namespace
{
	using DependencyDescriptor        = RTC::RTP::Codecs::DependencyDescriptor;
	using TemplateDependencyStructure = DependencyDescriptor::TemplateDependencyStructure;

	constexpr uint8_t DependencyDescriptorId{ static_cast<uint8_t>(
		RTC::RtpHeaderExtensionUri::Type::DEPENDENCY_DESCRIPTOR) };

	/**
	 * First packet of a L1T3 key frame with frame number 303 and a template
	 * dependency structure with these templates:
	 *
	 * - 0: T0, DTIs "SSS".
	 * - 1: T0, DTIs "SSS".
	 * - 2: T1, DTIs "-DS".
	 * - 3: T2, DTIs "--D".
	 * - 4: T2, DTIs "--D".
	 *
	 * Taken from https://issues.webrtc.org/issues/42225660.
	 */
	// clang-format off
	const std::vector<uint8_t> KeyFrameDependencyDescriptor =
	{
		0x80, 0x01, 0x2F, 0x80, 0x02, 0x14, 0xEA, 0xA8,
		0x60, 0x41, 0x4D, 0x14, 0x10, 0x20, 0x84, 0x26
	};
	// clang-format on

	constexpr uint16_t KeyFrameNumber{ 303 };
	constexpr uint8_t KeyFrameTemplateCount{ 5 };

	// Templates of the frames of each temporal layer.
	constexpr uint8_t T0Template{ 1 };
	constexpr uint8_t T1Template{ 2 };
	constexpr uint8_t T2Template{ 3 };

	std::vector<uint8_t> createDependencyDescriptor(
	  bool startOfFrame, bool endOfFrame, uint8_t frameDependencyTemplateId, uint16_t frameNumber)
	{
		return {
			static_cast<uint8_t>(
			  (startOfFrame ? 0x80 : 0x00) | (endOfFrame ? 0x40 : 0x00) | frameDependencyTemplateId),
			static_cast<uint8_t>(frameNumber >> 8),
			static_cast<uint8_t>(frameNumber & 0xFF),
		};
	}

	std::vector<uint8_t> createKeyFrameDependencyDescriptor(uint16_t frameNumber)
	{
		std::shared_ptr<TemplateDependencyStructure> templateDependencyStructure;

		const std::unique_ptr<DependencyDescriptor> dependencyDescriptor(
		  DependencyDescriptor::Parse(
		    KeyFrameDependencyDescriptor.data(),
		    KeyFrameDependencyDescriptor.size(),
		    templateDependencyStructure));

		REQUIRE(dependencyDescriptor);

		DependencyDescriptor::RewriteParams rewriteParams;

		rewriteParams.frameNumberDelta = static_cast<uint16_t>(frameNumber - KeyFrameNumber);

		uint8_t buffer[RTC::Consts::TwoBytesRtpExtensionMaxLength];
		const auto len = dependencyDescriptor->Write(rewriteParams, buffer);

		REQUIRE(len != 0);

		return { buffer, buffer + len };
	}

	/**
	 * Creates AV1 packets carrying the given Dependency Descriptor, with room for
	 * it to be rewritten as the Producer makes.
	 */
	class PacketFactory
	{
	public:
		std::unique_ptr<RTC::RTP::Packet> Create(
		  const std::vector<uint8_t>& dependencyDescriptor,
		  std::shared_ptr<TemplateDependencyStructure>& templateDependencyStructure,
		  uint8_t room = 8)
		{
			auto& buffer = this->buffers.emplace_back(new uint8_t[1500]);

			std::unique_ptr<RTC::RTP::Packet> packet(RTC::RTP::Packet::Factory(buffer.get(), 1500));

			REQUIRE(packet);

			packet->SetPayloadType(96);
			packet->SetSsrc(1111);
			packet->SetSequenceNumber(++this->seq);
			packet->SetTimestamp(this->seq * 3000);

			uint8_t value[RTC::Consts::TwoBytesRtpExtensionMaxLength]{};

			std::memcpy(value, dependencyDescriptor.data(), dependencyDescriptor.size());

			const std::vector<RTC::RTP::Packet::Extension> extensions{
				{ RTC::RtpHeaderExtensionUri::Type::DEPENDENCY_DESCRIPTOR,
				 DependencyDescriptorId, static_cast<uint8_t>(dependencyDescriptor.size() + room),
				 value }
			};

			packet->SetExtensions(RTC::RTP::Packet::ExtensionsType::Auto, extensions);

			REQUIRE(packet->UpdateDependencyDescriptor(
			  dependencyDescriptor.data(), dependencyDescriptor.size()));

			// Aggregation header (W=1) and an OBU_FRAME.
			const uint8_t payload[] = { 0x10, 0x30, 0x20, 0x00, 0x00 };

			packet->SetPayload(payload, sizeof(payload));

			RTC::RTP::Codecs::AV1::ProcessRtpPacket(packet.get(), templateDependencyStructure);

			return packet;
		}

	private:
		std::vector<std::unique_ptr<uint8_t[]>> buffers;
		uint16_t seq{ 0 };
	};

	std::vector<uint8_t> getDependencyDescriptor(const RTC::RTP::Packet* packet)
	{
		uint8_t len;
		const uint8_t* value = packet->GetExtensionValue(DependencyDescriptorId, len);

		REQUIRE(value);

		return { value, value + len };
	}

	/**
	 * Parses the Dependency Descriptor of the packet as the receiver does.
	 */
	std::unique_ptr<DependencyDescriptor> receiveDependencyDescriptor(
	  const RTC::RTP::Packet* packet,
	  std::shared_ptr<TemplateDependencyStructure>& templateDependencyStructure)
	{
		const auto data = getDependencyDescriptor(packet);

		std::unique_ptr<DependencyDescriptor> dependencyDescriptor(
		  DependencyDescriptor::Parse(data.data(), data.size(), templateDependencyStructure));

		REQUIRE(dependencyDescriptor);

		return dependencyDescriptor;
	}

	RTC::RTP::Codecs::EncodingContext::Params createSimulcastParams(uint8_t temporalLayers = 3)
	{
		RTC::RTP::Codecs::EncodingContext::Params params;

		params.spatialLayers               = 2;
		params.temporalLayers              = temporalLayers;
		params.simulcast                   = true;
		params.rewriteDependencyDescriptor = true;

		return params;
	}

	/**
	 * Does what SimulcastProducerStreamManager does when syncing to a stream.
	 */
	void syncContext(RTC::RTP::Codecs::AV1::EncodingContext& context, int16_t targetTemporalLayer)
	{
		context.SyncRequired();
		context.SetTargetTemporalLayer(targetTemporalLayer);
		context.SetCurrentTemporalLayer(0);
	}

	bool process(RTC::RTP::Codecs::AV1::EncodingContext& context, RTC::RTP::Packet* packet)
	{
		bool marker{ packet->HasMarker() };

		return packet->ProcessPayload(std::addressof(context), marker);
	}

	class ProducerStreamManagerListener : public RTC::ProducerStreamManager::Listener
	{
	public:
		bool IsActive() const override
		{
			return true;
		}
		void OnProducerStreamManagerKeyFrameRequested(uint32_t /*mappedSsrc*/) override
		{
		}
		void OnProducerStreamManagerNeedBitrateChange() override
		{
		}
		void OnProducerStreamManagerLayersChanged() override
		{
		}
		void OnProducerStreamManagerClearRetransmissionBuffer() override
		{
		}
		void OnProducerStreamManagerScore() override
		{
		}
	};

	class RtpStreamRecvListener : public RTC::RTP::RtpStreamRecv::Listener
	{
	public:
		void OnRtpStreamScore(
		  RTC::RTP::RtpStream* /*rtpStream*/, uint8_t /*score*/, uint8_t /*previousScore*/) override
		{
		}
		void OnRtpStreamSendRtcpPacket(
		  RTC::RTP::RtpStreamRecv* /*rtpStream*/, RTC::RTCP::Packet* /*packet*/) override
		{
		}
		uint8_t OnRtpStreamNeedWorstRemoteFractionLost(RTC::RTP::RtpStreamRecv* /*rtpStream*/) override
		{
			return 0;
		}
		void OnRtpStreamSpatialLayerActivityChanged(
		  RTC::RTP::RtpStreamRecv* /*rtpStream*/, uint8_t /*spatialLayer*/, bool /*isActive*/) override
		{
		}
	};

	// NOLINTNEXTLINE(readability-identifier-naming)
	RtpStreamRecvListener streamRecvListener;

	// NOLINTNEXTLINE(readability-identifier-naming)
	mocks::MockShared shared(/*getTimeUs*/
	                         []() -> int64_t
	                         {
		                         return 1000000000000;
	                         });
} // namespace

SCENARIO("AV1 payload descriptor", "[rtp][codecs][av1]")
{
	SECTION("IsKeyFrame() tells key frames from the aggregation header and the frame header")
	{
		// clang-format off
		// N=1, sequence header OBU and key frame OBU_FRAME, with length fields.
		const uint8_t keyFrame[] = { 0x08, 0x02, 0x08, 0x00, 0x03, 0x30, 0x10, 0x00 };
		// N=1, sequence header OBU and inter frame OBU_FRAME.
		const uint8_t interFrame[] = { 0x08, 0x02, 0x08, 0x00, 0x03, 0x30, 0x20, 0x00 };
		// N=1, sequence header OBU and an OBU_FRAME showing an existing frame.
		const uint8_t showExistingFrame[] = { 0x08, 0x02, 0x08, 0x00, 0x03, 0x30, 0x80, 0x00 };
		// Z=1 (continuation of an OBU of the previous packet).
		const uint8_t continuation[] = { 0x88, 0x02, 0x08, 0x00, 0x03, 0x30, 0x10, 0x00 };
		// N=0.
		const uint8_t notNewCodedVideoSequence[] = { 0x00, 0x02, 0x08, 0x00, 0x03, 0x30, 0x10, 0x00 };
		// N=1, W=2, the last OBU element without length field.
		const uint8_t keyFrameW2[] = { 0x28, 0x02, 0x08, 0x00, 0x30, 0x10, 0x00 };
		const uint8_t interFrameW2[] = { 0x28, 0x02, 0x08, 0x00, 0x30, 0x20, 0x00 };
		// N=1, OBU_FRAME_HEADER with extension and size field.
		const uint8_t keyFrameHeaderWithExtension[] = { 0x18, 0x1E, 0x00, 0x01, 0x10 };
		const uint8_t interFrameHeaderWithExtension[] = { 0x18, 0x1E, 0x00, 0x01, 0x20 };
		// N=1, W=1, just the sequence header (the frame is in the next packet).
		const uint8_t sequenceHeaderOnly[] = { 0x18, 0x08, 0x00, 0x00 };
		// Too short.
		const uint8_t tooShort[] = { 0x08 };
		// clang-format on

		REQUIRE(RTC::RTP::Codecs::AV1::IsKeyFrame(keyFrame, sizeof(keyFrame)));
		REQUIRE(!RTC::RTP::Codecs::AV1::IsKeyFrame(interFrame, sizeof(interFrame)));
		REQUIRE(!RTC::RTP::Codecs::AV1::IsKeyFrame(showExistingFrame, sizeof(showExistingFrame)));
		REQUIRE(!RTC::RTP::Codecs::AV1::IsKeyFrame(continuation, sizeof(continuation)));
		REQUIRE(!RTC::RTP::Codecs::AV1::IsKeyFrame(
		  notNewCodedVideoSequence, sizeof(notNewCodedVideoSequence)));
		REQUIRE(RTC::RTP::Codecs::AV1::IsKeyFrame(keyFrameW2, sizeof(keyFrameW2)));
		REQUIRE(!RTC::RTP::Codecs::AV1::IsKeyFrame(interFrameW2, sizeof(interFrameW2)));
		REQUIRE(
		  RTC::RTP::Codecs::AV1::IsKeyFrame(
		    keyFrameHeaderWithExtension, sizeof(keyFrameHeaderWithExtension)));
		REQUIRE(!RTC::RTP::Codecs::AV1::IsKeyFrame(
		  interFrameHeaderWithExtension, sizeof(interFrameHeaderWithExtension)));
		REQUIRE(RTC::RTP::Codecs::AV1::IsKeyFrame(sequenceHeaderOnly, sizeof(sequenceHeaderOnly)));
		REQUIRE(!RTC::RTP::Codecs::AV1::IsKeyFrame(tooShort, sizeof(tooShort)));
	}

	SECTION("packets without Dependency Descriptor are forwarded")
	{
		// clang-format off
		uint8_t keyFrame[] = { 0x08, 0x02, 0x08, 0x00, 0x03, 0x30, 0x10, 0x00 };
		uint8_t interFrame[] = { 0x10, 0x30, 0x20, 0x00 };
		// clang-format on

		std::unique_ptr<DependencyDescriptor> dependencyDescriptor;

		std::unique_ptr<RTC::RTP::Codecs::AV1::PayloadDescriptor> payloadDescriptor(
		  RTC::RTP::Codecs::AV1::Parse(keyFrame, sizeof(keyFrame), dependencyDescriptor));

		REQUIRE(payloadDescriptor);
		REQUIRE(payloadDescriptor->isKeyFrame);
		REQUIRE(!payloadDescriptor->dependencyDescriptor);

		payloadDescriptor.reset(
		  RTC::RTP::Codecs::AV1::Parse(interFrame, sizeof(interFrame), dependencyDescriptor));

		REQUIRE(payloadDescriptor);
		REQUIRE(!payloadDescriptor->isKeyFrame);

		std::unique_ptr<RTC::RTP::Packet> packet(
		  RTC::RTP::Packet::Factory(rtpCommon::FactoryBuffer, sizeof(rtpCommon::FactoryBuffer)));

		packet->SetPayload(keyFrame, sizeof(keyFrame));

		std::shared_ptr<TemplateDependencyStructure> templateDependencyStructure;

		RTC::RTP::Codecs::AV1::ProcessRtpPacket(packet.get(), templateDependencyStructure);

		REQUIRE(packet->IsKeyFrame());
		REQUIRE(packet->GetSpatialLayer() == 0);
		REQUIRE(packet->GetTemporalLayer() == 0);
		REQUIRE(packet->GetDependencyDescriptorMaxLength() == 0);

		auto params = createSimulcastParams();
		RTC::RTP::Codecs::AV1::EncodingContext context(params);

		syncContext(context, /*targetTemporalLayer*/ 0);

		REQUIRE(process(context, packet.get()));
		REQUIRE(context.GetCurrentSpatialLayer() == 0);
		REQUIRE(context.GetCurrentTemporalLayer() == 0);
		REQUIRE(!packet->GetPayloadEncoder());
	}
}

SCENARIO("AV1 simulcast", "[rtp][codecs][av1][simulcast]")
{
	PacketFactory factory;
	std::shared_ptr<TemplateDependencyStructure> templateDependencyStructure;

	SECTION("temporal layers are switched at frames that allow it")
	{
		auto params = createSimulcastParams();
		RTC::RTP::Codecs::AV1::EncodingContext context(params);

		syncContext(context, /*targetTemporalLayer*/ 0);

		uint16_t frameNumber{ KeyFrameNumber };

		auto keyFrame =
		  factory.Create(createKeyFrameDependencyDescriptor(frameNumber), templateDependencyStructure);

		REQUIRE(keyFrame->IsKeyFrame());
		REQUIRE(process(context, keyFrame.get()));
		REQUIRE(context.GetCurrentSpatialLayer() == 0);
		REQUIRE(context.GetCurrentTemporalLayer() == 0);

		const auto createFrame = [&](uint8_t frameDependencyTemplateId)
		{
			return factory.Create(
			  createDependencyDescriptor(true, true, frameDependencyTemplateId, ++frameNumber),
			  templateDependencyStructure);
		};

		// Higher temporal layers are dropped.
		REQUIRE(!process(context, createFrame(T2Template).get()));
		REQUIRE(!process(context, createFrame(T1Template).get()));
		REQUIRE(!process(context, createFrame(T2Template).get()));
		REQUIRE(process(context, createFrame(T0Template).get()));

		// T1 frames are not a switching point for T1 (DTI "D"), so T1 is not
		// forwarded until the next T0 frame, which is (DTI "S").
		context.SetTargetTemporalLayer(1);

		REQUIRE(!process(context, createFrame(T2Template).get()));
		REQUIRE(!process(context, createFrame(T1Template).get()));
		REQUIRE(context.GetCurrentTemporalLayer() == 0);
		REQUIRE(!process(context, createFrame(T2Template).get()));
		REQUIRE(process(context, createFrame(T0Template).get()));
		REQUIRE(context.GetCurrentTemporalLayer() == 1);
		REQUIRE(!process(context, createFrame(T2Template).get()));
		REQUIRE(process(context, createFrame(T1Template).get()));

		// T2 frames are not a switching point for T2 (DTI "D"), but T1 and T0 ones
		// are (DTI "S").
		context.SetTargetTemporalLayer(2);

		REQUIRE(!process(context, createFrame(T2Template).get()));
		REQUIRE(context.GetCurrentTemporalLayer() == 1);
		REQUIRE(process(context, createFrame(T0Template).get()));
		REQUIRE(context.GetCurrentTemporalLayer() == 2);
		REQUIRE(process(context, createFrame(T2Template).get()));
		REQUIRE(process(context, createFrame(T1Template).get()));
		REQUIRE(process(context, createFrame(T2Template).get()));

		// Downgrade happens once a new frame starts.
		context.SetTargetTemporalLayer(0);

		auto lastPacketOfFrame = factory.Create(
		  createDependencyDescriptor(false, true, T2Template, frameNumber), templateDependencyStructure);

		REQUIRE(process(context, lastPacketOfFrame.get()));
		REQUIRE(context.GetCurrentTemporalLayer() == 2);
		REQUIRE(!process(context, createFrame(T1Template).get()));
		REQUIRE(context.GetCurrentTemporalLayer() == 0);
		REQUIRE(!process(context, createFrame(T2Template).get()));
		REQUIRE(process(context, createFrame(T0Template).get()));

		// A key frame allows to switch to any temporal layer.
		context.SetTargetTemporalLayer(2);

		keyFrame =
		  factory.Create(createKeyFrameDependencyDescriptor(++frameNumber), templateDependencyStructure);

		REQUIRE(process(context, keyFrame.get()));
		REQUIRE(context.GetCurrentTemporalLayer() == 2);
		REQUIRE(process(context, createFrame(T2Template).get()));
	}

	SECTION("packets of temporal layers higher than the announced ones are dropped")
	{
		auto params = createSimulcastParams(/*temporalLayers*/ 2);
		RTC::RTP::Codecs::AV1::EncodingContext context(params);

		syncContext(context, /*targetTemporalLayer*/ 1);

		uint16_t frameNumber{ KeyFrameNumber };

		auto keyFrame =
		  factory.Create(createKeyFrameDependencyDescriptor(frameNumber), templateDependencyStructure);

		REQUIRE(process(context, keyFrame.get()));
		REQUIRE(process(
		  context,
		  factory
		    .Create(
		      createDependencyDescriptor(true, true, T1Template, ++frameNumber), templateDependencyStructure)
		    .get()));
		REQUIRE(!process(
		  context,
		  factory
		    .Create(
		      createDependencyDescriptor(true, true, T2Template, ++frameNumber), templateDependencyStructure)
		    .get()));
	}

	SECTION("frame numbers and template ids go on when switching to another stream")
	{
		auto params = createSimulcastParams();
		RTC::RTP::Codecs::AV1::EncodingContext context(params);
		std::shared_ptr<TemplateDependencyStructure> receiverTemplateDependencyStructure;

		syncContext(context, /*targetTemporalLayer*/ 2);

		// Stream A.
		auto keyFrameA = factory.Create(
		  createKeyFrameDependencyDescriptor(KeyFrameNumber), templateDependencyStructure);

		REQUIRE(process(context, keyFrameA.get()));

		// The first stream is forwarded as it is.
		REQUIRE(
		  getDependencyDescriptor(keyFrameA.get()) == createKeyFrameDependencyDescriptor(KeyFrameNumber));

		auto dependencyDescriptor =
		  receiveDependencyDescriptor(keyFrameA.get(), receiverTemplateDependencyStructure);

		REQUIRE(dependencyDescriptor->frameNumber == KeyFrameNumber);

		auto frameA = factory.Create(
		  createDependencyDescriptor(true, true, T0Template, KeyFrameNumber + 3),
		  templateDependencyStructure);

		REQUIRE(process(context, frameA.get()));
		REQUIRE(
		  getDependencyDescriptor(frameA.get()) ==
		  createDependencyDescriptor(true, true, T0Template, KeyFrameNumber + 3));

		// Stream B, whose frame numbers are lower than the ones of stream A.
		std::shared_ptr<TemplateDependencyStructure> templateDependencyStructureB;
		const auto keyFrameDependencyDescriptorB = createKeyFrameDependencyDescriptor(100);

		auto keyFrameB = factory.Create(keyFrameDependencyDescriptorB, templateDependencyStructureB);

		syncContext(context, /*targetTemporalLayer*/ 2);

		REQUIRE(process(context, keyFrameB.get()));

		dependencyDescriptor =
		  receiveDependencyDescriptor(keyFrameB.get(), receiverTemplateDependencyStructure);

		REQUIRE(dependencyDescriptor->isKeyFrame);
		REQUIRE(dependencyDescriptor->frameNumber == KeyFrameNumber + 4);
		REQUIRE(dependencyDescriptor->templateId == 0);
		REQUIRE(receiverTemplateDependencyStructure->templateIdOffset == KeyFrameTemplateCount);
		REQUIRE(receiverTemplateDependencyStructure->templateLayers.size() == KeyFrameTemplateCount);

		auto frameB = factory.Create(
		  createDependencyDescriptor(true, true, T2Template, 102), templateDependencyStructureB);

		REQUIRE(process(context, frameB.get()));

		dependencyDescriptor =
		  receiveDependencyDescriptor(frameB.get(), receiverTemplateDependencyStructure);

		REQUIRE(dependencyDescriptor->frameNumber == KeyFrameNumber + 6);
		REQUIRE(dependencyDescriptor->frameDependencyTemplateId == T2Template + KeyFrameTemplateCount);
		REQUIRE(dependencyDescriptor->templateId == T2Template);
		REQUIRE(dependencyDescriptor->temporalLayer == 2);

		const auto rewrittenDependencyDescriptor = getDependencyDescriptor(frameB.get());

		// The original Dependency Descriptor is restored.
		auto encoder = frameB->GetPayloadEncoder();

		REQUIRE(encoder);

		frameB->RestorePayload();

		REQUIRE(
		  getDependencyDescriptor(frameB.get()) ==
		  createDependencyDescriptor(true, true, T2Template, 102));

		// And rewritten again for retransmissions.
		frameB->EncodePayload(encoder.get());

		REQUIRE(getDependencyDescriptor(frameB.get()) == rewrittenDependencyDescriptor);

		frameB->RestorePayload();

		// Going back to stream A, which goes on after stream B.
		auto keyFrameA2 = factory.Create(
		  createKeyFrameDependencyDescriptor(KeyFrameNumber + 10), templateDependencyStructure);

		syncContext(context, /*targetTemporalLayer*/ 2);

		REQUIRE(process(context, keyFrameA2.get()));

		dependencyDescriptor =
		  receiveDependencyDescriptor(keyFrameA2.get(), receiverTemplateDependencyStructure);

		REQUIRE(dependencyDescriptor->frameNumber == KeyFrameNumber + 7);
		REQUIRE(receiverTemplateDependencyStructure->templateIdOffset == 2 * KeyFrameTemplateCount);
	}

	SECTION("the Dependency Descriptor is not rewritten if the receiver does not get it")
	{
		auto params                        = createSimulcastParams();
		params.rewriteDependencyDescriptor = false;
		RTC::RTP::Codecs::AV1::EncodingContext context(params);

		syncContext(context, /*targetTemporalLayer*/ 0);

		auto keyFrameA = factory.Create(
		  createKeyFrameDependencyDescriptor(KeyFrameNumber), templateDependencyStructure);

		REQUIRE(process(context, keyFrameA.get()));
		REQUIRE(
		  getDependencyDescriptor(keyFrameA.get()) == createKeyFrameDependencyDescriptor(KeyFrameNumber));
		REQUIRE(!keyFrameA->GetPayloadEncoder());

		std::shared_ptr<TemplateDependencyStructure> templateDependencyStructureB;

		auto keyFrameB =
		  factory.Create(createKeyFrameDependencyDescriptor(100), templateDependencyStructureB);

		syncContext(context, /*targetTemporalLayer*/ 0);

		REQUIRE(process(context, keyFrameB.get()));
		REQUIRE(getDependencyDescriptor(keyFrameB.get()) == createKeyFrameDependencyDescriptor(100));

		auto frameB = factory.Create(
		  createDependencyDescriptor(true, true, T0Template, 101), templateDependencyStructureB);

		REQUIRE(process(context, frameB.get()));
		REQUIRE(
		  getDependencyDescriptor(frameB.get()) ==
		  createDependencyDescriptor(true, true, T0Template, 101));
		REQUIRE(!frameB->GetPayloadEncoder());

		// Temporal layers are still selected.
		REQUIRE(!process(
		  context,
		  factory
		    .Create(createDependencyDescriptor(true, true, T2Template, 102), templateDependencyStructureB)
		    .get()));
	}

	SECTION("active decode targets are told while temporal layers are dropped")
	{
		auto params = createSimulcastParams();
		RTC::RTP::Codecs::AV1::EncodingContext context(params);
		std::shared_ptr<TemplateDependencyStructure> receiverTemplateDependencyStructure;

		syncContext(context, /*targetTemporalLayer*/ 0);

		uint16_t frameNumber{ KeyFrameNumber };

		const auto createFrame = [&](uint8_t frameDependencyTemplateId)
		{
			return factory.Create(
			  createDependencyDescriptor(true, true, frameDependencyTemplateId, ++frameNumber),
			  templateDependencyStructure);
		};

		auto keyFrame =
		  factory.Create(createKeyFrameDependencyDescriptor(frameNumber), templateDependencyStructure);

		// The structure takes the whole 16 bytes, so 3 bits of active decode targets
		// take one more.
		REQUIRE(keyFrame->GetDependencyDescriptorMaxLength() == KeyFrameDependencyDescriptor.size() + 1);
		REQUIRE(process(context, keyFrame.get()));

		auto dependencyDescriptor =
		  receiveDependencyDescriptor(keyFrame.get(), receiverTemplateDependencyStructure);

		REQUIRE(dependencyDescriptor->isKeyFrame);
		REQUIRE(dependencyDescriptor->activeDecodeTargetsBitmask == 0b001);

		keyFrame->RestorePayload();

		REQUIRE(
		  getDependencyDescriptor(keyFrame.get()) == createKeyFrameDependencyDescriptor(KeyFrameNumber));

		auto frame = createFrame(T0Template);

		// 3 bytes, 5 flag bits and 3 bits of active decode targets.
		REQUIRE(frame->GetDependencyDescriptorMaxLength() == 4);
		REQUIRE(process(context, frame.get()));

		dependencyDescriptor =
		  receiveDependencyDescriptor(frame.get(), receiverTemplateDependencyStructure);

		REQUIRE(dependencyDescriptor->frameNumber == frameNumber);
		REQUIRE(dependencyDescriptor->activeDecodeTargetsBitmask == 0b001);

		frame->RestorePayload();

		REQUIRE(
		  getDependencyDescriptor(frame.get()) ==
		  createDependencyDescriptor(true, true, T0Template, frameNumber));

		// Once all decode targets are forwarded again, the receiver is told so
		// until the next key frame.
		context.SetTargetTemporalLayer(2);

		frame = createFrame(T0Template);

		REQUIRE(process(context, frame.get()));

		dependencyDescriptor =
		  receiveDependencyDescriptor(frame.get(), receiverTemplateDependencyStructure);

		REQUIRE(dependencyDescriptor->activeDecodeTargetsBitmask == 0b111);

		frame = createFrame(T2Template);

		REQUIRE(process(context, frame.get()));

		dependencyDescriptor =
		  receiveDependencyDescriptor(frame.get(), receiverTemplateDependencyStructure);

		REQUIRE(dependencyDescriptor->activeDecodeTargetsBitmask == 0b111);

		keyFrame =
		  factory.Create(createKeyFrameDependencyDescriptor(++frameNumber), templateDependencyStructure);

		REQUIRE(process(context, keyFrame.get()));
		REQUIRE(
		  getDependencyDescriptor(keyFrame.get()) == createKeyFrameDependencyDescriptor(frameNumber));

		frame = createFrame(T2Template);

		REQUIRE(process(context, frame.get()));
		REQUIRE(
		  getDependencyDescriptor(frame.get()) ==
		  createDependencyDescriptor(true, true, T2Template, frameNumber));
	}

	SECTION("the Dependency Descriptor is rewritten without active decode targets if there is no room")
	{
		auto params = createSimulcastParams();
		RTC::RTP::Codecs::AV1::EncodingContext context(params);
		std::shared_ptr<TemplateDependencyStructure> receiverTemplateDependencyStructure;

		syncContext(context, /*targetTemporalLayer*/ 0);

		auto keyFrameA = factory.Create(
		  createKeyFrameDependencyDescriptor(KeyFrameNumber), templateDependencyStructure, /*room*/ 0);

		REQUIRE(process(context, keyFrameA.get()));

		// Switch to stream B.
		std::shared_ptr<TemplateDependencyStructure> templateDependencyStructureB;

		auto keyFrameB = factory.Create(
		  createKeyFrameDependencyDescriptor(100), templateDependencyStructureB, /*room*/ 0);

		syncContext(context, /*targetTemporalLayer*/ 0);

		REQUIRE(process(context, keyFrameB.get()));

		auto dependencyDescriptor =
		  receiveDependencyDescriptor(keyFrameB.get(), receiverTemplateDependencyStructure);

		REQUIRE(dependencyDescriptor->frameNumber == KeyFrameNumber + 1);
		REQUIRE(dependencyDescriptor->activeDecodeTargetsBitmask == 0b111);

		auto frameB = factory.Create(
		  createDependencyDescriptor(true, true, T0Template, 101),
		  templateDependencyStructureB,
		  /*room*/ 0);

		REQUIRE(process(context, frameB.get()));

		dependencyDescriptor =
		  receiveDependencyDescriptor(frameB.get(), receiverTemplateDependencyStructure);

		REQUIRE(dependencyDescriptor->frameNumber == KeyFrameNumber + 2);
		REQUIRE(dependencyDescriptor->templateId == T0Template);
		REQUIRE(!dependencyDescriptor->activeDecodeTargetsBitmask.has_value());
	}
}

SCENARIO("AV1 SimulcastProducerStreamManager", "[rtp][codecs][av1][simulcast][producerstreammanager]")
{
	constexpr uint32_t MappedSsrc0{ 1000 };
	constexpr uint32_t MappedSsrc1{ 2000 };

	PacketFactory factory;

	SECTION("streams are switched on key frames and the receiver sees a single stream")
	{
		RTC::RtpCodecMimeType mimeType;

		mimeType.SetMimeType("video/AV1");

		REQUIRE(
		  RTC::RTP::Codecs::Tools::IsValidTypeForCodec(RTC::RtpParameters::Type::SIMULCAST, mimeType));
		REQUIRE(RTC::RTP::Codecs::Tools::CanBeKeyFrame(mimeType));

		auto params = createSimulcastParams();
		std::unique_ptr<RTC::RTP::Codecs::EncodingContext> encodingContext(
		  RTC::RTP::Codecs::Tools::GetEncodingContext(mimeType, params));

		std::vector<RTC::RtpEncodingParameters> consumableRtpEncodings(2);

		consumableRtpEncodings[0].ssrc = MappedSsrc0;
		consumableRtpEncodings[1].ssrc = MappedSsrc1;

		ProducerStreamManagerListener listener;

		RTC::SimulcastProducerStreamManager manager(
		  consumableRtpEncodings,
		  /*preferredLayers*/ { 1, 2 },
		  std::move(encodingContext),
		  RTC::Media::Kind::VIDEO,
		  /*keyFrameSupported*/ true,
		  std::addressof(listener),
		  std::addressof(shared));

#ifdef MS_USE_BUILTIN_BWE
		manager.SetBweManaged(true);
#endif

		RTC::RTP::RtpStream::Params rtpStreamParams;

		rtpStreamParams.clockRate = 90000;
		rtpStreamParams.ssrc      = MappedSsrc0;

		RTC::RTP::RtpStreamRecv rtpStream0(
		  std::addressof(streamRecvListener), std::addressof(shared), rtpStreamParams, 0, false);

		rtpStreamParams.ssrc = MappedSsrc1;

		RTC::RTP::RtpStreamRecv rtpStream1(
		  std::addressof(streamRecvListener), std::addressof(shared), rtpStreamParams, 0, false);

		manager.ProducerRtpStream(std::addressof(rtpStream0), MappedSsrc0);
		manager.ProducerRtpStream(std::addressof(rtpStream1), MappedSsrc1);

		// Both streams were captured at the same instants with the same RTP
		// timestamps.
		for (auto* rtpStream : { std::addressof(rtpStream0), std::addressof(rtpStream1) })
		{
			const auto ntp = Utils::Time::TimeUsToNtp(1000 * 1000);
			RTC::RTCP::SenderReport sr;

			sr.SetSsrc(rtpStream->GetSsrc());
			sr.SetNtpSec(ntp.seconds);
			sr.SetNtpFrac(ntp.fractions);
			sr.SetRtpTs(0);

			rtpStream->ReceiveRtcpSenderReport(std::addressof(sr), shared.GetTimeUs());
		}

		std::shared_ptr<TemplateDependencyStructure> templateDependencyStructure0;
		std::shared_ptr<TemplateDependencyStructure> templateDependencyStructure1;
		std::shared_ptr<TemplateDependencyStructure> receiverTemplateDependencyStructure;

		const auto processPacket = [&manager](RTC::RTP::Packet* packet, uint32_t ssrc, uint32_t timestamp)
		{
			packet->SetSsrc(ssrc);
			packet->SetTimestamp(timestamp);

			return manager.ProcessRtpPacket(
			  packet, /*lastSentPacketHasMarker*/ true, /*clockRate*/ 90000, /*maxPacketTs*/ 0);
		};

		using ResultType = RTC::ProducerStreamManager::RtpPacketProcessResult::Type;

		manager.UpdateTargetLayers(0, 2);

		// Not a key frame, so it is not forwarded.
		auto frame0 = factory.Create(
		  createDependencyDescriptor(true, true, T0Template, 1000), templateDependencyStructure0);

		REQUIRE(processPacket(frame0.get(), MappedSsrc0, 3000).type == ResultType::BUFFER);

		auto keyFrame0 =
		  factory.Create(createKeyFrameDependencyDescriptor(1001), templateDependencyStructure0);
		auto result = processPacket(keyFrame0.get(), MappedSsrc0, 6000);

		REQUIRE(result.type == ResultType::FORWARD);
		REQUIRE(result.spatialLayerSwitched);
		REQUIRE(manager.GetCurrentSpatialLayer() == 0);
		REQUIRE(manager.GetCurrentTemporalLayer() == 2);
		REQUIRE(
		  receiveDependencyDescriptor(keyFrame0.get(), receiverTemplateDependencyStructure)->frameNumber ==
		  1001);

		keyFrame0->RestorePayload();

		frame0 = factory.Create(
		  createDependencyDescriptor(true, true, T2Template, 1003), templateDependencyStructure0);

		REQUIRE(processPacket(frame0.get(), MappedSsrc0, 9000).type == ResultType::FORWARD);
		REQUIRE(
		  receiveDependencyDescriptor(frame0.get(), receiverTemplateDependencyStructure)->frameNumber ==
		  1003);

		frame0->RestorePayload();

		// Switch to the stream 1, whose frame numbers are lower.
		manager.UpdateTargetLayers(1, 2);

		auto frame1 = factory.Create(
		  createDependencyDescriptor(true, true, T0Template, 10), templateDependencyStructure1);

		REQUIRE(processPacket(frame1.get(), MappedSsrc1, 12000).type == ResultType::BUFFER);

		auto keyFrame1 =
		  factory.Create(createKeyFrameDependencyDescriptor(12), templateDependencyStructure1);

		REQUIRE(keyFrame1->IsKeyFrame());

		result = processPacket(keyFrame1.get(), MappedSsrc1, 15000);

		REQUIRE(result.type == ResultType::FORWARD);
		REQUIRE(result.spatialLayerSwitched);
		REQUIRE(manager.GetCurrentSpatialLayer() == 1);

		auto dependencyDescriptor =
		  receiveDependencyDescriptor(keyFrame1.get(), receiverTemplateDependencyStructure);

		REQUIRE(dependencyDescriptor->isKeyFrame);
		REQUIRE(dependencyDescriptor->frameNumber == 1004);
		REQUIRE(receiverTemplateDependencyStructure->templateIdOffset == KeyFrameTemplateCount);

		keyFrame1->RestorePayload();

		// Packets of the stream 0 are not forwarded anymore.
		frame0 = factory.Create(
		  createDependencyDescriptor(true, true, T0Template, 1005), templateDependencyStructure0);

		REQUIRE(processPacket(frame0.get(), MappedSsrc0, 18000).type == ResultType::SILENT_DROP);

		frame1 = factory.Create(
		  createDependencyDescriptor(true, true, T1Template, 14), templateDependencyStructure1);

		REQUIRE(processPacket(frame1.get(), MappedSsrc1, 18000).type == ResultType::FORWARD);

		dependencyDescriptor =
		  receiveDependencyDescriptor(frame1.get(), receiverTemplateDependencyStructure);

		REQUIRE(dependencyDescriptor->frameNumber == 1006);
		REQUIRE(dependencyDescriptor->templateId == T1Template);
		REQUIRE(dependencyDescriptor->temporalLayer == 1);

		// Lower the temporal layer of the stream 1.
		manager.UpdateTargetLayers(1, 0);

		frame1 = factory.Create(
		  createDependencyDescriptor(true, true, T2Template, 15), templateDependencyStructure1);

		REQUIRE(processPacket(frame1.get(), MappedSsrc1, 21000).type == ResultType::DROP);
		REQUIRE(manager.GetCurrentTemporalLayer() == 0);

		frame1 = factory.Create(
		  createDependencyDescriptor(true, true, T0Template, 16), templateDependencyStructure1);

		REQUIRE(processPacket(frame1.get(), MappedSsrc1, 24000).type == ResultType::FORWARD);

		dependencyDescriptor =
		  receiveDependencyDescriptor(frame1.get(), receiverTemplateDependencyStructure);

		REQUIRE(dependencyDescriptor->frameNumber == 1008);
		REQUIRE(dependencyDescriptor->activeDecodeTargetsBitmask == 0b001);
	}
}
