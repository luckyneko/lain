#include "lain/camera/flow/calibratecameranode.h"

#include "decimal.h"
#include "detection.h"

#include <lain/camera/calibration/board.h>
#include <lain/camera/calibration/request.h>

#include <algorithm>
#include <cstdint>
#include <string>

namespace lain::camera
{
	CalibrateCameraNode::CalibrateCameraNode()
		: Node("CalibrateCamera")
	{
		m_footage = addInput<media::FrameSequence>("footage");
		m_board = addInput<board::Specification>("board");
		// Optional: most calibrations start from nothing, and the policy says what one is for.
		m_imported = addInput<CameraModel>("imported", flow::Presence::Optional);

		// The request's own defaults, so a fresh node asks for exactly what calibration::Request
		// does.
		const calibration::Request defaults;
		m_model = addParam<DistortionModel>("model", defaults.model);
		m_importedPolicy = addParam<calibration::ImportedModelPolicy>("importedPolicy", defaults.importedPolicy);
		m_fitnessProfile = addParam<std::string>("fitnessProfile", defaults.fitnessProfile);
		m_maximumViews = addParam<int>("maximumViews", int(defaults.maximumViews));
		m_heldOutFraction = addParam<float>("heldOutFraction", float(defaults.heldOutFraction));
		m_resamples = addParam<int>("resamples", int(defaults.resamples));
		m_seed = addParam<int>("seed", int(defaults.seed));
		m_deterministic = addParam<bool>("deterministic", false);
		m_longestSide = addParam<int>("longestSide", 0);
		m_refineNative = addParam<bool>("refineNative", defaults.detection.refineAtNativeResolution);
		m_minimumCorners = addParam<int>("minimumCorners", int(defaults.detection.minimumCorners));

		m_report = addOutput<calibration::Report>("report");
	}

	void CalibrateCameraNode::compute(flow::NodeEvaluation& evaluation) const
	{
		calibration::Request request;
		request.model = param(m_model).get<DistortionModel>();
		const flow::PortValue& imported = evaluation.input(m_imported);
		if (!imported.empty())
			request.imported = imported.get<CameraModel>();
		request.importedPolicy = param(m_importedPolicy).get<calibration::ImportedModelPolicy>();
		request.fitnessProfile = param(m_fitnessProfile).get<std::string>();
		request.maximumViews = std::uint32_t(std::max(0, param(m_maximumViews).get<int>()));
		request.heldOutFraction = detail::decimal(param(m_heldOutFraction).get<float>());
		request.resamples = std::uint32_t(std::max(0, param(m_resamples).get<int>()));
		// A seed is an identity, not a quantity: a negative int keeps its bits rather than clamping,
		// so every int names a distinct seed.
		request.seed = std::uint64_t(std::uint32_t(param(m_seed).get<int>()));
		request.execution = param(m_deterministic).get<bool>() ? calibration::ExecutionPolicy::DeterministicDebug
															   : calibration::ExecutionPolicy::Normal;
		request.detection = detail::detectionRequest(param(m_longestSide).get<int>(), param(m_refineNative).get<bool>(),
													 param(m_minimumCorners).get<int>());

		evaluation.output(m_report).set(
			calibration::board::calibrate(evaluation.input(m_footage).get<media::FrameSequence>(),
										  evaluation.input(m_board).get<board::Specification>(), request));
	}
} // namespace lain::camera
