#define MS_CLASS "RTC::Codecs::DependencyDescriptor"
// #define MS_LOG_DEV_LEVEL 3

#include "RTC/RTP/Codecs/DependencyDescriptor.hpp"
#include "Logger.hpp"
#include <bitset>
#include <cstring> // std::memset()
#include <string>
#include <vector>

namespace RTC
{
	namespace RTP
	{
		namespace Codecs
		{
			/* Static. */

			// Maximum number of templates, given that template ids take 6 bits.
			static constexpr size_t MaxTemplates{ 64 };
			// Bit offset of the template dependency structure (after the mandatory
			// descriptor fields and the flags of the extended descriptor fields).
			static constexpr uint32_t TemplateDependencyStructureOffset{ 29 };
			// Bit offset of the content of the template dependency structure that
			// follows its template id offset.
			static constexpr uint32_t TemplateDependencyStructureBodyOffset{
				TemplateDependencyStructureOffset + 6
			};

			static uint8_t getBitAt(const uint8_t* data, uint32_t offset)
			{
				return (data[offset >> 3] >> (7 - (offset & 0x7))) & 0x1;
			}

			static uint32_t getAllDecodeTargetsBitmask(uint8_t decodeTargetCount)
			{
				return static_cast<uint32_t>((uint64_t{ 1 } << decodeTargetCount) - 1);
			}

			/**
			 * Writes bits into a buffer that has been zeroed.
			 */
			class BitWriter
			{
			public:
				explicit BitWriter(uint8_t* data) : data(data)
				{
				}

			public:
				uint32_t GetOffset() const
				{
					return this->offset;
				}
				void PutBit(uint8_t bit)
				{
					if (bit)
					{
						this->data[this->offset >> 3] |= (1u << (7 - (this->offset & 0x7)));
					}

					++this->offset;
				}
				void PutBits(uint32_t count, uint32_t bits)
				{
					for (uint32_t i{ 0 }; i < count; ++i)
					{
						PutBit((bits >> (count - i - 1)) & 0x1);
					}
				}
				void CopyBits(const uint8_t* src, uint32_t from, uint32_t to)
				{
					for (uint32_t i{ from }; i < to; ++i)
					{
						PutBit(getBitAt(src, i));
					}
				}

			private:
				uint8_t* data;
				uint32_t offset{ 0 };
			};

			// clang-format off
			ankerl::unordered_dense::map<DependencyDescriptor::DecodeTargetIndication, std::string> DependencyDescriptor::dtiToString =
			{
				{ DependencyDescriptor::DecodeTargetIndication::NOT_PRESENT, "-" },
				{ DependencyDescriptor::DecodeTargetIndication::DISCARDABLE, "D" },
				{ DependencyDescriptor::DecodeTargetIndication::SWITCH,      "S" },
				{ DependencyDescriptor::DecodeTargetIndication::REQUIRED,    "R" },
			};
			// clang-format on

			/* Class methods. */

			DependencyDescriptor* DependencyDescriptor::Parse(
			  const uint8_t* data,
			  size_t len,
			  std::shared_ptr<TemplateDependencyStructure>& templateDependencyStructure)
			{
				MS_TRACE();

				if (len < 3)
				{
					MS_WARN_DEV("ignoring payload with length < 3");

					return nullptr;
				}

				if (len > Consts::TwoBytesRtpExtensionMaxLength)
				{
					MS_WARN_DEV("ignoring payload with length > %" PRIu8, Consts::TwoBytesRtpExtensionMaxLength);

					return nullptr;
				}

				std::unique_ptr<DependencyDescriptor> dependencyDescriptor(
				  new DependencyDescriptor(data, len, templateDependencyStructure));

				if (!dependencyDescriptor->ReadMandatoryDescriptorFields())
				{
					MS_WARN_DEV("failed to read mandatory fields");

					return nullptr;
				}

				if (len > 3)
				{
					if (!dependencyDescriptor->ReadExtendedDescriptorFields())
					{
						MS_WARN_DEV("failed to read extended fields");

						return nullptr;
					}
				}

				if (!dependencyDescriptor->ReadFrameDependencyDefinition())
				{
					MS_WARN_DEV("failed to read frame dependency definition");

					return nullptr;
				}

				// The attached template dependency structure applies to the next frames.
				if (dependencyDescriptor->isKeyFrame)
				{
					templateDependencyStructure = dependencyDescriptor->templateDependencyStructure;
				}

				// Keep the active decode targets told by the sender.
				if (
				  dependencyDescriptor->activeDecodeTargetsPresent &&
				  dependencyDescriptor->activeDecodeTargetsBitmask.has_value())
				{
					dependencyDescriptor->templateDependencyStructure->activeDecodeTargetsBitmask =
					  dependencyDescriptor->activeDecodeTargetsBitmask.value();
				}

				return dependencyDescriptor.release();
			}

