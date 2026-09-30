#pragma once

#include <lain/camera/board/pattern.h>
#include <lain/camera/board/specification.h>
#include <lain/data/data.h>

#include <optional>
#include <string>
#include <vector>

// Board specifications as documents: {pattern, instance}.
namespace lain::camera::board
{
	// Enums reflect as their names ("Aruco5x5_100", "Standard").
	LAIN_SERIALIZE(PatternParameters, dictionary, squaresX, squaresY, markerToSquare, firstMarkerId, layout)

	// Lengths are written in MILLIMETRES, the unit a printed board is measured in, and read back to
	// the nanometre (core::Length's resolution): 23.7 mm is 23.7 on disk and 23700000 nm in memory.
	void serialize(data::Archive& archive, Instance& instance);

	// A specification's parts with nothing checked, as a document holds them: {pattern, instance}.
	// What a document embedding a board (a calibration fixture) reads, before specificationFrom judges
	// it; the counterpart of CameraModelParameters.
	struct SpecificationParameters
	{
		PatternParameters pattern;
		Instance instance;
	};
	LAIN_SERIALIZE(SpecificationParameters, pattern, instance)

	data::Value specificationToValue(const Specification& specification);

	struct SpecificationRead
	{
		std::optional<Specification> specification;
		std::vector<std::string> problems; // empty exactly when `specification` is set
	};

	// A specification from its parts, through Pattern::create and Specification::create, with every
	// diagnostic as a problem.
	SpecificationRead specificationFrom(const SpecificationParameters& parameters);

	// A board specification from a document, read strictly (data::fromValueStrict): a misspelled
	// dictionary name is a problem, not the first dictionary. Then specificationFrom.
	SpecificationRead specificationFromValue(const data::Value& document);
} // namespace lain::camera::board
