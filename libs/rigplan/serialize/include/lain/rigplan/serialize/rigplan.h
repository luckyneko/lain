#pragma once

#include <lain/data/value.h>
#include <lain/rigplan/rigplan.h>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

// Rig plans and specification catalogues as documents (CONTEXT.md, "Rig plan", "Specification
// catalogue"). A separate target over lain::rigplan + lain::data by the boundary rule
// lain::camera::serialize follows: lain::rigplan stays free of lain::data.
//
// The conventions are the camera documents': a length a datasheet quotes is a number with its unit in
// its key (focalLengthMm, pixelPitchUm), an angle has Degrees in its key, a position is {x, y, z} in
// metres, an identity is a canonical uuid string, and a distortion is the camera-model document's
// {type, value} under its stable key.
namespace lain::rigplan
{
	// The version a rig plan document is written at, and the newest this build reads. It stays 1, and
	// its shape may still change without a migration, until M15 slice 7 commits the first example rig
	// plan: until then no rig plan exists outside the tests.
	constexpr std::uint32_t kRigPlanDocumentVersion = 1;

	// The version a specification catalogue document is written at, and the newest this build reads.
	constexpr std::uint32_t kCatalogueDocumentVersion = 1;

	// A rig plan as a document: {version, specifications: {sensors, lenses, cameras, lights}, room, planes,
	// boxes, props, boards, mounts, cameras, reference, lights, timeline: {frames, rate: {numerator,
	// denominator}}, seed, scales}, the room and the reference absent when there is none. A board's pattern and instance are spelled as a board specification document
	// spells them, and a path is {keys: [{frame, position, aim}]}.
	data::Value rigPlanToValue(const RigPlan& plan);

	// The outcome of reading a rig plan document: a rig plan, or every reason there is none.
	struct RigPlanRead
	{
		std::optional<RigPlan> plan;
		std::vector<std::string> problems; // empty exactly when `plan` is set
	};

	// A rig plan from a document, its SHAPE read strictly, as the camera documents are: an unknown or
	// missing key, an unknown name for a light's kind or a distortion, an identity that is not a uuid,
	// or a value of the wrong kind is a problem named by its path, never a silent default. Its CONTENT
	// is not judged: a rig plan naming a specification it lacks still reads, so it can be opened and
	// fixed, and check() is what says it cannot be realised.
	RigPlanRead rigPlanFromValue(const data::Value& document);

	// ADR-0027 at the file boundary, for a rig plan document: every path it holds (an image material's
	// picture, a prop's mesh) rewritten relative to `folder` when inside its tree (relativizePaths, after
	// rigPlanToValue when saving to a file there), or read against it (resolvePaths, before
	// rigPlanFromValue when loading from one). Never applied to an undo snapshot, which meets no file,
	// so the conversions themselves need no idea where a document lives (ADR-0027's reason).
	void relativizePaths(data::Value& document, const std::filesystem::path& folder);
	void resolvePaths(data::Value& document, const std::filesystem::path& folder);

	// A specification catalogue as a document: {version, sensors, lenses, cameras, lights}.
	data::Value catalogueToValue(const Specifications& specifications);

	// The outcome of reading a catalogue document: its specifications, or every reason there are none.
	struct CatalogueRead
	{
		std::optional<Specifications> specifications;
		std::vector<std::string> problems; // empty exactly when `specifications` is set
	};

	// A catalogue from a document, read as a rig plan's specifications are: shape strictly, content
	// left to check().
	CatalogueRead catalogueFromValue(const data::Value& document);
} // namespace lain::rigplan
