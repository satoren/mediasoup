#define MS_CLASS "RTC::RTP::Codecs::AV1"
// #define MS_LOG_DEV_LEVEL 3

#include "RTC/RTP/Codecs/AV1.hpp"
#include "Logger.hpp"
#include "RTC/SeqManager.hpp"
#include <utility> // std::cmp_greater()

namespace RTC
{
	namespace RTP
	{
		namespace Codecs
		{
			/* Static. */

			// Maximum number of templates, given that template ids take 6 bits.
			static constexpr uint8_t MaxTemplates{ 64 };

			// OBU types.
			// https://aomediacodec.github.io/av1-spec/#obu-header-semantics
			static constexpr uint8_t ObuFrameHeader{ 3 };
			static constexpr uint8_t ObuFrame{ 6 };

			/**
			 * Reads a leb128 value.
			 *
			 * @returns The number of read bytes, or 0 if it could not be read.
			 *
			 * @see https://aomediacodec.github.io/av1-spec/#leb128
			 */
			static size_t readLeb128(const uint8_t* data, size_t len, size_t& value)
			{
				value = 0;

				for (size_t idx{ 0 }; idx < len && idx < 8; ++idx)
				{
					value |= static_cast<size_t>(data[idx] & 0x7F) << (idx * 7);

					if ((data[idx] & 0x80) == 0)
					{
						return idx + 1;
					}
				}

				return 0;
			}

			/* Class methods. */

			AV1::PayloadDescriptor* AV1::Parse(
			  const uint8_t* data,
			  size_t len,
			  std::unique_ptr<Codecs::DependencyDescriptor>& dependencyDescriptor)
			{
				MS_TRACE();

				if (dependencyDescriptor)
				{
					return new PayloadDescriptor(dependencyDescriptor);
				}

				// Without Dependency Descriptor there is no layer information but key
				// frames can still be told from the payload.
				return new PayloadDescriptor(IsKeyFrame(data, len));
			}

			bool AV1::IsKeyFrame(const uint8_t* data, size_t len)
			{
				MS_TRACE();

				if (len < 2)
				{
					return false;
				}

				// Aggregation header: |Z|Y| W |N|-|-|-|
				const uint8_t aggregationHeader = data[0];
				// Whether the first OBU element continues an OBU of the previous packet.
				const bool z = aggregationHeader & 0x80;
				// Number of OBU elements (0 means that each element has a length field).
				const uint8_t w = (aggregationHeader >> 4) & 0x03;
				// Whether this is the first packet of a coded video sequence, which
				// starts with a key frame.
				const bool n = aggregationHeader & 0x08;

				if (z || !n)
				{
					return false;
				}

				size_t offset{ 1 };

				// Look for the header of the frame, which may not be in this packet.
				for (size_t element{ 1 }; offset < len; ++element)
				{
					size_t obuLen{ 0 };

					// The last element has no length field if the number of elements is
					// given.
					if (w != 0 && element == w)
					{
						obuLen = len - offset;
					}
					else
					{
						const size_t read = readLeb128(data + offset, len - offset, obuLen);

						if (read == 0)
						{
							return false;
						}

						offset += read;

						// The OBU continues in the next packet.
						obuLen = std::min(obuLen, len - offset);
					}

					if (obuLen == 0)
					{
						break;
					}

					const uint8_t* obu      = data + offset;
					const uint8_t obuType   = (obu[0] >> 3) & 0x0F;
					const bool obuExtension = obu[0] & 0x04;
					const bool obuHasSize   = obu[0] & 0x02;
					size_t obuHeaderLen     = obuExtension ? 2 : 1;

					if (obuType == ObuFrameHeader || obuType == ObuFrame)
					{
						if (obuHasSize)
						{
							size_t obuSize;
							const size_t read = obuLen > obuHeaderLen
							                      ? readLeb128(obu + obuHeaderLen, obuLen - obuHeaderLen, obuSize)
							                      : 0;

							if (read == 0)
							{
								return true;
							}

							obuHeaderLen += read;
						}

						// The frame header is in the next packet.
						if (obuLen <= obuHeaderLen)
						{
							return true;
						}

						// Uncompressed header: show_existing_frame f(1), frame_type f(2).
						// https://aomediacodec.github.io/av1-spec/#uncompressed-header-syntax
						const uint8_t header         = obu[obuHeaderLen];
						const bool showExistingFrame = header & 0x80;
						const uint8_t frameType      = (header >> 5) & 0x03;
						static constexpr uint8_t KeyFrame{ 0 };

						return !showExistingFrame && frameType == KeyFrame;
					}

					offset += obuLen;
				}

				return true;
			}