			/* Instance methods. */

			DependencyDescriptor::DependencyDescriptor(
			  const uint8_t* data,
			  size_t len,
			  std::shared_ptr<TemplateDependencyStructure> templateDependencyStructure)
			  : templateDependencyStructure(std::move(templateDependencyStructure)),
			    bitStream(const_cast<uint8_t*>(data), len),
			    length(static_cast<uint8_t>(len))
			{
				MS_TRACE();
			}

			void DependencyDescriptor::Dump(int indentation) const
			{
				MS_TRACE();

				MS_DUMP_CLEAN(indentation, "<DependencyDescriptor>");
				MS_DUMP_CLEAN(indentation, "  startOfFrame: %s", this->startOfFrame ? "true" : "false");
				MS_DUMP_CLEAN(indentation, "  endOfFrame: %s", this->endOfFrame ? "true" : "false");
				MS_DUMP_CLEAN(indentation, "  frameDependencyTemplateId: %u", this->frameDependencyTemplateId);
				MS_DUMP_CLEAN(indentation, "  frameNumber: %u", this->frameNumber);
				MS_DUMP_CLEAN(indentation, "  templateId: %u", this->templateId);
				MS_DUMP_CLEAN(indentation, "  spatialLayer: %u", this->spatialLayer);
				MS_DUMP_CLEAN(indentation, "  temporalLayer: %u", this->temporalLayer);

				if (this->isKeyFrame)
				{
					MS_DUMP_CLEAN(indentation + 1, "<TemplateDependencyStructure>");
					MS_DUMP_CLEAN(
					  indentation + 1, "  spatialLayers: %u", this->templateDependencyStructure->spatialLayers);
					MS_DUMP_CLEAN(
					  indentation + 1, "  temporalLayers: %u", this->templateDependencyStructure->temporalLayers);
					MS_DUMP_CLEAN(
					  indentation + 1,
					  "  templateIdOffset: %u",
					  this->templateDependencyStructure->templateIdOffset);
					MS_DUMP_CLEAN(
					  indentation + 1,
					  "  decodeTargetCount: %u",
					  this->templateDependencyStructure->decodeTargetCount);
					MS_DUMP_CLEAN(
					  indentation + 1, "  chainCount: %u", this->templateDependencyStructure->chainCount);
					MS_DUMP_CLEAN(indentation + 2, "<TemplateLayers>");
					for (const auto& layer : this->templateDependencyStructure->templateLayers)
					{
						MS_DUMP_CLEAN(indentation + 3, "<FrameDependencyTemplate>");
						MS_DUMP_CLEAN(indentation + 3, "  spatialLayerId: %u", layer.spatialLayer);
						MS_DUMP_CLEAN(indentation + 3, "  temporalLayerId: %u", layer.temporalLayer);
						std::string dtis;
						for (const auto& dti : layer.decodeTargetIndications)
						{
							dtis += dtiToString[dti];
						}
						MS_DUMP_CLEAN(indentation + 3, "  decodeTargetIndications: %s", dtis.c_str());
						std::string fdiffs;
						for (const auto& fdiff : layer.frameDiffs)
						{
							if (!fdiffs.empty())
							{
								fdiffs += ",";
							}

							fdiffs += std::to_string(fdiff);
						}
						MS_DUMP_CLEAN(indentation + 3, "  frameDiffs: %s", fdiffs.c_str());
						std::string fdiffChains;
						for (const auto& fdiffChain : layer.frameDiffChains)
						{
							if (!fdiffChains.empty())
							{
								fdiffChains += ",";
							}

							fdiffChains += std::to_string(fdiffChain);
						}
						MS_DUMP_CLEAN(indentation + 3, "  frameDiffChains: %s", fdiffChains.c_str());
						MS_DUMP_CLEAN(indentation + 3, "</FrameDependencyTemplate>");
					}
					MS_DUMP_CLEAN(indentation + 2, "</TemplateLayers>");
					MS_DUMP_CLEAN(indentation + 1, "</TemplateDependencyStructure>");
				}

				if (this->activeDecodeTargetsBitmask.has_value())
				{
					MS_DUMP_CLEAN(
					  indentation + 1,
					  "activeDecodeTargetsBitmask: %s",
					  std::bitset<32>(this->activeDecodeTargetsBitmask.value()).to_string().c_str());
				}
				MS_DUMP_CLEAN(indentation, "</DependencyDescriptor>");
			}

