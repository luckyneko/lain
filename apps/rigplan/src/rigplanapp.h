#pragma once

#include "mainwindow.h"

#include <lain/app/applicationdelegate.h>
#include <lain/app/cli.h>

#include <cstddef>
#include <cstdint>
#include <string>

namespace rigplan
{
	// rigplan's application delegate. Two modes, by the command line:
	//   rigplan [plan] [--reset-layout] [--frames N]
	//     the editor: a window on `plan`, or else on the plan open last (the gui, M15 slices 8 and 9);
	//   rigplan render <plan> --out <folder> [--seed N] [--deviation D] [--noise N] [--in-flight K] [--estimate]
	//     headless: the rig plan's synthetic capture (lain::rigplan::capture), after printing what it
	//     will take on disk. --estimate prints that and writes nothing.
	// --device and --threads are lain::app's.
	class RigplanApp : public lain::app::ApplicationDelegate
	{
	public:
		bool onInit(lain::app::Application& app, lain::app::cli::App& cli) override;
		bool onStart(lain::app::Application& app) override;
		void onUpdate(lain::app::Application& app, const lain::app::TimeState& time, const lain::app::InputState& input) override;
		int onProcess(lain::app::Application& app) override;

		// The editor's command line: the plan to open (empty for the plan open last), and whether to start
		// from the default docks.
		const std::string& planToOpen() const { return m_openPath; }
		bool resetLayout() const { return m_resetLayout; }

	private:
		lain::app::cli::App* m_render = nullptr;
		std::string m_planPath;
		std::string m_out;
		std::uint64_t m_seed = 0;
		double m_deviation = 0;
		double m_noise = 0;
		std::size_t m_inFlight = 0;
		bool m_estimateOnly = false;

		std::string m_openPath;
		bool m_resetLayout = false;
		int m_frames = 0; // the editor: quit after N frames (0 = until closed), for a smoke

		MainWindow m_window;
	};
} // namespace rigplan
