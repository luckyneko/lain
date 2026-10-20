#pragma once

#include "lain/camera/calibration/report.h"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

// The calibration fallback's vocabulary (ADR-0016): which methods a graph's calibration method
// includes, when an attempt is accepted, and the attempts a fallback made. The fallback itself is a
// graph of gates and small nodes, so neither method module knows of the other or holds a fallback
// policy; what they share is stated here, once.
namespace lain::camera::calibration
{
	// The graph-boundary policy selecting the methods a calibration tries (CONTEXT.md, "Calibration
	// method"), in priority order: a board's evidence before the scene's.
	enum class Method
	{
		Board,
		Targetless,
		BoardThenTargetless,
	};

	// Whether `selected` includes the single method `single`: Board and Targetless include themselves,
	// BoardThenTargetless both. The one place a selection's methods are named, so the node that switches
	// each method's gate and anything else that asks cannot disagree.
	bool includes(Method selected, Method single);

	// Whether an attempt is accepted (CONTEXT.md, "Accepted attempt"): it succeeded, and is Ready under
	// its own method's fitness profile. The gate that skips a later method and the record of the
	// attempts both ask THIS, so the two cannot disagree. A looser acceptance is a looser profile, not a
	// second rule here.
	bool accepts(const Report& report);

	// The attempts a fallback made, in priority order (CONTEXT.md, "Calibration attempt"): a method
	// skipped because an earlier attempt was accepted made none, and is not listed.
	struct Attempts
	{
		std::vector<Report> reports;

		// The position of the accepted attempt: the first that accepts. Derived, not stored, so it
		// cannot disagree with the reports it indexes. Empty when none is, and the reports say why.
		std::optional<std::size_t> accepted() const;

		// One line for a person: which attempt was accepted and its report's line; or that none was,
		// with each attempt's line.
		std::string toString() const;
	};
} // namespace lain::camera::calibration