			DependencyDescriptor::DecodeTargetIndication DependencyDescriptor::GetDecodeTargetIndication(
			  uint8_t decodeTarget) const
			{
				MS_TRACE();

				if (this->customDecodeTargetIndicationsPresent)
				{
					if (decodeTarget >= this->customDecodeTargetIndications.size())
					{
						return DecodeTargetIndication::NOT_PRESENT;
					}

					return this->customDecodeTargetIndications[decodeTarget];
				}

				const auto& decodeTargetIndications =
				  this->templateDependencyStructure->templateLayers[this->templateId].decodeTargetIndications;

				if (decodeTarget >= decodeTargetIndications.size())
				{
					return DecodeTargetIndication::NOT_PRESENT;
				}

				return decodeTargetIndications[decodeTarget];
			}

			std::optional<uint8_t> DependencyDescriptor::GetDecodeTarget(
			  uint8_t spatialLayer, uint8_t temporalLayer) const
			{
				MS_TRACE();

				const auto& decodeTargetLayers = this->templateDependencyStructure->decodeTargetLayers;

				for (size_t decodeTarget{ 0 }; decodeTarget < decodeTargetLayers.size(); ++decodeTarget)
				{
					const auto& decodeTargetLayer = decodeTargetLayers[decodeTarget];

					if (decodeTargetLayer.spatialLayer == spatialLayer && decodeTargetLayer.temporalLayer == temporalLayer)
					{
						return static_cast<uint8_t>(decodeTarget);
					}
				}

				return std::nullopt;
			}

			uint32_t DependencyDescriptor::GetDecodeTargetsBitmask(
			  uint8_t maxSpatialLayer, uint8_t maxTemporalLayer) const
			{
				MS_TRACE();

				const auto& decodeTargetLayers = this->templateDependencyStructure->decodeTargetLayers;
				uint32_t bitmask{ 0 };

				for (size_t decodeTarget{ 0 }; decodeTarget < decodeTargetLayers.size(); ++decodeTarget)
				{
					const auto& decodeTargetLayer = decodeTargetLayers[decodeTarget];

					if (decodeTargetLayer.spatialLayer <= maxSpatialLayer && decodeTargetLayer.temporalLayer <= maxTemporalLayer)
					{
						bitmask |= (uint32_t{ 1 } << decodeTarget);
					}
				}

				return bitmask;
			}

			uint8_t DependencyDescriptor::GetMaxRewrittenLength() const
			{
				MS_TRACE();

				return static_cast<uint8_t>(std::min<uint32_t>(
				  (GetRewrittenBits(/*withActiveDecodeTargets*/ true) + 7) >> 3,
				  Consts::TwoBytesRtpExtensionMaxLength));
			}

			uint8_t DependencyDescriptor::Write(const RewriteParams& params, uint8_t* buffer) const
			{
				MS_TRACE();

				const auto* data = this->bitStream.GetData();
				const bool writeActiveDecodeTargets =
				  params.activeDecodeTargetsBitmask.has_value() || this->activeDecodeTargetsPresent;
				const uint32_t bits = GetRewrittenBits(writeActiveDecodeTargets);

				if (bits > Consts::TwoBytesRtpExtensionMaxLength * 8)
				{
					MS_WARN_DEV("rewritten descriptor does not fit [bits:%" PRIu32 "]", bits);

					return 0;
				}

				const auto len = static_cast<uint8_t>((bits + 7) >> 3);

				std::memset(buffer, 0, len);

				BitWriter writer(buffer);

				// Mandatory descriptor fields.
				writer.PutBit(this->startOfFrame ? 1 : 0);
				writer.PutBit(this->endOfFrame ? 1 : 0);
				writer.PutBits(6, (this->frameDependencyTemplateId + params.templateIdDelta) % MaxTemplates);
				writer.PutBits(16, static_cast<uint16_t>(this->frameNumber + params.frameNumberDelta));

				// Extended descriptor fields.
				if (this->extendedDescriptorFieldsPresent || writeActiveDecodeTargets)
				{
					writer.PutBit(this->isKeyFrame ? 1 : 0);
					writer.PutBit(writeActiveDecodeTargets ? 1 : 0);
					writer.PutBit(this->customDecodeTargetIndicationsPresent ? 1 : 0);
					writer.PutBit(this->customFrameDiffsPresent ? 1 : 0);
					writer.PutBit(this->customChainsPresent ? 1 : 0);

					if (this->isKeyFrame)
					{
						writer.PutBits(
						  6,
						  (this->templateDependencyStructure->templateIdOffset + params.templateIdDelta) %
						    MaxTemplates);
						writer.CopyBits(
						  data, TemplateDependencyStructureBodyOffset, this->templateDependencyStructureEndOffset);
					}

					if (writeActiveDecodeTargets)
					{
						writer.PutBits(
						  this->templateDependencyStructure->decodeTargetCount,
						  params.activeDecodeTargetsBitmask.value_or(
						    this->activeDecodeTargetsBitmask.value_or(0)));
					}
				}

				// Custom fields of the frame dependency definition.
				writer.CopyBits(
				  data, this->frameDependencyDefinitionOffset, this->frameDependencyDefinitionEndOffset);

				MS_ASSERT(writer.GetOffset() == bits, "wrong number of written bits");

				return len;
			}