			void AV1::ProcessRtpPacket(
			  RTP::Packet* packet,
			  std::shared_ptr<Codecs::DependencyDescriptor::TemplateDependencyStructure>& templateDependencyStructure)
			{
				MS_TRACE();

				std::unique_ptr<Codecs::DependencyDescriptor> dependencyDescriptor;

				// Read dependency descriptor.
				packet->ReadDependencyDescriptor(dependencyDescriptor, templateDependencyStructure);

				PayloadDescriptor* payloadDescriptor =
				  AV1::Parse(packet->GetPayload(), packet->GetPayloadLength(), dependencyDescriptor);

				if (!payloadDescriptor)
				{
					return;
				}

				auto* payloadDescriptorHandler = new PayloadDescriptorHandler(payloadDescriptor);

				packet->SetPayloadDescriptorHandler(payloadDescriptorHandler);
			}

			/* Instance methods. */

			AV1::PayloadDescriptor::PayloadDescriptor(bool isKeyFrame) : isKeyFrame(isKeyFrame)
			{
				MS_TRACE();
			}

			AV1::PayloadDescriptor::PayloadDescriptor(
			  std::unique_ptr<Codecs::DependencyDescriptor>& dependencyDescriptor)
			{
				MS_TRACE();

				// Read fields.
				this->startOfFrame  = dependencyDescriptor->startOfFrame;
				this->endOfFrame    = dependencyDescriptor->endOfFrame;
				this->frameNumber   = dependencyDescriptor->frameNumber;
				this->spatialLayer  = dependencyDescriptor->spatialLayer;
				this->temporalLayer = dependencyDescriptor->temporalLayer;
				this->isKeyFrame    = dependencyDescriptor->isKeyFrame;

				this->dependencyDescriptor = std::move(dependencyDescriptor);
			}

			void AV1::PayloadDescriptor::Dump(int indentation) const
			{
				MS_TRACE();

				MS_DUMP_CLEAN(indentation, "<AV1::PayloadDescriptor>");
				MS_DUMP_CLEAN(indentation, "  isKeyFrame: %s", this->isKeyFrame ? "true" : "false");
				if (this->dependencyDescriptor)
				{
					this->dependencyDescriptor->Dump(indentation + 1);
				}
				MS_DUMP_CLEAN(indentation, "</AV1::PayloadDescriptor>");
			}

			void AV1::PayloadDescriptor::Restore(RTP::Packet* packet) const
			{
				MS_TRACE();

				if (!this->dependencyDescriptor)
				{
					return;
				}

				// Write back the original Dependency Descriptor.
				packet->UpdateDependencyDescriptor(
				  this->dependencyDescriptor->GetData(), this->dependencyDescriptor->GetLength());
			}

