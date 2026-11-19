#include "lain/camera/flow/nextattemptnode.h"

#include <lain/camera/calibration/attempts.h>

#include <algorithm>
#include <vector>

namespace lain::camera
{
	NextAttemptNode::NextAttemptNode()
		: AttemptReportsNode("NextAttempt")
	{
		m_included = addInput<bool>("included", flow::Default{true});
		m_run = addOutput<bool>("run");
	}

	void NextAttemptNode::compute(flow::NodeEvaluation& evaluation) const
	{
		const std::vector<const calibration::Report*> made = attempts(evaluation);
		const bool accepted = std::any_of(made.begin(), made.end(), [](const calibration::Report* report)
										  { return calibration::accepts(*report); });
		evaluation.output(m_run).set(evaluation.input(m_included).get<bool>() && !accepted);
	}
} // namespace lain::camera
