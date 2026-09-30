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

	data::Value specificationToValue(const Specification& specification);

	struct SpecificationRead
	{
		std::optional<Specification> specification;
		std::vector<std::string> problems; // empty exactly when `specification` is set
	};

	// A board specification from a document, read strictly (see cameraModelFromValue): a misspelled
	// dictionary name is a problem, not the first dictionary. Pattern::create and Specification::create
	// then judge what was read, and their diagnostics join the problems.
	SpecificationRead specificationFromValue(const data::Value& document);
} // namespace lain::camera::board
