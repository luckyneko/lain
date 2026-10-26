#include "lain/rigplan/serialize/capture.h"

#include "strict.h"

#include <lain/camera/serialize/capturerecord.h> // a capture record as camera documents spell it
#include <lain/rigplan/serialize/rigplan.h>
#include <lain/string/format.h>

#include <algorithm>
#include <utility>

namespace lain::rigplan
{
	// The manifest's parts with nothing checked, as the document holds them. The rig plan is not among
	// them: it is a document of its own, embedded whole, so it is set and taken out by its key.
	struct CaptureOutputDoc
	{
		OutputFormat format = OutputFormat::Png;
		std::uint32_t bits = 8;
		image::ColorSpace curve = image::ColorSpace::sRGB;
		std::optional<std::uint8_t> quality;
		bool lossy = false;
	};
	LAIN_SERIALIZE(CaptureOutputDoc, format, bits, curve, quality, lossy)

	struct CaptureCameraDoc
	{
		std::string id;
		std::string name;
		CaptureOutputDoc output;
		camera::CaptureRecord record;
	};
	LAIN_SERIALIZE(CaptureCameraDoc, id, name, output, record)

	struct CaptureDeviceDoc
	{
		std::string name;
		std::string kind;
	};
	LAIN_SERIALIZE(CaptureDeviceDoc, name, kind)

	struct CaptureManifestDoc
	{
		std::string generator;
		CaptureDeviceDoc device;
		Shutter shutter = Shutter::Global;
		std::vector<CaptureCameraDoc> cameras;
	};
	LAIN_SERIALIZE(CaptureManifestDoc, generator, device, shutter, cameras)

	// The key the rig plan is embedded under.
	static constexpr const char* kRigPlanKey = "rigPlan";

	// Whether `format` loses fidelity: what a manifest's `lossy` states.
	static bool lossyFormat(OutputFormat format)
	{
		return format == OutputFormat::Jpeg;
	}

	data::Value captureManifestToValue(const CaptureManifest& manifest, const std::filesystem::path& folder)
	{
		CaptureManifestDoc doc;
		doc.generator = manifest.generator;
		doc.device = {manifest.device, manifest.deviceKind};
		doc.shutter = manifest.shutter;
		for (const CaptureCamera& camera : manifest.cameras)
		{
			const SensorOutput& o = camera.output;
			doc.cameras.push_back({camera.id.toString(), camera.name, {o.format, o.bits, o.curve, o.quality, lossyFormat(o.format)}, camera.record});
		}
		data::Value document = data::versionedDocument(kCaptureDocumentVersion, data::toValue(doc));
		data::Value plan = rigPlanToValue(manifest.plan);
		relativizePaths(plan, folder);
		// After the generator, device and shutter, before the cameras: the order a person reads it in.
		data::Value::Object& entries = *document.asObject();
		const auto cameras = std::find_if(entries.begin(), entries.end(), [](const auto& entry)
										  { return entry.first == "cameras"; });
		entries.insert(cameras, {kRigPlanKey, std::move(plan)});
		return document;
	}

	CaptureManifestRead captureManifestFromValue(const data::Value& document, const std::filesystem::path& folder)
	{
		CaptureManifestRead result;
		// The rig plan out first, so the rest reads as its parts; a document with no rig plan is missing
		// a key, which is a problem of the same kind as any other.
		data::Value rest = document;
		std::optional<data::Value> plan;
		if (data::Value::Object* entries = rest.asObject())
		{
			for (auto entry = entries->begin(); entry != entries->end(); ++entry)
			{
				if (entry->first == kRigPlanKey)
				{
					plan = std::move(entry->second);
					entries->erase(entry);
					break;
				}
			}
		}
		const std::optional<CaptureManifestDoc> parts =
			detail::readStrictly<CaptureManifestDoc>(rest, "capture", kCaptureDocumentVersion, result.problems);
		if (!parts)
			return result;
		if (!plan)
		{
			result.problems.push_back(string::format("'{}' is missing", kRigPlanKey));
			return result;
		}
		resolvePaths(*plan, folder);
		RigPlanRead read = rigPlanFromValue(*plan);
		for (const std::string& problem : read.problems)
			result.problems.push_back(string::format("'{}': {}", kRigPlanKey, problem));

		CaptureManifest manifest;
		manifest.generator = parts->generator;
		manifest.device = parts->device.name;
		manifest.deviceKind = parts->device.kind;
		manifest.shutter = parts->shutter;
		for (std::size_t i = 0; i < parts->cameras.size(); ++i)
		{
			const CaptureCameraDoc& camera = parts->cameras[i];
			const std::optional<core::Uuid> id = core::Uuid::parse(camera.id);
			if (!id)
				result.problems.push_back(string::format("'cameras[{}].id' is '{}', which is not a uuid", i, camera.id));
			const CaptureOutputDoc& o = camera.output;
			if (o.lossy != lossyFormat(o.format))
				result.problems.push_back(string::format("'cameras[{}].output.lossy' is {}, which its format is not", i, o.lossy));
			manifest.cameras.push_back({id.value_or(core::Uuid{}), camera.name, {o.format, o.bits, o.curve, o.quality}, camera.record});
		}
		if (result.problems.empty())
		{
			manifest.plan = std::move(*read.plan);
			result.manifest = std::move(manifest);
		}
		return result;
	}
} // namespace lain::rigplan
