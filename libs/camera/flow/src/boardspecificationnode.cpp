#include "lain/camera/flow/boardspecificationnode.h"

#include "decimal.h"

#include <lain/log/log.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

namespace lain::camera
{
	// A count from an int parameter. A negative one is taken as zero rather than wrapped, so it is
	// refused as the too-small count it is instead of as an absurdly large one.
	static std::uint32_t count(int value)
	{
		return std::uint32_t(std::max(0, value));
	}

	BoardSpecificationNode::BoardSpecificationNode()
		: Node("BoardSpecification")
	{
		// A common printed board: 7x5 squares of 30 mm, markers three quarters of a square, from the
		// 5x5 dictionary of 100.
		m_dictionary = addParam<board::Dictionary>("dictionary", board::Dictionary::Aruco5x5_100);
		m_squaresX = addParam<int>("squaresX", 7);
		m_squaresY = addParam<int>("squaresY", 5);
		m_markerToSquare = addParam<float>("markerToSquare", 0.75f);
		m_firstMarkerId = addParam<int>("firstMarkerId", 0);
		m_layout = addParam<board::CharucoLayout>("layout", board::CharucoLayout::Standard);
		m_identity = addParam<std::string>("identity", "board");
		m_squareLength = addParam<float>("squareLengthMm", 30.0f);

		m_board = addOutput<board::Specification>("board");
	}

	void BoardSpecificationNode::compute(flow::NodeEvaluation& evaluation) const
	{
		board::PatternParameters parameters;
		parameters.dictionary = param(m_dictionary).get<board::Dictionary>();
		parameters.squaresX = count(param(m_squaresX).get<int>());
		parameters.squaresY = count(param(m_squaresY).get<int>());
		parameters.markerToSquare = detail::decimal(param(m_markerToSquare).get<float>());
		parameters.firstMarkerId = count(param(m_firstMarkerId).get<int>());
		parameters.layout = param(m_layout).get<board::CharucoLayout>();

		const board::PatternResult pattern = board::Pattern::create(parameters);
		if (!pattern.pattern)
		{
			for (const board::PatternDiagnostic& problem : pattern.diagnostics)
				log::warn("camera: not a board pattern: {}", problem.detail);
			evaluation.output(m_board).clear();
			return;
		}

		// A whole number of micrometres: the precision a float holds at board scale, so 23.7 means
		// 23.7 mm rather than the 23.700000763 a float stores.
		const float millimetres = param(m_squareLength).get<float>();
		board::Instance instance;
		instance.identity = param(m_identity).get<std::string>();
		instance.squareLength.value =
			core::Length::fromMicrometres(std::isfinite(millimetres) ? std::round(double(millimetres) * 1000.0) : 0.0);

		const board::SpecificationResult specification = board::Specification::create(*pattern.pattern, instance);
		if (!specification.specification)
		{
			for (const board::SpecificationDiagnostic& problem : specification.diagnostics)
				log::warn("camera: not a board specification: {}", problem.detail);
			evaluation.output(m_board).clear();
			return;
		}
		evaluation.output(m_board).set(*specification.specification);
	}
} // namespace lain::camera
