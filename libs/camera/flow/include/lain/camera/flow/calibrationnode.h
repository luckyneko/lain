#pragma once

#include <lain/camera/calibration/request.h>
#include <lain/flow/evaluation.h>
#include <lain/flow/node.h>
#include <lain/media/framesequence.h>

#include <string>

namespace lain::camera
{
	// What the calibration nodes share, whatever their method (calibrateCamera,
	// calibrateCameraTargetless): one camera's `footage`, and the calibration::Request settings every
	// method takes, an `imported` model among them.
	//
	// Abstract: a method's node declares its own inputs after `footage`, calls declareRequest(),
	// declares what only its method needs and its report, and builds the report from request() and
	// footage(). One routine for the settings, so the two methods cannot come to mean different things
	// by the same name.
	class CalibrationNode : public flow::Node
	{
	protected:
		// Declares `footage`, input 0, so a method's own inputs follow it.
		explicit CalibrationNode(std::string name);

		// Declare `imported`, an optional camera model taking part through `importedPolicy`, then the
		// settings every calibration takes, with calibration::Request's defaults: model,
		// importedPolicy, fitnessProfile, heldOutFraction, resamples, seed and deterministic. Not
		// maximumViews: a targetless calibration selects no views, so the method that does declares it.
		void declareRequest();

		// The request those settings ask for, with the imported model when one is wired. A float is
		// the decimal a person typed, a count below 0 is 0, and a seed keeps its bits.
		calibration::Request request(const flow::NodeEvaluation& evaluation) const;

		const media::FrameSequence& footage(const flow::NodeEvaluation& evaluation) const;

	private:
		flow::PortId m_footage;			// "footage" (media::FrameSequence)
		flow::PortId m_imported;		// "imported" (CameraModel), optional
		flow::PortId m_model;			// "model" (DistortionModel)
		flow::PortId m_importedPolicy;	// "importedPolicy" (calibration::ImportedModelPolicy)
		flow::PortId m_fitnessProfile;	// "fitnessProfile" (std::string, empty = the method's own)
		flow::PortId m_heldOutFraction; // "heldOutFraction" (float)
		flow::PortId m_resamples;		// "resamples" (int)
		flow::PortId m_seed;			// "seed" (int)
		flow::PortId m_deterministic;	// "deterministic" (bool)
	};
} // namespace lain::camera
