#pragma once

#include <lain/camera/board/specification.h>
#include <lain/flow/evaluation.h>
#include <lain/flow/node.h>

namespace lain::camera
{
	// A ChArUco board specification from its parameters: the pattern (dictionary, squares, marker
	// ratio, first marker id, layout) and the physical instance (identity, measured square length).
	// What renderBoard draws, and what detectBoard and calibrateCamera look for.
	//
	// Parameters rather than inputs: a board is configuration, the same for every frame and every
	// element of a map, and nothing upstream computes one.
	//
	// Parameters that do not make a board FAIL the node with every reason (NodeEvaluation::fail): the
	// output is empty, so everything downstream is suppressed (ADR-0007) rather than run against a
	// board that is not the one asked for, and the reasons are where a host shows a failure.
	class BoardSpecificationNode : public flow::Node
	{
	public:
		BoardSpecificationNode();

		std::unique_ptr<flow::Node> clone() const override { return std::make_unique<BoardSpecificationNode>(*this); }
		void compute(flow::NodeEvaluation& evaluation) const override;

	private:
		flow::PortId m_dictionary;	   // "dictionary" (board::Dictionary)
		flow::PortId m_squaresX;	   // "squaresX" (int)
		flow::PortId m_squaresY;	   // "squaresY" (int)
		flow::PortId m_markerToSquare; // "markerToSquare" (float)
		flow::PortId m_firstMarkerId;  // "firstMarkerId" (int)
		flow::PortId m_layout;		   // "layout" (board::CharucoLayout)
		flow::PortId m_identity;	   // "identity" (std::string)
		flow::PortId m_squareLength;   // "squareLengthMm" (float, millimetres)
		flow::PortId m_board;		   // "board" (board::Specification)
	};
} // namespace lain::camera
