#pragma once

#include <lain/camera/cameramodel.h>
#include <lain/camera/comparison.h>
#include <lain/flow/evaluation.h>
#include <lain/flow/node.h>

namespace lain::camera
{
	// How far a camera model is from an expected one (camera::compareModels): a calibration's model
	// against a synthetic capture's truth, or a manufacturer's model against a calibration. The
	// expected model's rays, through the found one.
	//
	// Two models of different images FAIL the node with the reason, rather than passing on a comparison
	// that says nothing.
	class CompareModelsNode : public flow::Node
	{
	public:
		CompareModelsNode();

		std::unique_ptr<flow::Node> clone() const override { return std::make_unique<CompareModelsNode>(*this); }
		void compute(flow::NodeEvaluation& evaluation) const override;

	private:
		flow::PortId m_expected;   // "expected" (CameraModel)
		flow::PortId m_found;	   // "found" (CameraModel)
		flow::PortId m_comparison; // "comparison" (ModelComparison)
	};
} // namespace lain::camera
