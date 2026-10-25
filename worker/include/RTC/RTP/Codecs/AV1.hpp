#ifndef MS_RTC_RTP_CODECS_AV1_HPP
#define MS_RTC_RTP_CODECS_AV1_HPP

#include "common.hpp"
#include "RTC/RTP/Codecs/PayloadDescriptorHandler.hpp"
#include "RTC/RTP/Packet.hpp"

namespace RTC
{
	namespace RTP
	{
		namespace Codecs
		{
			class AV1
			{
			public:
				struct PayloadDescriptor : public Codecs::PayloadDescriptor
				{
					struct Encoder : public Codecs::PayloadDescriptor::Encoder
					{
						~Encoder() override = default;
						explicit Encoder(const Codecs::DependencyDescriptor::RewriteParams& rewriteParams)
						  : rewriteParams(rewriteParams)
						{
						}
						void Encode(RTP::Packet* packet, const AV1::PayloadDescriptor* payloadDescriptor) const;

						Codecs::DependencyDescriptor::RewriteParams rewriteParams;
					};

					/**
					 * Payload descriptor of a packet without Dependency Descriptor.
					 */
					explicit PayloadDescriptor(bool isKeyFrame);
					/**
					 * Payload descriptor of a packet with Dependency Descriptor.
					 */
					explicit PayloadDescriptor(std::unique_ptr<Codecs::DependencyDescriptor>& dependencyDescriptor);
					/* Pure virtual methods inherited from Codecs::PayloadDescriptor. */
					~PayloadDescriptor() override = default;

					void Dump(int indentation = 0) const override;
					void Restore(RTP::Packet* packet) const;
					std::unique_ptr<Codecs::PayloadDescriptor::Encoder> GetEncoder() const
					{
						if (this->encoder.has_value())
						{
							return std::make_unique<Encoder>(this->encoder.value());
						}
						else
						{
							return nullptr;
						}
					}

					// Fields in Dependency Descriptor extension.
					bool startOfFrame{ false };
					bool endOfFrame{ false };
					uint16_t frameNumber{ 0 };
					uint8_t spatialLayer{ 0 };
					uint8_t temporalLayer{ 0 };
					std::unique_ptr<Codecs::DependencyDescriptor> dependencyDescriptor{ nullptr };
					// Parsed values.
					bool isKeyFrame{ false };
					std::optional<Encoder> encoder{ std::nullopt };
				};

			public:
				static AV1::PayloadDescriptor* Parse(
				  const uint8_t* data,
				  size_t len,
				  std::unique_ptr<Codecs::DependencyDescriptor>& dependencyDescriptor);
				/**
				 * Whether the given RTP payload starts a key frame, as told by its
				 * aggregation header and, if present, by the header of its frame.
				 *
				 * @see https://aomediacodec.github.io/av1-rtp-spec/#44-av1-aggregation-header
				 */
				static bool IsKeyFrame(const uint8_t* data, size_t len);
				static void ProcessRtpPacket(
				  RTP::Packet* packet,
				  std::shared_ptr<Codecs::DependencyDescriptor::TemplateDependencyStructure>&
				    templateDependencyStructure);

			public:
				class EncodingContext : public Codecs::EncodingContext
				{
				public:
					explicit EncodingContext(Codecs::EncodingContext::Params& params)
					  : Codecs::EncodingContext(params)
					{
					}
					~EncodingContext() override = default;

					/* Pure virtual methods inherited from Codecs::EncodingContext. */
				public:
					void SyncRequired() override
					{
						this->syncRequired = true;
					}

				public:
					bool syncRequired{ false };
					// Rewriting of the Dependency Descriptor so the receiver sees a single
					// stream of frames regardless of how many streams they came from.
					// Added to the frame number of forwarded frames.
					uint16_t frameNumberDelta{ 0 };
					// Added to the template ids of forwarded frames.
					uint8_t templateIdDelta{ 0 };
					// Highest forwarded frame number (once rewritten).
					std::optional<uint16_t> highestFrameNumber;
					// Template id offset (once rewritten) whose templates do not collide
					// with the ones of the last forwarded template dependency structure.
					std::optional<uint8_t> nextTemplateIdOffset;
					// Whether active decode targets have been written since the last
					// forwarded key frame, so they must keep being written.
					bool activeDecodeTargetsWritten{ false };
				};

				class PayloadDescriptorHandler : public Codecs::PayloadDescriptorHandler
				{
				public:
					explicit PayloadDescriptorHandler(PayloadDescriptor* payloadDescriptor);
					~PayloadDescriptorHandler() override = default;

				public:
					void Dump(int indentation = 0) const override
					{
						this->payloadDescriptor->Dump(indentation);
					}
					bool Process(
					  Codecs::EncodingContext* encodingContext, RTP::Packet* packet, bool& marker) override;
					void RtpPacketChanged(RTP::Packet* /*packet*/) override {};
					std::unique_ptr<Codecs::PayloadDescriptor::Encoder> GetEncoder() const override
					{
						return this->payloadDescriptor->GetEncoder();
					}
					void Encode(RTP::Packet* packet, Codecs::PayloadDescriptor::Encoder* encoder) override;
					void Restore(RTP::Packet* packet) override;
					uint8_t GetSpatialLayer() const override
					{
						return this->payloadDescriptor->spatialLayer;
					}
					uint8_t GetTemporalLayer() const override
					{
						return this->payloadDescriptor->temporalLayer;
					}
					bool IsKeyFrame() const override
					{
						return this->payloadDescriptor->isKeyFrame;
					}
					uint8_t GetDependencyDescriptorMaxLength() const override;

				private:
					bool ProcessSvc(AV1::EncodingContext* context, bool& marker) const;
					bool ProcessSimulcast(AV1::EncodingContext* context) const;
					void RewriteDependencyDescriptor(AV1::EncodingContext* context, RTP::Packet* packet);

				private:
					std::unique_ptr<PayloadDescriptor> payloadDescriptor;
				};
			};
		} // namespace Codecs
	} // namespace RTP
} // namespace RTC

#endif