			void AV1::PayloadDescriptor::Encoder::Encode(
			  RTP::Packet* packet, const AV1::PayloadDescriptor* payloadDescriptor) const
			{
				MS_TRACE();

				const auto* dependencyDescriptor = payloadDescriptor->dependencyDescriptor.get();

				if (!dependencyDescriptor)
				{
					return;
				}

				auto rewriteParams = this->rewriteParams;

				// Nothing to rewrite, so just make sure that the packet holds the
				// original one.
				if (
				  rewriteParams.frameNumberDelta == 0 && rewriteParams.templateIdDelta == 0 &&
				  !rewriteParams.activeDecodeTargetsBitmask.has_value())
				{
					payloadDescriptor->Restore(packet);

					return;
				}

				uint8_t buffer[Consts::TwoBytesRtpExtensionMaxLength];
				auto len = dependencyDescriptor->Write(rewriteParams, buffer);

				if (len != 0 && packet->UpdateDependencyDescriptor(buffer, len))
				{
					return;
				}

				// There is no room for the active decode targets, so at least keep the
				// frame numbers and template ids consistent.
				if (rewriteParams.activeDecodeTargetsBitmask.has_value())
				{
					rewriteParams.activeDecodeTargetsBitmask.reset();

					len = dependencyDescriptor->Write(rewriteParams, buffer);

					if (len != 0 && packet->UpdateDependencyDescriptor(buffer, len))
					{
						return;
					}
				}

				MS_WARN_TAG(
				  rtp,
				  "cannot rewrite dependency descriptor [ssrc:%" PRIu32 ", seq:%" PRIu16 "]",
				  packet->GetSsrc(),
				  packet->GetSequenceNumber());
			}

			AV1::PayloadDescriptorHandler::PayloadDescriptorHandler(AV1::PayloadDescriptor* payloadDescriptor)
			{
				MS_TRACE();

				this->payloadDescriptor.reset(payloadDescriptor);
			}

			bool AV1::PayloadDescriptorHandler::Process(
			  Codecs::EncodingContext* encodingContext, RTP::Packet* packet, bool& marker)
			{
				MS_TRACE();

				auto* context = static_cast<Codecs::AV1::EncodingContext*>(encodingContext);

				MS_ASSERT(context->GetTargetTemporalLayer() >= 0, "target temporal layer cannot be -1");

				// Without Dependency Descriptor there is no layer information, so there
				// is nothing to filter or to rewrite.
				if (!this->payloadDescriptor->dependencyDescriptor)
				{
					context->syncRequired = false;

					context->SetCurrentSpatialLayer(0);
					context->SetCurrentTemporalLayer(0);

					this->payloadDescriptor->encoder.reset();

					return true;
				}

				const bool forward =
				  context->IsSimulcast() ? ProcessSimulcast(context) : ProcessSvc(context, marker);

				if (!forward)
				{
					return false;
				}

				if (context->RewritesDependencyDescriptor())
				{
					RewriteDependencyDescriptor(context, packet);
				}
				else
				{
					this->payloadDescriptor->encoder.reset();
				}

				return true;
			}

			uint8_t AV1::PayloadDescriptorHandler::GetDependencyDescriptorMaxLength() const
			{
				MS_TRACE();

				if (!this->payloadDescriptor->dependencyDescriptor)
				{
					return 0;
				}

				return this->payloadDescriptor->dependencyDescriptor->GetMaxRewrittenLength();
			}

