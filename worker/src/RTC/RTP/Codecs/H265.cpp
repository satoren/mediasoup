#define MS_CLASS "RTC::RTP::Codecs::H265"
// #define MS_LOG_DEV_LEVEL 3

#include "RTC/RTP/Codecs/H265.hpp"
#include "Logger.hpp"
#include "Utils.hpp"

namespace RTC
{
	namespace RTP
	{
		namespace Codecs
		{
			/* Static. */

			// Size of the NAL unit header and the payload header (RFC 7798 section 1.1.4).
			static constexpr size_t NalUnitHeaderSize{ 2 };
			// Size of the FU header (RFC 7798 section 4.4.3).
			static constexpr size_t FuHeaderSize{ 1 };
			// Size of the NALU size field in Aggregation Packets (RFC 7798 section 4.4.2).
			static constexpr size_t AggregationNaluSizeSize{ 2 };

			// NAL unit types (ITU-T H.265 table 7-1 and RFC 7798 section 4.4).
			enum NalUnitType : uint8_t
			{
				BLA_W_LP   = 16,
				BLA_W_RADL = 17,
				BLA_N_LP   = 18,
				IDR_W_RADL = 19,
				IDR_N_LP   = 20,
				CRA_NUT    = 21,
				VPS_NUT    = 32,
				SPS_NUT    = 33,
				AP         = 48,
				FU         = 49,
				PACI       = 50
			};

			static inline uint8_t GetNalUnitType(const uint8_t* nalUnitHeader)
			{
				return (nalUnitHeader[0] >> 1) & 0x3F;
			}

			static inline bool IsKeyFrameNalUnitType(uint8_t nalUnitType)
			{
				switch (nalUnitType)
				{
					// IRAP (intra random access point) pictures.
					case NalUnitType::BLA_W_LP:
					case NalUnitType::BLA_W_RADL:
					case NalUnitType::BLA_N_LP:
					case NalUnitType::IDR_W_RADL:
					case NalUnitType::IDR_N_LP:
					case NalUnitType::CRA_NUT:
					// Parameter sets that precede an IRAP picture.
					case NalUnitType::VPS_NUT:
					case NalUnitType::SPS_NUT:
					{
						return true;
					}

					default:
					{
						return false;
					}
				}
			}

			/* Class methods. */

			H265::PayloadDescriptor* H265::Parse(
			  const uint8_t* data, size_t len, Codecs::DependencyDescriptor* dependencyDescriptor)
			{
				MS_TRACE();

				std::unique_ptr<PayloadDescriptor> payloadDescriptor(new PayloadDescriptor());

				if (dependencyDescriptor)
				{
					// Read fields.
					payloadDescriptor->startOfFrame  = dependencyDescriptor->startOfFrame;
					payloadDescriptor->endOfFrame    = dependencyDescriptor->endOfFrame;
					payloadDescriptor->spatialLayer  = dependencyDescriptor->spatialLayer;
					payloadDescriptor->temporalLayer = dependencyDescriptor->temporalLayer;

					payloadDescriptor->isKeyFrame = dependencyDescriptor->isKeyFrame;
				}
				else
				{
					// The payload header carries nuh_temporal_id_plus1 in its 3 lowest
					// bits. For Aggregation Packets it is the lowest value among the
					// aggregated NAL units, and for Fragmentation Units it is the value of
					// the fragmented NAL unit (RFC 7798 sections 4.4.2 and 4.4.3).
					if (len >= NalUnitHeaderSize)
					{
						const uint8_t temporalIdPlusOne = data[1] & 0x07;

						// A value of 0 is not allowed.
						if (temporalIdPlusOne > 0)
						{
							payloadDescriptor->temporalLayer = temporalIdPlusOne - 1;
						}
					}

					payloadDescriptor->isKeyFrame = IsKeyFrame(data, len);
				}

				return payloadDescriptor.release();
			}

