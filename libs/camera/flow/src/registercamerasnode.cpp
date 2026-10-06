#include "lain/camera/flow/registercamerasnode.h"

#include "decimal.h"
#include "detection.h"

#include <lain/camera/capture/capturegroup.h>
#include <lain/camera/registration/board.h>
#include <lain/camera/registration/request.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <variant>

namespace lain::camera
{
	RegisterCamerasNode::RegisterCamerasNode()
		: Node("RegisterCameras")
	{
		m_footage = addInput<std::vector<media::FrameSequence>>("footage");
		m_models = addInput<std::vector<CameraModel>>("models");
		m_board = addInput<board::Specification>("board");

		// The request's own defaults, so a fresh node asks for exactly what registration::Request and
		// board::DetectionRequest do.
		const registration::Request defaults;
		const board::DetectionRequest searchDefaults;
		m_reference = addParam<std::string>("reference", std::string{});
		m_unknownApplicability =
			addParam<registration::ApplicabilityPolicy>("unknownApplicability", defaults.unknownApplicability);
		m_pixelSigma = addParam<float>("pixelSigma", float(defaults.noise.pixelSigma));
		m_loss = addParam<registration::LossFamily>("loss", defaults.loss.family);
		m_lossScale = addParam<float>("lossScale", float(defaults.loss.scale));
		m_fitnessProfile = addParam<std::string>("fitnessProfile", defaults.fitnessProfile);
		m_heldOutFraction = addParam<float>("heldOutFraction", float(defaults.heldOutFraction));
		m_resamples = addParam<int>("resamples", int(defaults.resamples));
		m_seed = addParam<int>("seed", int(defaults.seed));
		m_maximumIterations = addParam<int>("maximumIterations", int(defaults.maximumIterations));
		m_deterministic = addParam<bool>("deterministic", false);
		m_longestSide = addParam<int>("longestSide", 0);
		m_refineNative = addParam<bool>("refineNative", searchDefaults.refineAtNativeResolution);
		m_minimumCorners = addParam<int>("minimumCorners", int(searchDefaults.minimumCorners));

		m_report = addOutput<registration::Report>("report");
	}

	void RegisterCamerasNode::compute(flow::NodeEvaluation& evaluation) const
	{
		registration::Request request;
		const std::string reference = param(m_reference).get<std::string>();
		if (!reference.empty())
			request.reference = capture::CameraIdentity{reference};
		request.unknownApplicability = param(m_unknownApplicability).get<registration::ApplicabilityPolicy>();
		request.noise.pixelSigma = detail::decimal(param(m_pixelSigma).get<float>());
		request.loss.family = param(m_loss).get<registration::LossFamily>();
		request.loss.scale = detail::decimal(param(m_lossScale).get<float>());
		request.fitnessProfile = param(m_fitnessProfile).get<std::string>();
		request.heldOutFraction = detail::decimal(param(m_heldOutFraction).get<float>());
		request.resamples = std::uint32_t(std::max(0, param(m_resamples).get<int>()));
		// A seed is an identity, not a quantity: a negative int keeps its bits rather than clamping.
		request.seed = std::uint64_t(std::uint32_t(param(m_seed).get<int>()));
		request.maximumIterations = std::uint32_t(std::max(0, param(m_maximumIterations).get<int>()));
		request.execution = param(m_deterministic).get<bool>() ? ExecutionPolicy::DeterministicDebug : ExecutionPolicy::Normal;
		const board::DetectionRequest detection = detail::detectionRequest(
			param(m_longestSide).get<int>(), param(m_refineNative).get<bool>(), param(m_minimumCorners).get<int>());

		const auto& footage = evaluation.input(m_footage).get<std::vector<media::FrameSequence>>();
		const auto& models = evaluation.input(m_models).get<std::vector<CameraModel>>();
		const board::Specification& board = evaluation.input(m_board).get<board::Specification>();

		// A mismatch is the report's to say, like every other reason a registration cannot happen.
		registration::Report refused;
		refused.reproducibility.request = request;
		std::get<registration::BoardRecord>(refused.reproducibility.method).detection = detection;
		if (footage.size() != models.size())
		{
			refused.failures.push_back({registration::Failure::InvalidDataset,
										std::to_string(footage.size()) + " cameras' footage and " +
											std::to_string(models.size()) + " camera models: they pair by position"});
			evaluation.output(m_report).set(std::move(refused));
			return;
		}

		// Each camera named by its footage's source, and grouped by position.
		std::vector<registration::board::RigFootage> cameras;
		std::vector<capture::CameraFootage> byPosition;
		for (std::size_t c = 0; c < footage.size(); ++c)
		{
			const std::string source = footage[c].size() > 0 ? footage[c].frame(0).source.toString() : std::string{};
			cameras.push_back({capture::CameraIdentity{source}, models[c], footage[c]});
			byPosition.push_back({capture::CameraIdentity{source}, footage[c]});
		}
		const capture::GroupingResult grouping = capture::groupsByPosition(byPosition);
		if (!grouping.diagnostics.empty())
		{
			refused.failures.push_back({registration::Failure::InvalidDataset, grouping.diagnostics.front().detail});
			evaluation.output(m_report).set(std::move(refused));
			return;
		}
		evaluation.output(m_report).set(registration::board::registerCameras(cameras, grouping.groups, board, detection, request));
	}
} // namespace lain::camera