			bool AV1::PayloadDescriptorHandler::ProcessSvc(AV1::EncodingContext* context, bool& marker) const
			{
				MS_TRACE();

				MS_ASSERT(context->GetTargetSpatialLayer() >= 0, "target spatial layer cannot be -1");

				auto packetSpatialLayer  = GetSpatialLayer();
				auto packetTemporalLayer = GetTemporalLayer();
				auto tmpSpatialLayer     = context->GetCurrentSpatialLayer();
				auto tmpTemporalLayer    = context->GetCurrentTemporalLayer();

				// If packet spatial or temporal layer is higher than maximum announced
				// one, drop the packet.
				if (packetSpatialLayer >= context->GetSpatialLayers() || packetTemporalLayer >= context->GetTemporalLayers())
				{
					MS_WARN_TAG(
					  rtp, "too high packet layers %" PRIu8 ":%" PRIu8, packetSpatialLayer, packetTemporalLayer);

					return false;
				}

				// Upgrade current spatial layer if needed.
				if (context->GetTargetSpatialLayer() > context->GetCurrentSpatialLayer())
				{
					if (this->payloadDescriptor->isKeyFrame)
					{
						MS_DEBUG_DEV(
						  "upgrading tmpSpatialLayer from %" PRIu16 " to %" PRIu16 " (packet:%" PRIu8 ":%" PRIu8
						  ")",
						  context->GetCurrentSpatialLayer(),
						  context->GetTargetSpatialLayer(),
						  packetSpatialLayer,
						  packetTemporalLayer);

						tmpSpatialLayer  = context->GetTargetSpatialLayer();
						tmpTemporalLayer = 0; // Just in case.
					}
				}
				// Downgrade current spatial layer if needed.
				else if (context->GetTargetSpatialLayer() < context->GetCurrentSpatialLayer())
				{
					if (packetSpatialLayer == context->GetTargetSpatialLayer() && this->payloadDescriptor->endOfFrame)
					{
						MS_DEBUG_DEV(
						  "downgrading tmpSpatialLayer from %" PRIu16 " to %" PRIu16 " (packet:%" PRIu8
						  ":%" PRIu8 ") without keyframe (full SVC)",
						  context->GetCurrentSpatialLayer(),
						  context->GetTargetSpatialLayer(),
						  packetSpatialLayer,
						  packetTemporalLayer);

						tmpSpatialLayer  = context->GetTargetSpatialLayer();
						tmpTemporalLayer = 0; // Just in case.
					}
				}

				// Filter spatial layers higher than current one.
				if (packetSpatialLayer > tmpSpatialLayer)
				{
					return false;
				}

				// Upgrade current temporal layer if needed.
				if (context->GetTargetTemporalLayer() > context->GetCurrentTemporalLayer())
				{
					if (packetTemporalLayer > context->GetCurrentTemporalLayer())
					{
						MS_DEBUG_DEV(
						  "upgrading tmpTemporalLayer from %" PRIu16 " to %" PRIu8 " (packet:%" PRIu8 ":%" PRIu8
						  ")",
						  context->GetCurrentTemporalLayer(),
						  packetTemporalLayer,
						  packetSpatialLayer,
						  packetTemporalLayer);

						tmpTemporalLayer = packetTemporalLayer;
					}
				}
				// Downgrade current temporal layer if needed.
				else if (context->GetTargetTemporalLayer() < context->GetCurrentTemporalLayer())
				{
					if (packetTemporalLayer == context->GetTargetTemporalLayer() && this->payloadDescriptor->endOfFrame)
					{
						MS_DEBUG_DEV(
						  "downgrading tmpTemporalLayer from %" PRIu16 " to %" PRIu16 " (packet:%" PRIu8
						  ":%" PRIu8 ")",
						  context->GetCurrentTemporalLayer(),
						  context->GetTargetTemporalLayer(),
						  packetSpatialLayer,
						  packetTemporalLayer);

						tmpTemporalLayer = packetTemporalLayer;
					}
				}

				// Filter temporal layers higher than current one.
				if (packetTemporalLayer > tmpTemporalLayer)
				{
					return false;
				}

				// Set marker bit if needed.
				if (packetSpatialLayer == tmpSpatialLayer && this->payloadDescriptor->endOfFrame)
				{
					marker = true;
				}

				// Update current spatial layer if needed.
				if (tmpSpatialLayer != context->GetCurrentSpatialLayer())
				{
					context->SetCurrentSpatialLayer(tmpSpatialLayer);
				}

				// Update current temporal layer if needed.
				if (tmpTemporalLayer != context->GetCurrentTemporalLayer())
				{
					context->SetCurrentTemporalLayer(tmpTemporalLayer);
				}

				return true;
			}

