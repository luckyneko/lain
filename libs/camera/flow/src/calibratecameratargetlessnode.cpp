#include "lain/camera/flow/calibratecameratargetlessnode.h"

#include "decimal.h"
#include "extraction.h"
#include "identity.h"

#include <lain/camera/calibration/targetless.h>

#include <algorithm>
#include <cstdint>
#include <variant>

namespace lain::camera
{
	CalibrateCameraTargetlessNode::CalibrateCameraTargetlessNode()
		: CalibrationNode("CalibrateCameraTargetless")
	{
		declareRequest();

		// The settings' own defaults, so a fresh node asks for exactly what
		// calibration::targetless::Settings does.
		const calibration::targetless::Settings defaults;
		const feature::SequenceExtractionRequest& extraction = defaults.extraction;
		const auto* side = std::get_if<LongestSide>(&extraction.scale);
		m_longestSide = addParam<int>("longestSide", side ? int(side->pixels) : 0);
		m_sampledFrames = addParam<int>("sampledFrames", int(extraction.samples));
		m_featureCap = addParam<int>("featureCap", int(extraction.featureCap));
		m_matchSearch = addParam<feature::MatchSearch>("matchSearch", extraction.matching.search);
		m_matchRatio = addParam<float>("matchRatio", float(extraction.matching.ratio));
		m_inlierPixels = addParam<float>("inlierPixels", float(extraction.threshold));
		m_minimumPairInliers = addParam<int>("minimumPairInliers", int(extraction.minimumPairInliers));
		m_loss = addParam<refinement::LossFamily>("loss", defaults.loss.family);
		m_lossScale = addParam<float>("lossScale", float(defaults.loss.scale));
		m_freePrincipalPoint = addParam<bool>("freePrincipalPoint", defaults.freePrincipalPoint);

		m_report = addOutput<calibration::Report>("report");
	}

	calibration::targetless::Settings CalibrateCameraTargetlessNode::settings() const
	{
		calibration::targetless::Settings settings;
		feature::SequenceExtractionRequest& extraction = settings.extraction;
		detail::extractionSettings(extraction, param(m_longestSide).get<int>(), param(m_featureCap).get<int>(),
								   param(m_matchSearch).get<feature::MatchSearch>(), param(m_matchRatio).get<float>(),
								   param(m_minimumPairInliers).get<int>());
		extraction.samples = std::uint32_t(std::max(0, param(m_sampledFrames).get<int>()));
		extraction.threshold = detail::decimal(param(m_inlierPixels).get<float>());
		settings.loss.family = param(m_loss).get<refinement::LossFamily>();
		settings.loss.scale = detail::decimal(param(m_lossScale).get<float>());
		settings.freePrincipalPoint = param(m_freePrincipalPoint).get<bool>();
		// A value the method cannot use is refused there, with the reason, rather than quietly
		// corrected here.
		return settings;
	}

	void CalibrateCameraTargetlessNode::compute(flow::NodeEvaluation& evaluation) const
	{
		const media::FrameSequence& camera = footage(evaluation);
		evaluation.output(m_report).set(
			calibration::targetless::calibrate(capture::CameraFootage{detail::identityOf(camera), camera}, settings(),
											   request(evaluation)));
	}
} // namespace lain::camera
