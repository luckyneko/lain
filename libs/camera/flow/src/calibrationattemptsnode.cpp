#include "lain/camera/flow/calibrationattemptsnode.h"

#include <lain/camera/calibration/attempts.h>

#include <optional>
#include <utility>

namespace lain::camera
{
	CalibrationAttemptsNode::CalibrationAttemptsNode()
		: AttemptReportsNode("CalibrationAttempts")
	{
		m_attempts = addOutput<calibration::Attempts>("attempts");
		m_accepted = addOutput<calibration::Report>("accepted");
	}

	void CalibrationAttemptsNode::compute(flow::NodeEvaluation& evaluation) const
	{
		calibration::Attempts made;
		for (const calibration::Report* report : attempts(evaluation))
			made.reports.push_back(*report);
		// An output this does not write keeps its previous value, so each is cleared explicitly: a
		// retained evaluation that had accepted must not go on offering that model.
		if (made.reports.empty())
		{
			evaluation.output(m_attempts).clear();
			evaluation.output(m_accepted).clear();
			return;
		}
		if (const std::optional<std::size_t> position = made.accepted())
			evaluation.output(m_accepted).set(made.reports[*position]);
		else
			evaluation.output(m_accepted).clear();
		evaluation.output(m_attempts).set(std::move(made));
	}
} // namespace lain::camera
