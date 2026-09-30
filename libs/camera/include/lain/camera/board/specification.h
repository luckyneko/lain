#pragma once

#include "lain/camera/board/pattern.h"

#include <lain/core/length.h>
#include <lain/math/types.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace lain::camera::board
{
	// One measured physical length of a board, with optional HARD bounds on its true value
	// (CONTEXT.md, "Measurement"). A missing bound means the uncertainty is unknown, which is not the
	// same as a bound equal to the value. Deliberately this one concrete case and not a generic
	// Measurement<T>, which waits for a second caller (M9, Not in this milestone).
	struct MeasuredLength
	{
		core::Length value;
		std::optional<core::Length> lowerBound;
		std::optional<core::Length> upperBound;
	};

	// One physical board printed from a pattern (CONTEXT.md, "Board instance"): its own identity and
	// its own measurement. Two boards from one pattern share a fingerprint and may measure
	// differently.
	struct Instance
	{
		std::string identity;		 // how this board is told apart from others printed from the pattern
		MeasuredLength squareLength; // the printed chessboard square's side
	};

	enum class SpecificationProblem
	{
		NoIdentity,				 // an empty instance identity
		NonPositiveLength,		 // a square length that is zero or negative
		BoundsDoNotContainValue, // a lower bound above the value, or an upper bound below it
	};

	struct SpecificationDiagnostic
	{
		SpecificationProblem problem;
		std::string detail;
	};

	struct SpecificationResult;

	// Exactly which board: its pattern and its physical instance (CONTEXT.md, "Board specification").
	// Board calibration needs one, because detected features alone establish no metric scale.
	//
	// Feature positions are in the BOARD FRAME, in metres: origin at the board's top-left corner as
	// printed, X right and Y down along its face, Z into the board. That is the camera frame's
	// convention (XRightYDownZForward), so a camera facing the print squarely with its image upright
	// sees the board's axes as its own.
	class Specification
	{
	public:
		static SpecificationResult create(const Pattern& pattern, const Instance& instance);

		const Pattern& pattern() const { return m_pattern; }
		const Instance& instance() const { return m_instance; }

		// The marker's side, from the measured square and the pattern's ratio.
		core::Length markerLength() const;

		// Where corner `id` is on the board, or nullopt for an id the pattern does not have.
		std::optional<math::Vec3d> cornerPosition(std::uint32_t id) const;

		// One line for a person: the layout, the dictionary, the board's identity and square length,
		// and the start of the pattern fingerprint.
		std::string toString() const;

	private:
		Specification(const Pattern& pattern, const Instance& instance)
			: m_pattern(pattern)
			, m_instance(instance)
		{
		}

		Pattern m_pattern;
		Instance m_instance;
	};

	struct SpecificationResult
	{
		std::optional<Specification> specification;
		std::vector<SpecificationDiagnostic> diagnostics; // empty exactly when `specification` is set
	};
} // namespace lain::camera::board