			bool AV1::PayloadDescriptorHandler::ProcessSimulcast(AV1::EncodingContext* context) const
			{
				MS_TRACE();

				// Each simulcast stream is a separate encoding whose spatial layers (if
				// any) are all forwarded, so only temporal layers are selected here.
				const auto* dependencyDescriptor = this->payloadDescriptor->dependencyDescriptor.get();
				const auto maxSpatialLayer =
				  static_cast<uint8_t>(dependencyDescriptor->templateDependencyStructure->spatialLayers);
				auto packetTemporalLayer = GetTemporalLayer();
				auto targetTemporalLayer = context->GetTargetTemporalLayer();
				int16_t tmpTemporalLayer = context->GetCurrentTemporalLayer();

				// If packet temporal layer is higher than maximum announced one, drop the
				// packet.
				if (packetTemporalLayer >= context->GetTemporalLayers())
				{
					MS_WARN_TAG(rtp, "too high packet temporal layer %" PRIu8, packetTemporalLayer);

					return false;
				}

				// A key frame is a switching point for every decode target.
				if (this->payloadDescriptor->isKeyFrame)
				{
					tmpTemporalLayer = targetTemporalLayer;
				}
				// Upgrade current temporal layer once a frame allows to switch to a
				// higher one, so its decode target can be decoded from it on.
				else if (targetTemporalLayer > tmpTemporalLayer && this->payloadDescriptor->startOfFrame)
				{
					for (auto temporalLayer = targetTemporalLayer; temporalLayer > tmpTemporalLayer;
					     --temporalLayer)
					{
						const auto decodeTarget = dependencyDescriptor->GetDecodeTarget(
						  maxSpatialLayer, static_cast<uint8_t>(temporalLayer));

						if (
						  decodeTarget.has_value() &&
						  dependencyDescriptor->GetDecodeTargetIndication(decodeTarget.value()) ==
						    Codecs::DependencyDescriptor::DecodeTargetIndication::SWITCH)
						{
							MS_DEBUG_DEV(
							  "upgrading tmpTemporalLayer from %" PRIi16 " to %" PRIi16 " (packet:%" PRIu8 ")",
							  tmpTemporalLayer,
							  temporalLayer,
							  packetTemporalLayer);

							tmpTemporalLayer = temporalLayer;

							break;
						}
					}
				}
				// Downgrade current temporal layer once a new frame starts, so no frame is
				// partially forwarded.
				else if (targetTemporalLayer < tmpTemporalLayer && this->payloadDescriptor->startOfFrame)
				{
					MS_DEBUG_DEV(
					  "downgrading tmpTemporalLayer from %" PRIi16 " to %" PRIi16 " (packet:%" PRIu8 ")",
					  tmpTemporalLayer,
					  targetTemporalLayer,
					  packetTemporalLayer);

					tmpTemporalLayer = targetTemporalLayer;
				}

				// Each simulcast stream has a single spatial layer for the receiver.
				context->SetCurrentSpatialLayer(0);

				// Update current temporal layer if needed.
				if (tmpTemporalLayer != context->GetCurrentTemporalLayer())
				{
					context->SetCurrentTemporalLayer(tmpTemporalLayer);
				}

				// Filter temporal layers higher than current one.
				if (std::cmp_greater(packetTemporalLayer, tmpTemporalLayer))
				{
					return false;
				}

				return true;
			}

