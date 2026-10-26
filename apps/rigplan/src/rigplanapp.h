#pragma once

#include <lain/app/applicationdelegate.h>
#include <lain/app/cli.h>

#include <cstddef>
#include <cstdint>
#include <string>

namespace rigplan
{
	// rigplan's application delegate. Headless only for now:
	//   render <plan> --out <folder> [--seed N] [--deviation D] [--noise N] [--in-flight K] [--estimate]
	// writes the rig plan's synthetic capture (lain::rigplan::capture), after printing what it will
	// take on disk. --estimate prints that and writes nothing. --device and --threads are lain::app's.
	class RigplanApp : public lain::app::ApplicationDelegate
	{
	public:
		bool onInit(lain::app::Application& app, lain::app::cli::App& cli) override;
		bool onStart(lain::app::Application& app) override;
		int onProcess(lain::app::Application& app) override;

	private:
		lain::app::cli::App* m_render = nullptr;
		std::string m_planPath;
		std::string m_out;
		std::uint64_t m_seed = 0;
		double m_deviation = 0;
		double m_noise = 0;
		std::size_t m_inFlight = 0;
		bool m_estimateOnly = false;
	};
} // namespace rigplan
