#include "lain/camera/flow/registercamerasnode.h"

#include "decimal.h"
#include "detection.h"

#include <lain/camera/registration/board.h>
#include <lain/camera/registration/request.h>

#include <variant>

namespace lain::camera
{
	RegisterCamerasNode::RegisterCamerasNode()
		: RigRegistrationNode("RegisterCameras")
	{
		m_board = addInput<board::Specification>("board");
		declareRequest();

		// The request's own defaults, so a fresh node asks for exactly what registration::Request and
		// board::DetectionRequest do.
		const registration::Request defaults;
		const board::DetectionRequest searchDefaults;
		m_pixelSigma = addParam<float>("pixelSigma", float(defaults.noise.pixelSigma));
		m_longestSide = addParam<int>("longestSide", 0);
		m_refineNative = addParam<bool>("refineNative", searchDefaults.refineAtNativeResolution);
		m_minimumCorners = addParam<int>("minimumCorners", int(searchDefaults.minimumCorners));

		m_report = addOutput<registration::Report>("report");
	}

	void RegisterCamerasNode::compute(flow::NodeEvaluation& evaluation) const
	{
		registration::Request request = this->request();
		request.noise.pixelSigma = detail::decimal(param(m_pixelSigma).get<float>());
		const board::DetectionRequest detection = detail::detectionRequest(
			param(m_longestSide).get<int>(), param(m_refineNative).get<bool>(), param(m_minimumCorners).get<int>());

		// A rig that cannot be made is the report's to say, like every other reason a registration
		// cannot happen, and the report records the search it would have asked for, as the method's do.
		const std::variant<Rig, registration::FailureReason> rig = this->rig(evaluation);
		if (const auto* refusal = std::get_if<registration::FailureReason>(&rig))
		{
			registration::Report refused;
			refused.reproducibility.request = request;
			std::get<registration::BoardRecord>(refused.reproducibility.method).detection = detection;
			refused.failures.push_back(*refusal);
			evaluation.output(m_report).set(std::move(refused));
			return;
		}
		const Rig& cameras = std::get<Rig>(rig);
		const board::Specification& board = evaluation.input(m_board).get<board::Specification>();
		evaluation.output(m_report).set(
			registration::board::registerCameras(cameras.cameras, cameras.groups, board, detection, request));
	}
} // namespace lain::camera