			void AV1::PayloadDescriptorHandler::RewriteDependencyDescriptor(
			  AV1::EncodingContext* context, RTP::Packet* packet)
			{
				MS_TRACE();

				const auto* dependencyDescriptor = this->payloadDescriptor->dependencyDescriptor.get();
				const auto& templateDependencyStructure = dependencyDescriptor->templateDependencyStructure;

				// The first packet after a sync (a key frame when switching to another
				// stream) may come from a stream whose frame numbers and template ids are
				// unrelated to the ones forwarded so far. Make its frames follow the last
				// forwarded one, since the receiver drops a key frame whose frame number
				// is lower than the one of the last key frame, and make its templates not
				// collide with the previous ones, so packets that arrive before the new
				// template dependency structure cannot be taken for the old ones.
				if (context->syncRequired)
				{
					context->syncRequired = false;

					if (context->highestFrameNumber.has_value())
					{
						context->frameNumberDelta = static_cast<uint16_t>(
						  context->highestFrameNumber.value() + 1 - dependencyDescriptor->frameNumber);
					}

					if (dependencyDescriptor->isKeyFrame && context->nextTemplateIdOffset.has_value())
					{
						context->templateIdDelta = static_cast<uint8_t>(
						  (context->nextTemplateIdOffset.value() + MaxTemplates -
							 templateDependencyStructure->templateIdOffset) %
						  MaxTemplates);
					}
				}

				Codecs::DependencyDescriptor::RewriteParams rewriteParams;

				rewriteParams.frameNumberDelta = context->frameNumberDelta;
				rewriteParams.templateIdDelta  = context->templateIdDelta;

				const auto frameNumber =
				  static_cast<uint16_t>(dependencyDescriptor->frameNumber + context->frameNumberDelta);

				if (
				  !context->highestFrameNumber.has_value() ||
				  RTC::SeqManager<uint16_t>::IsSeqHigherThan(frameNumber, context->highestFrameNumber.value()))
				{
					context->highestFrameNumber = frameNumber;
				}

				if (dependencyDescriptor->isKeyFrame)
				{
					context->nextTemplateIdOffset = static_cast<uint8_t>(
					  (templateDependencyStructure->templateIdOffset + context->templateIdDelta +
						 templateDependencyStructure->templateLayers.size()) %
					  MaxTemplates);

					// A key frame makes all decode targets active for the receiver.
					context->activeDecodeTargetsWritten = false;
				}

				// Tell the receiver which decode targets are being forwarded, which is
				// needed once some of them are not, and from then on until the next key
				// frame, since the last told ones apply until told otherwise.
				const auto maxSpatialLayer =
				  context->IsSimulcast() ? static_cast<uint8_t>(templateDependencyStructure->spatialLayers)
					                       : static_cast<uint8_t>(context->GetCurrentSpatialLayer());
				const auto maxTemporalLayer = static_cast<uint8_t>(context->GetCurrentTemporalLayer());
				const auto senderActiveDecodeTargetsBitmask =
				  templateDependencyStructure->activeDecodeTargetsBitmask;
				const auto activeDecodeTargetsBitmask =
				  dependencyDescriptor->GetDecodeTargetsBitmask(maxSpatialLayer, maxTemporalLayer) &
				  senderActiveDecodeTargetsBitmask;

				if (activeDecodeTargetsBitmask != senderActiveDecodeTargetsBitmask || context->activeDecodeTargetsWritten)
				{
					rewriteParams.activeDecodeTargetsBitmask = activeDecodeTargetsBitmask;

					context->activeDecodeTargetsWritten = true;
				}

				// Store the rewriting for retransmissions.
				this->payloadDescriptor->encoder.emplace(rewriteParams);
				this->payloadDescriptor->encoder->Encode(packet, this->payloadDescriptor.get());
			}

			void AV1::PayloadDescriptorHandler::Encode(
			  RTP::Packet* packet, Codecs::PayloadDescriptor::Encoder* encoder)
			{
				MS_TRACE();

				auto* av1Encoder = static_cast<AV1::PayloadDescriptor::Encoder*>(encoder);

				av1Encoder->Encode(packet, this->payloadDescriptor.get());
			}

			void AV1::PayloadDescriptorHandler::Restore(RTP::Packet* packet)
			{
				MS_TRACE();

				this->payloadDescriptor->Restore(packet);
			}
		} // namespace Codecs
	} // namespace RTP
} // namespace RTC