			uint32_t DependencyDescriptor::GetRewrittenBits(bool withActiveDecodeTargets) const
			{
				MS_TRACE();

				// Mandatory descriptor fields.
				uint32_t bits{ 24 };

				if (this->extendedDescriptorFieldsPresent || withActiveDecodeTargets)
				{
					// Flags of the extended descriptor fields.
					bits += 5;

					if (this->isKeyFrame)
					{
						bits += this->templateDependencyStructureEndOffset - TemplateDependencyStructureOffset;
					}

					if (withActiveDecodeTargets)
					{
						bits += this->templateDependencyStructure->decodeTargetCount;
					}
				}

				bits += this->frameDependencyDefinitionEndOffset - this->frameDependencyDefinitionOffset;

				return bits;
			}

			bool DependencyDescriptor::ReadMandatoryDescriptorFields()
			{
				MS_TRACE();

				if (this->bitStream.GetLeftBits() < 24)
				{
					return false;
				}

				this->startOfFrame              = this->bitStream.GetBit();
				this->endOfFrame                = this->bitStream.GetBit();
				this->frameDependencyTemplateId = this->bitStream.GetBits(6);
				this->frameNumber               = this->bitStream.GetBits(16);

				return true;
			}

			bool DependencyDescriptor::ReadExtendedDescriptorFields()
			{
				MS_TRACE();

				if (this->bitStream.GetLeftBits() < 5)
				{
					return false;
				}

				this->extendedDescriptorFieldsPresent = true;

				const bool templateDependencyStructurePresent = this->bitStream.GetBit();

				this->activeDecodeTargetsPresent           = this->bitStream.GetBit();
				this->customDecodeTargetIndicationsPresent = this->bitStream.GetBit();
				this->customFrameDiffsPresent              = this->bitStream.GetBit();
				this->customChainsPresent                  = this->bitStream.GetBit();

				if (templateDependencyStructurePresent)
				{
					// Parse it into a new structure so the previous one remains valid for
					// the descriptors that were parsed with it.
					this->templateDependencyStructure = std::make_shared<TemplateDependencyStructure>();

					if (!ReadTemplateDependencyStructure())
					{
						return false;
					}

					this->templateDependencyStructureEndOffset = this->bitStream.GetOffset();
					this->isKeyFrame                           = true;

					this->activeDecodeTargetsBitmask =
					  getAllDecodeTargetsBitmask(this->templateDependencyStructure->decodeTargetCount);

					this->templateDependencyStructure->activeDecodeTargetsBitmask =
					  this->activeDecodeTargetsBitmask.value();
				}

				if (this->activeDecodeTargetsPresent)
				{
					if (!this->templateDependencyStructure)
					{
						return false;
					}

					if (this->bitStream.GetLeftBits() < this->templateDependencyStructure->decodeTargetCount)
					{
						return false;
					}

					this->activeDecodeTargetsBitmask =
					  this->bitStream.GetBits(this->templateDependencyStructure->decodeTargetCount);
				}

				return true;
			}

			bool DependencyDescriptor::ReadTemplateDependencyStructure()
			{
				MS_TRACE();

				if (this->bitStream.GetLeftBits() < 11)
				{
					return false;
				}

				this->templateDependencyStructure->templateIdOffset  = this->bitStream.GetBits(6);
				this->templateDependencyStructure->decodeTargetCount = this->bitStream.GetBits(5) + 1;

				if (!ReadTemplateLayers())
				{
					return false;
				}

				if (!ReadTemplateDecodeTargetIndications())
				{
					return false;
				}

				if (!ReadTemplateFrameDiffs())
				{
					return false;
				}

				if (!ReadTemplateFrameDiffChains())
				{
					return false;
				}

				ComputeDecodeTargetLayers();

				if (!ReadRenderResolutions())
				{
					return false;
				}

				return true;
			}

