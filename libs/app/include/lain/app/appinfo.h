#pragma once

#include <lain/core/version.h>

#include <string>

namespace lain::app
{
	// The identity an Application carries. `name` is the program name — used as the
	// CLI program name (help / usage), the Vulkan instance's application name, and the
	// startup log line. `version` is surfaced by --version, logged at startup, and
	// clamped to an acm::Version for the instance. A plain config struct, like
	// WindowSpec.
	struct AppInfo
	{
		std::string name;
		core::Version version;
	};
} // namespace lain::app