			bool H265::IsKeyFrame(const uint8_t* data, size_t len)
			{
				MS_TRACE();

				if (len < NalUnitHeaderSize)
				{
					MS_WARN_DEV("ignoring payload with length < %zu", NalUnitHeaderSize);

					return false;
				}

				const uint8_t nalUnitType = GetNalUnitType(data);

				switch (nalUnitType)
				{
					// Aggregation Packet.
					case NalUnitType::AP:
					{
						size_t offset{ NalUnitHeaderSize };

						len -= NalUnitHeaderSize;

						// Iterate NAL units.
						while (len >= AggregationNaluSizeSize + NalUnitHeaderSize)
						{
							const auto naluSize = Utils::Byte::Get2Bytes(data, offset);

							if (IsKeyFrameNalUnitType(GetNalUnitType(data + offset + AggregationNaluSizeSize)))
							{
								return true;
							}

							// Check if there is room for the indicated NAL unit size.
							if (len < (naluSize + AggregationNaluSizeSize))
							{
								break;
							}

							offset += naluSize + AggregationNaluSizeSize;
							len -= naluSize + AggregationNaluSizeSize;
						}

						break;
					}

					// Fragmentation Unit.
					case NalUnitType::FU:
					{
						if (len < NalUnitHeaderSize + FuHeaderSize)
						{
							break;
						}

						const uint8_t fuHeader = data[NalUnitHeaderSize];
						const bool startBit    = fuHeader & 0x80;
						const uint8_t fuType   = fuHeader & 0x3F;

						if (startBit && IsKeyFrameNalUnitType(fuType))
						{
							return true;
						}

						break;
					}

					// PACI packets are not supported (neither libwebrtc does).
					case NalUnitType::PACI:
					{
						break;
					}

					// Single NAL unit packet.
					default:
					{
						return IsKeyFrameNalUnitType(nalUnitType);
					}
				}

				return false;
			}

			void H265::ProcessRtpPacket(
			  RTP::Packet* packet,
			  std::unique_ptr<Codecs::DependencyDescriptor::TemplateDependencyStructure>& templateDependencyStructure)
			{
				MS_TRACE();

				auto* data = packet->GetPayload();
				auto len   = packet->GetPayloadLength();
				std::unique_ptr<Codecs::DependencyDescriptor> dependencyDescriptor;

				// Read dependency descriptor.
				packet->ReadDependencyDescriptor(dependencyDescriptor, templateDependencyStructure);

				PayloadDescriptor* payloadDescriptor = H265::Parse(data, len, dependencyDescriptor.get());

				if (!payloadDescriptor)
				{
					return;
				}

				auto* payloadDescriptorHandler = new PayloadDescriptorHandler(payloadDescriptor);

				packet->SetPayloadDescriptorHandler(payloadDescriptorHandler);
			}

			/* Instance methods. */

			void H265::PayloadDescriptor::Dump(int indentation) const
			{
				MS_TRACE();

				MS_DUMP_CLEAN(indentation, "<H265::PayloadDescriptor>");
				MS_DUMP_CLEAN(
				  indentation,
				  "  startOfFrame:%" PRIu8 "|endOfFrame:%" PRIu8,
				  this->startOfFrame,
				  this->endOfFrame);
				MS_DUMP_CLEAN(indentation, "  spatialLayer:%" PRIu8, this->spatialLayer);
				MS_DUMP_CLEAN(indentation, "  temporalLayer:%" PRIu8, this->temporalLayer);
				MS_DUMP_CLEAN(indentation, "  isKeyFrame: %s", this->isKeyFrame ? "true" : "false");
				MS_DUMP_CLEAN(indentation, "</H265::PayloadDescriptor>");
			}

			H265::PayloadDescriptorHandler::PayloadDescriptorHandler(H265::PayloadDescriptor* payloadDescriptor)
			{
				MS_TRACE();

				this->payloadDescriptor.reset(payloadDescriptor);
			}

			bool H265::PayloadDescriptorHandler::Process(
			  Codecs::EncodingContext* encodingContext, RTP::Packet* /*packet*/, bool& /*marker*/)
			{
				MS_TRACE();

				auto* context = static_cast<Codecs::H265::EncodingContext*>(encodingContext);

				MS_ASSERT(context->GetTargetTemporalLayer() >= 0, "target temporal layer cannot be -1");

				if (this->payloadDescriptor->temporalLayer > context->GetTargetTemporalLayer())
				{
					return false;
				}

				// H265 always has a single spatial layer (0).
				context->SetCurrentSpatialLayer(0);

				// Update/fix current temporal layer.
				if (this->payloadDescriptor->temporalLayer > context->GetCurrentTemporalLayer())
				{
					context->SetCurrentTemporalLayer(this->payloadDescriptor->temporalLayer);
				}

				if (context->GetCurrentTemporalLayer() > context->GetTargetTemporalLayer())
				{
					context->SetCurrentTemporalLayer(context->GetTargetTemporalLayer());
				}

				return true;
			}
		} // namespace Codecs
	} // namespace RTP
} // namespace RTC