			bool DependencyDescriptor::ReadTemplateLayers()
			{
				MS_TRACE();

				uint8_t temporalId    = 0;
				uint8_t spatialId     = 0;
				uint32_t nextLayerIdc = 0;

				do
				{
					if (this->templateDependencyStructure->templateLayers.size() == MaxTemplates)
					{
						return false;
					}

					this->templateDependencyStructure->templateLayers.emplace_back(spatialId, temporalId);

					if (this->bitStream.GetLeftBits() < 2)
					{
						return false;
					}

					nextLayerIdc = this->bitStream.GetBits(2);

					// nextLayerIdc == 0, same spatialId and temporalId.
					if (nextLayerIdc == 1)
					{
						temporalId++;
					}
					else if (nextLayerIdc == 2)
					{
						temporalId = 0;
						spatialId++;
					}
				} while (nextLayerIdc != 3);

				this->templateDependencyStructure->spatialLayers  = spatialId;
				this->templateDependencyStructure->temporalLayers = temporalId;

				return true;
			}

			bool DependencyDescriptor::ReadTemplateDecodeTargetIndications()
			{
				MS_TRACE();

				auto templateCount = this->templateDependencyStructure->templateLayers.size();

				for (size_t templateIndex = 0; templateIndex < templateCount; ++templateIndex)
				{
					for (uint8_t dtIndex = 0; dtIndex < this->templateDependencyStructure->decodeTargetCount;
					     ++dtIndex)
					{
						if (this->bitStream.GetLeftBits() < 2)
						{
							return false;
						}

						this->templateDependencyStructure->templateLayers[templateIndex]
						  .decodeTargetIndications.push_back(
						    static_cast<DecodeTargetIndication>(this->bitStream.GetBits(2)));
					}
				}

				return true;
			}

			bool DependencyDescriptor::ReadTemplateFrameDiffs()
			{
				MS_TRACE();

				auto templateCount = this->templateDependencyStructure->templateLayers.size();

				for (size_t templateIndex = 0; templateIndex < templateCount; ++templateIndex)
				{
					if (this->bitStream.GetLeftBits() < 1)
					{
						return false;
					}

					bool followsFlag = this->bitStream.GetBit();

					while (followsFlag)
					{
						if (this->bitStream.GetLeftBits() < 5)
						{
							return false;
						}

						const uint8_t fdiff = this->bitStream.GetBits(4) + 1;

						this->templateDependencyStructure->templateLayers[templateIndex].frameDiffs.push_back(
						  fdiff);

						followsFlag = this->bitStream.GetBit();
					}
				}

				return true;
			}

			bool DependencyDescriptor::ReadTemplateFrameDiffChains()
			{
				MS_TRACE();

				auto chainCount =
				  this->bitStream.ReadNs(this->templateDependencyStructure->decodeTargetCount + 1);

				if (!chainCount.has_value())
				{
					return false;
				}

				this->templateDependencyStructure->chainCount = chainCount.value();

				if (chainCount == 0)
				{
					return true;
				}

				for (uint8_t dtIndex = 0; dtIndex < this->templateDependencyStructure->decodeTargetCount;
				     ++dtIndex)
				{
					auto chain = this->bitStream.ReadNs(chainCount.value());

					if (!chain.has_value())
					{
						return false;
					}

					this->templateDependencyStructure->decodeTargetProtectedBy.push_back(chain.value());
				}

				auto templateCount = this->templateDependencyStructure->templateLayers.size();

				for (size_t templateIndex = 0; templateIndex < templateCount; ++templateIndex)
				{
					for (uint32_t chainIndex = 0; chainIndex < chainCount; ++chainIndex)
					{
						if (this->bitStream.GetLeftBits() < 4)
						{
							return false;
						}

						this->templateDependencyStructure->templateLayers[templateIndex].frameDiffChains.push_back(
						  this->bitStream.GetBits(4));
					}
				}

				return true;
			}

