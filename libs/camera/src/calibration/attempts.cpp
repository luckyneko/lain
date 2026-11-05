#include "lain/camera/calibration/attempts.h"

#include <lain/string/format.h>

#include <string>

namespace lain::camera::calibration
{
	bool includes(Method selected, Method single)
	{
		return selected == single || selected == Method::BoardThenTargetless;
	}

	bool accepts(const Report& report)
	{
		return report.status == CalibrationStatus::Succeeded && report.verdict == Verdict::Ready;
	}

	std::optional<std::size_t> Attempts::accepted() const
	{
		for (std::size_t i = 0; i < reports.size(); ++i)
		{
			if (accepts(reports[i]))
				return i;
		}
		return std::nullopt;
	}

	std::string Attempts::toString() const
	{
		if (reports.empty())
			return "No attempts";
		if (const std::optional<std::size_t> position = accepted())
			return lain::string::format("Attempt {} of {} accepted: {}", *position + 1, reports.size(),
										reports[*position].toString());
		// None accepted: what each attempt was short of is the first thing a person wants to know.
		std::string text = lain::string::format("No attempt of {} accepted", reports.size());
		for (std::size_t i = 0; i < reports.size(); ++i)
			text += lain::string::format("; {}: {}", i + 1, reports[i].toString());
		return text;
	}
} // namespace lain::camera::calibration
