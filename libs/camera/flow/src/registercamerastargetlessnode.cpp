#include "lain/camera/flow/registercamerastargetlessnode.h"

#include "decimal.h"

#include <lain/camera/registration/targetless.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <variant>

namespace lain::camera
{
	RegisterCamerasTargetlessNode::RegisterCamerasTargetlessNode()
		: RigRegistrationNode("RegisterCamerasTargetless")
	{
		declareRequest();

		// The extraction request's own defaults, so a fresh node asks for exactly what
		// feature::ExtractionRequest does.
		const feature::ExtractionRequest defaults;
		const auto* side = std::get_if<LongestSide>(&defaults.scale);
		m_longestSide = addParam<int>("longestSide", side ? int(side->pixels) : 0);
		m_sampledGroups = addParam<int>("sampledGroups", int(defaults.samples));
		m_featureCap = addParam<int>("featureCap", int(defaults.featureCap));
		m_matchSearch = addParam<feature::MatchSearch>("matchSearch", defaults.matching.search);
		m_matchRatio = addParam<float>("matchRatio", float(defaults.matching.ratio));
		m_inlierAngleMrad = addParam<float>("inlierAngleMrad", float(defaults.geometry.angle * 1000.0));
		m_minimumPairInliers = addParam<int>("minimumPairInliers", int(defaults.minimumPairInliers));

		m_report = addOutput<registration::Report>("report");
	}

	feature::ExtractionRequest RegisterCamerasTargetlessNode::extraction() const
	{
		feature::ExtractionRequest request;
		const int longestSide = param(m_longestSide).get<int>();
		if (longestSide > 0)
			request.scale = LongestSide{std::uint32_t(longestSide)};
		else
			request.scale = NativeScale{};
		request.samples = std::uint32_t(std::max(0, param(m_sampledGroups).get<int>()));
		request.featureCap = std::uint32_t(std::max(0, param(m_featureCap).get<int>()));
		request.matching.search = param(m_matchSearch).get<feature::MatchSearch>();
		request.matching.ratio = detail::decimal(param(m_matchRatio).get<float>());
		// Milliradians as the decimal typed, to a millionth of one, in radians: 4 is exactly 0.004.
		// A whole number of nanoradians over 1e9 is the double nearest that decimal. Dividing
		// detail::decimal's answer by 1000 rounds twice, and misses it for a quarter of the values
		// (50,390 of the 200,000 from 0.000001 to 0.2 mrad, measured).
		request.geometry.angle = std::round(double(param(m_inlierAngleMrad).get<float>()) * 1e6) / 1e9;
		request.minimumPairInliers = std::uint32_t(std::max(0, param(m_minimumPairInliers).get<int>()));
		// A value the method cannot use is refused there, with the reason (an ExtractionFailed report),
		// rather than quietly corrected here.
		return request;
	}

	void RegisterCamerasTargetlessNode::compute(flow::NodeEvaluation& evaluation) const
	{
		const registration::Request request = this->request();
		// The registration's execution policy governs the extraction too; the method records it so.
		feature::ExtractionRequest extraction = this->extraction();
		extraction.execution = request.execution;

		// A rig that cannot be made is the report's to say, like every other reason a registration
		// cannot happen. A default Report is a board one, so the refusal says which method it was,
		// with what it would have extracted with, as the method's own refusals do.
		const std::variant<Rig, registration::FailureReason> rig = this->rig(evaluation);
		if (const auto* refusal = std::get_if<registration::FailureReason>(&rig))
		{
			registration::Report refused;
			refused.reproducibility.request = request;
			refused.diagnostics.method = registration::TargetlessDiagnostics{};
			registration::TargetlessRecord record;
			record.extraction = extraction;
			refused.reproducibility.method = record;
			refused.failures.push_back(*refusal);
			evaluation.output(m_report).set(std::move(refused));
			return;
		}
		const Rig& cameras = std::get<Rig>(rig);
		evaluation.output(m_report).set(
			registration::targetless::registerCameras(cameras.cameras, cameras.groups, extraction, request));
	}
} // namespace lain::camera