			bool DependencyDescriptor::ReadRenderResolutions()
			{
				MS_TRACE();

				if (this->bitStream.GetLeftBits() < 1)
				{
					return false;
				}

				const bool resolutionsPresent = this->bitStream.GetBit();

				if (!resolutionsPresent)
				{
					return true;
				}

				// Render width and height (16 bits each) of each spatial layer.
				const uint32_t bits = (this->templateDependencyStructure->spatialLayers + 1) * 32;

				if (this->bitStream.GetLeftBits() < bits)
				{
					return false;
				}

				this->bitStream.SkipBits(bits);

				return true;
			}

			void DependencyDescriptor::ComputeDecodeTargetLayers()
			{
				MS_TRACE();

				auto& decodeTargetLayers   = this->templateDependencyStructure->decodeTargetLayers;
				const auto& templateLayers = this->templateDependencyStructure->templateLayers;

				decodeTargetLayers.clear();

				for (uint8_t dtIndex = 0; dtIndex < this->templateDependencyStructure->decodeTargetCount;
				     ++dtIndex)
				{
					DecodeTargetLayer decodeTargetLayer;

					for (const auto& templateLayer : templateLayers)
					{
						if (templateLayer.decodeTargetIndications[dtIndex] != DecodeTargetIndication::NOT_PRESENT)
						{
							decodeTargetLayer.spatialLayer = std::max<uint8_t>(
							  decodeTargetLayer.spatialLayer, static_cast<uint8_t>(templateLayer.spatialLayer));
							decodeTargetLayer.temporalLayer = std::max<uint8_t>(
							  decodeTargetLayer.temporalLayer, static_cast<uint8_t>(templateLayer.temporalLayer));
						}
					}

					decodeTargetLayers.push_back(decodeTargetLayer);
				}
			}

			bool DependencyDescriptor::ReadFrameDependencyDefinition()
			{
				MS_TRACE();

				if (!this->templateDependencyStructure)
				{
					MS_WARN_DEV("no template dependency structure");

					return false;
				}

				const uint8_t templateIndex = (this->frameDependencyTemplateId + MaxTemplates -
				                               this->templateDependencyStructure->templateIdOffset) %
				                              MaxTemplates;

				if (this->templateDependencyStructure->templateLayers.size() <= templateIndex)
				{
					MS_WARN_DEV("invalid template index %u", templateIndex);

					return false;
				}

				this->templateId = templateIndex;

				// Retrieve spatial and temporal layers.
				this->spatialLayer =
				  this->templateDependencyStructure->templateLayers[this->templateId].spatialLayer;
				this->temporalLayer =
				  this->templateDependencyStructure->templateLayers[this->templateId].temporalLayer;

				this->frameDependencyDefinitionOffset = this->bitStream.GetOffset();

				if (this->customDecodeTargetIndicationsPresent && !ReadFrameDecodeTargetIndications())
				{
					return false;
				}

				if (this->customFrameDiffsPresent && !ReadFrameDiffs())
				{
					return false;
				}

				if (this->customChainsPresent && !ReadFrameDiffChains())
				{
					return false;
				}

				this->frameDependencyDefinitionEndOffset = this->bitStream.GetOffset();

				return true;
			}

			bool DependencyDescriptor::ReadFrameDecodeTargetIndications()
			{
				MS_TRACE();

				for (uint8_t dtIndex = 0; dtIndex < this->templateDependencyStructure->decodeTargetCount;
				     ++dtIndex)
				{
					if (this->bitStream.GetLeftBits() < 2)
					{
						return false;
					}

					this->customDecodeTargetIndications.push_back(
					  static_cast<DecodeTargetIndication>(this->bitStream.GetBits(2)));
				}

				return true;
			}

			bool DependencyDescriptor::ReadFrameDiffs()
			{
				MS_TRACE();

				while (true)
				{
					if (this->bitStream.GetLeftBits() < 2)
					{
						return false;
					}

					const uint32_t nextFrameDiffSize = this->bitStream.GetBits(2);

					if (nextFrameDiffSize == 0)
					{
						return true;
					}

					if (this->bitStream.GetLeftBits() < 4 * nextFrameDiffSize)
					{
						return false;
					}

					this->bitStream.SkipBits(4 * nextFrameDiffSize);
				}
			}

			bool DependencyDescriptor::ReadFrameDiffChains()
			{
				MS_TRACE();

				const uint32_t bits = this->templateDependencyStructure->chainCount * 8;

				if (this->bitStream.GetLeftBits() < bits)
				{
					return false;
				}

				this->bitStream.SkipBits(bits);

				return true;
			}
		} // namespace Codecs
	} // namespace RTP
} // namespace RTC
