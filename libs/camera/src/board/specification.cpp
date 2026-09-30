#include "lain/camera/board/specification.h"

#include <lain/string/format.h>

#include <string>

namespace lain::camera::board
{
	SpecificationResult Specification::create(const Pattern& pattern, const Instance& instance)
	{
		SpecificationResult result;
		const auto problem = [&result](SpecificationProblem kind, std::string detail)
		{ result.diagnostics.push_back({kind, std::move(detail)}); };

		if (instance.identity.empty())
			problem(SpecificationProblem::NoIdentity, "a board instance needs an identity");

		const MeasuredLength& square = instance.squareLength;
		if (square.value <= core::Length{})
			problem(SpecificationProblem::NonPositiveLength,
					"the square length is " + std::to_string(square.value.millimetres()) + " mm");
		if (square.lowerBound && *square.lowerBound > square.value)
			problem(SpecificationProblem::BoundsDoNotContainValue, "the lower bound is above the measured square length");
		if (square.upperBound && *square.upperBound < square.value)
			problem(SpecificationProblem::BoundsDoNotContainValue, "the upper bound is below the measured square length");

		if (result.diagnostics.empty())
			result.specification = Specification{pattern, instance};
		return result;
	}

	core::Length Specification::markerLength() const
	{
		return m_instance.squareLength.value * m_pattern.parameters().markerToSquare;
	}

	std::optional<math::Vec3d> Specification::cornerPosition(std::uint32_t id) const
	{
		if (id >= m_pattern.cornerCount())
			return std::nullopt;
		// Row by row from the top left, the first inner corner one square in from each edge.
		const std::uint32_t across = m_pattern.parameters().squaresX - 1;
		const double square = m_instance.squareLength.value.metres();
		return math::Vec3d{double(id % across + 1) * square, double(id / across + 1) * square, 0.0};
	}

	std::string Specification::toString() const
	{
		const PatternParameters& p = m_pattern.parameters();
		std::string text = lain::string::format("ChArUco {}x{} {}", p.squaresX, p.squaresY, name(p.dictionary));
		if (p.firstMarkerId != 0)
			text += lain::string::format(" from marker {}", p.firstMarkerId);
		if (p.layout == CharucoLayout::Legacy)
			text += " (legacy layout)";
		// Eight hex digits tell two patterns apart at a glance; the whole digest is in the pattern.
		return text + lain::string::format(", '{}', {:g} mm squares, pattern {}", m_instance.identity,
										   m_instance.squareLength.value.millimetres(),
										   m_pattern.fingerprint().toString().substr(0, 8));
	}
} // namespace lain::camera::board
