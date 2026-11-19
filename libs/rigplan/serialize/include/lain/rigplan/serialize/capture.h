#pragma once

#include <lain/camera/capturerecord.h>
#include <lain/core/uuid.h>
#include <lain/data/value.h>
#include <lain/rigplan/rigplan.h>
#include <lain/rigplan/specification.h>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

// A synthetic capture's manifest (CONTEXT.md, "Synthetic capture"): capture.json at the root of a
// capture folder, written LAST, so a capture folder without one is an incomplete capture and nothing
// should read it as footage.
namespace lain::rigplan
{
	// The version a capture manifest is written at, and the newest this build reads.
	constexpr std::uint32_t kCaptureDocumentVersion = 1;

	// How a capture's frames were exposed. Every synthetic capture is global shutter, each frame an
	// instant (ADR-0029); the manifest says so rather than leaving it to be assumed, so a capture that
	// one day is not cannot be mistaken for one that is.
	enum class Shutter
	{
		Global,
	};

	// One camera of a capture: what its frames are, and its capture record.
	struct CaptureCamera
	{
		core::Uuid id;
		std::string name;	 // its folder under rig/, and so its identity to M9
		SensorOutput output; // its frames' format, depth, curve and quality
		// Its device and stream, and its nominal model as the record's imported manufacturer model
		// (CONTEXT.md, "Nominal model"): what a real camera's datasheet would have handed over.
		camera::CaptureRecord record;
	};

	// What capture.json holds.
	struct CaptureManifest
	{
		std::string generator;	// what wrote it, with its version: "rigplan 0.1.0"
		std::string device;		// the name of the device that rendered the light
		std::string deviceKind; // "hardware" or "cpu", as --device names them
		Shutter shutter = Shutter::Global;
		// The rig plan as rendered: its seed and scales are the ones this capture used, so this plan and
		// nothing else reproduces it (ADR-0029).
		RigPlan plan;
		std::vector<CaptureCamera> cameras; // the plan's cameras, in its order
	};

	// A manifest as a document: {version, generator, device: {name, kind}, shutter, rigPlan, cameras:
	// [{id, name, output: {format, bits, curve, quality, lossy}, record}]}. The rig plan is embedded
	// as its own versioned document, its paths rewritten relative to `folder` (the capture folder) by
	// ADR-0027's rule. A manifest only ever exists as capture.json in its capture folder, so it takes
	// the folder here rather than at a separate file boundary. `lossy` states what the format implies
	// (JPEG is the lossy one), for a reader with no codecs to ask.
	data::Value captureManifestToValue(const CaptureManifest& manifest, const std::filesystem::path& folder);

	struct CaptureManifestRead
	{
		std::optional<CaptureManifest> manifest;
		std::vector<std::string> problems; // empty exactly when `manifest` is set
	};

	// A manifest from a document, read strictly as the truth documents are, its rig plan's paths
	// resolved against `folder`. A `lossy` that disagrees with its format is a problem.
	CaptureManifestRead captureManifestFromValue(const data::Value& document, const std::filesystem::path& folder);
} // namespace lain::rigplan
