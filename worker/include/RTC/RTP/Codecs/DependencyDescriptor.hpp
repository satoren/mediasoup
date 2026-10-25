#ifndef MS_RTC_RTP_CODECS_DEPENDENCY_DESCRIPTOR_HPP
#define MS_RTC_RTP_CODECS_DEPENDENCY_DESCRIPTOR_HPP

#include "common.hpp"
#include "Utils.hpp" // BitStream.
#include <ankerl/unordered_dense.h>
#include <memory>
#include <optional>
#include <vector>

namespace RTC
{
	namespace RTP
	{
		namespace Codecs
		{
			/**
			 * Dependency Descriptor RTP header extension.
			 *
			 * @see https://aomediacodec.github.io/av1-rtp-spec/#dependency-descriptor-rtp-header-extension
			 */
			struct DependencyDescriptor
			{
			public:
				enum class DecodeTargetIndication : uint8_t
				{
					NOT_PRESENT = 0,
					DISCARDABLE = 1,
					SWITCH      = 2,
					REQUIRED    = 3
				};

			private:
				static ankerl::unordered_dense::map<DecodeTargetIndication, std::string> dtiToString;

			private:
				struct FrameDependencyTemplate
				{
					FrameDependencyTemplate(uint32_t spatialLayer, uint32_t temporalLayer)
					  : spatialLayer(spatialLayer), temporalLayer(temporalLayer)
					{
					}

					uint32_t spatialLayer;
					uint32_t temporalLayer;
					std::vector<DecodeTargetIndication> decodeTargetIndications;
					std::vector<uint8_t> frameDiffs;
					std::vector<uint8_t> frameDiffChains;
				};

			public:
				struct DecodeTargetLayer
				{
					uint8_t spatialLayer{ 0 };
					uint8_t temporalLayer{ 0 };
				};

				/**
				 * Template dependency structure as attached to the first packet of a key
				 * frame.
				 *
				 * @remarks
				 * - Once parsed, a structure is never modified (but for the active decode
				 *   targets told by the sender) and a new one is created for every attached
				 *   one, so descriptors parsed with it keep being valid after a new one
				 *   arrives.
				 */
				struct TemplateDependencyStructure
				{
					// Highest spatial layer id.
					uint32_t spatialLayers{ 0 };
					// Highest temporal layer id.
					uint32_t temporalLayers{ 0 };
					uint8_t templateIdOffset{ 0 };
					uint8_t decodeTargetCount{ 0 };
					uint8_t chainCount{ 0 };
					std::vector<FrameDependencyTemplate> templateLayers;
					std::vector<uint8_t> decodeTargetProtectedBy;
					// Spatial and temporal layer of each decode target.
					std::vector<DecodeTargetLayer> decodeTargetLayers;
					// Active decode targets as last told by the sender.
					uint32_t activeDecodeTargetsBitmask{ 0 };
				};

				/**
				 * Changes to apply to the descriptor when writing it.
				 */
				struct RewriteParams
				{
					// Added to the frame number.
					uint16_t frameNumberDelta{ 0 };
					// Added to the frame dependency template id and to the template id
					// offset of the attached structure (modulo 64).
					uint8_t templateIdDelta{ 0 };
					// If set, active decode targets bitmask to write.
					std::optional<uint32_t> activeDecodeTargetsBitmask{ std::nullopt };
				};

			public:
				static DependencyDescriptor* Parse(
				  const uint8_t* data,
				  size_t len,
				  std::shared_ptr<TemplateDependencyStructure>& templateDependencyStructure);

			private:
				DependencyDescriptor(
				  const uint8_t* data,
				  size_t len,
				  std::shared_ptr<TemplateDependencyStructure> templateDependencyStructure);

			public:
				void Dump(int indentation = 0) const;
				/**
				 * Original bytes of the descriptor.
				 */
				const uint8_t* GetData() const
				{
					return this->bitStream.GetData();
				}
				/**
				 * Original length of the descriptor.
				 */
				uint8_t GetLength() const
				{
					return this->length;
				}
				DecodeTargetIndication GetDecodeTargetIndication(uint8_t decodeTarget) const;
				/**
				 * Decode target of the given layers, if any.
				 */
				std::optional<uint8_t> GetDecodeTarget(uint8_t spatialLayer, uint8_t temporalLayer) const;
				/**
				 * Bitmask with the decode targets whose layers are not higher than the
				 * given ones.
				 */
				uint32_t GetDecodeTargetsBitmask(uint8_t maxSpatialLayer, uint8_t maxTemporalLayer) const;
				/**
				 * Length of the descriptor once written with an active decode targets
				 * bitmask.
				 */
				uint8_t GetMaxRewrittenLength() const;
				/**
				 * Write the descriptor with the given changes into the given buffer, which
				 * must be at least `Consts::TwoBytesRtpExtensionMaxLength` bytes long.
				 *
				 * @returns The written length, or 0 if it does not fit.
				 */
				uint8_t Write(const RewriteParams& params, uint8_t* buffer) const;

			private:
				uint32_t GetRewrittenBits(bool withActiveDecodeTargets) const;
				bool ReadMandatoryDescriptorFields();
				bool ReadExtendedDescriptorFields();
				bool ReadTemplateDependencyStructure();
				bool ReadTemplateLayers();
				bool ReadTemplateDecodeTargetIndications();
				bool ReadTemplateFrameDiffs();
				bool ReadTemplateFrameDiffChains();
				bool ReadRenderResolutions();
				void ComputeDecodeTargetLayers();
				bool ReadFrameDependencyDefinition();
				bool ReadFrameDecodeTargetIndications();
				bool ReadFrameDiffs();
				bool ReadFrameDiffChains();

			public:
				bool startOfFrame{ false };
				bool endOfFrame{ false };
				uint8_t frameDependencyTemplateId{ 0 };
				uint16_t frameNumber{ 0 };
				// Index of the template of this frame in the template dependency structure.
				uint8_t templateId{ 0 };
				std::shared_ptr<TemplateDependencyStructure> templateDependencyStructure;
				// Active decode targets bitmask present in the descriptor or implied by
				// the attached template dependency structure.
				std::optional<uint32_t> activeDecodeTargetsBitmask{ std::nullopt };
				// Calculated.
				uint8_t temporalLayer{ 0 };
				uint8_t spatialLayer{ 0 };
				// Whether the frame is a key frame. Set to true if the descriptor contains
				// a template dependency structure.
				bool isKeyFrame{ false };

			private:
				Utils::BitStream bitStream;
				uint8_t length{ 0 };
				// Flags in the extended descriptor fields.
				bool extendedDescriptorFieldsPresent{ false };
				bool activeDecodeTargetsPresent{ false };
				bool customDecodeTargetIndicationsPresent{ false };
				bool customFrameDiffsPresent{ false };
				bool customChainsPresent{ false };
				// Bit offsets in the descriptor.
				uint32_t templateDependencyStructureEndOffset{ 0 };
				uint32_t frameDependencyDefinitionOffset{ 0 };
				uint32_t frameDependencyDefinitionEndOffset{ 0 };
				// Custom decode target indications of this frame.
				std::vector<DecodeTargetIndication> customDecodeTargetIndications;
			};
		} // namespace Codecs
	} // namespace RTP
} // namespace RTC

#endif
