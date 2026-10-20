#include "lain/camera/flow/calibrationnode.h"

#include "decimal.h"

#include <lain/camera/cameramodel.h>

#include <algorithm>
#include <cstdint>
#include <string>

namespace lain::camera
{
	CalibrationNode::CalibrationNode(std::string name)
		: Node(std::move(name))
	{
		m_footage = addInput<media::FrameSequence>("footage");
	}

	void CalibrationNode::declareRequest()
	{
		// Optional: most calibrations start from nothing, and the policy says what one is for.
		m_imported = addInput<CameraModel>("imported", flow::Presence::Optional);

		// The request's own defaults, so a fresh node asks for exactly what calibration::Request does.
		const calibration::Request defaults;
		m_model = addParam<DistortionModel>("model", defaults.model);
		m_importedPolicy = addParam<calibration::ImportedModelPolicy>("importedPolicy", defaults.importedPolicy);
		m_fitnessProfile = addParam<std::string>("fitnessProfile", defaults.fitnessProfile);
		m_heldOutFraction = addParam<float>("heldOutFraction", float(defaults.heldOutFraction));
		m_resamples = addParam<int>("resamples", int(defaults.resamples));
		m_seed = addParam<int>("seed", int(defaults.seed));
		m_deterministic = addParam<bool>("deterministic", false);
	}

	calibration::Request CalibrationNode::request(const flow::NodeEvaluation& evaluation) const
	{
		calibration::Request request;
		request.model = param(m_model).get<DistortionModel>();
		const flow::PortValue& imported = evaluation.input(m_imported);
		if (!imported.empty())
			request.imported = imported.get<CameraModel>();
		request.importedPolicy = param(m_importedPolicy).get<calibration::ImportedModelPolicy>();
		request.fitnessProfile = param(m_fitnessProfile).get<std::string>();
		request.heldOutFraction = detail::decimal(param(m_heldOutFraction).get<float>());
		request.resamples = std::uint32_t(std::max(0, param(m_resamples).get<int>()));
		// A seed is an identity, not a quantity: a negative int keeps its bits rather than clamping,
		// so every int names a distinct seed.
		request.seed = std::uint64_t(std::uint32_t(param(m_seed).get<int>()));
		request.execution = param(m_deterministic).get<bool>() ? calibration::ExecutionPolicy::DeterministicDebug
															   : calibration::ExecutionPolicy::Normal;
		return request;
	}

	const media::FrameSequence& CalibrationNode::footage(const flow::NodeEvaluation& evaluation) const
	{
		return evaluation.input(m_footage).get<media::FrameSequence>();
	}
} // namespace lain::camera
