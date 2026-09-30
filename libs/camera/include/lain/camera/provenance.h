#pragma once

#include <string>

namespace lain::camera
{
	// Which backend produced a result, for reports and reproducibility records.
	struct Provenance
	{
		std::string backend; // its registry key ("opencv")
		std::string version; // the backend library's own version
	};
} // namespace lain::camera
