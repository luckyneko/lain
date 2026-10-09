#include "lain/camera/flow/rigregistrationnode.h"

#include "decimal.h"

#include <lain/media/framesequence.h>

#include <algorithm>
#include <cstdint>
#include <string>

namespace lain::camera
{
	RigRegistrationNode::RigRegistrationNode(std::string name)
		: Node(std::move(name))
	{
		m_footage = addInput<std::vector<media::FrameSequence>>("footage");
		m_models = addInput<std::vector<CameraModel>>("models");
	}

	void RigRegistrationNode::declareRequest()
	{
		// The request's own defaults, so a fresh node asks for exactly what registration::Request does.
		const registration::Request defaults;
		m_reference = addParam<std::string>("reference", std::string{});
		m_unknownApplicability =
			addParam<registration::ApplicabilityPolicy>("unknownApplicability", defaults.unknownApplicability);
		m_loss = addParam<registration::LossFamily>("loss", defaults.loss.family);
		m_lossScale = addParam<float>("lossScale", float(defaults.loss.scale));
		m_fitnessProfile = addParam<std::string>("fitnessProfile", defaults.fitnessProfile);
		m_heldOutFraction = addParam<float>("heldOutFraction", float(defaults.heldOutFraction));
		m_resamples = addParam<int>("resamples", int(defaults.resamples));
		m_seed = addParam<int>("seed", int(defaults.seed));
		m_maximumIterations = addParam<int>("maximumIterations", int(defaults.maximumIterations));
		m_deterministic = addParam<bool>("deterministic", false);
	}

	registration::Request RigRegistrationNode::request() const
	{
		registration::Request request;
		const std::string reference = param(m_reference).get<std::string>();
		if (!reference.empty())
			request.reference = capture::CameraIdentity{reference};
		request.unknownApplicability = param(m_unknownApplicability).get<registration::ApplicabilityPolicy>();
		request.loss.family = param(m_loss).get<registration::LossFamily>();
		request.loss.scale = detail::decimal(param(m_lossScale).get<float>());
		request.fitnessProfile = param(m_fitnessProfile).get<std::string>();
		request.heldOutFraction = detail::decimal(param(m_heldOutFraction).get<float>());
		request.resamples = std::uint32_t(std::max(0, param(m_resamples).get<int>()));
		// A seed is an identity, not a quantity: a negative int keeps its bits rather than clamping.
		request.seed = std::uint64_t(std::uint32_t(param(m_seed).get<int>()));
		request.maximumIterations = std::uint32_t(std::max(0, param(m_maximumIterations).get<int>()));
		request.execution = param(m_deterministic).get<bool>() ? ExecutionPolicy::DeterministicDebug : ExecutionPolicy::Normal;
		return request;
	}

	std::variant<RigRegistrationNode::Rig, registration::FailureReason>
	RigRegistrationNode::rig(const flow::NodeEvaluation& evaluation) const
	{
		const auto& footage = evaluation.input(m_footage).get<std::vector<media::FrameSequence>>();
		const auto& models = evaluation.input(m_models).get<std::vector<CameraModel>>();
		if (footage.size() != models.size())
		{
			return registration::FailureReason{registration::Failure::InvalidDataset,
											   std::to_string(footage.size()) + " cameras' footage and " +
												   std::to_string(models.size()) + " camera models: they pair by position"};
		}

		// Each camera named by its footage's source, and grouped by position.
		Rig rig;
		std::vector<capture::CameraFootage> byPosition;
		for (std::size_t c = 0; c < footage.size(); ++c)
		{
			const std::string source = footage[c].size() > 0 ? footage[c].frame(0).source.toString() : std::string{};
			rig.cameras.push_back({capture::CameraIdentity{source}, models[c], footage[c]});
			byPosition.push_back({capture::CameraIdentity{source}, footage[c]});
		}
		capture::GroupingResult grouping = capture::groupsByPosition(byPosition);
		if (!grouping.diagnostics.empty())
			return registration::FailureReason{registration::Failure::InvalidDataset, grouping.diagnostics.front().detail};
		rig.groups = std::move(grouping.groups);
		return rig;
	}
} // namespace lain::camera
