#include "rigplanapp.h"

#include "document.h" // readPlan: the one way a plan is read from a file

#include <lain/app/application.h>
#include <lain/camera/backends.h> // registerCameraBackends: the board renderer, where a camera plugin is built
#include <lain/io/data/codecs.h>
#include <lain/io/image/codecs.h>
#include <lain/io/mesh/codecs.h>
#include <lain/log/log.h>
#include <lain/rigplan/capture.h>
#include <lain/string/format.h>

#include <filesystem>
#include <iostream>
#include <limits>

namespace rigplan
{
	using namespace lain;
	namespace fs = std::filesystem;

	bool RigplanApp::onInit(app::Application&, app::cli::App& cli)
	{
		cli.require_subcommand(1);
		m_render = cli.add_subcommand("render", "write a rig plan's synthetic capture: its footage, its ground truth and capture.json");
		m_render->add_option("plan", m_planPath, "the rig plan document")->required();
		m_render->add_option("--out,-o", m_out, "the folder to write the capture into: new, or empty");
		// Each override is read through count(), so leaving it out keeps the rig plan's own value
		// rather than a default here standing in for it.
		m_render->add_option("--seed", m_seed, "the seed the draws and the noise grow from (the rig plan's otherwise)");
		m_render->add_option("--deviation", m_deviation, "both deviation scales, locked together: 0 is the nominal rig, 1 every tolerance in full")
			->check(app::cli::Range(0.0, 1.0));
		m_render->add_option("--noise", m_noise, "the noise scale: 0 records the light exactly, 1 is the sensor's own noise")
			->check(app::cli::Range(0.0, 1.0));
		m_render->add_option("--in-flight", m_inFlight, "frames recorded, encoded and written at once (one a pool worker otherwise): fewer holds less memory")
			->check(app::cli::Range(std::size_t{1}, std::numeric_limits<std::size_t>::max()));
		m_render->add_flag("--estimate", m_estimateOnly, "print what the capture will take on disk, and write nothing");
		return true;
	}

	bool RigplanApp::onStart(app::Application&)
	{
		return true; // headless: open no window, so run() invokes onProcess once
	}

	int RigplanApp::onProcess(app::Application& app)
	{
		io::image::registerImageCodecs();
		io::mesh::registerMeshCodecs();
		io::data::registerDataCodecs();
		camera::registerCameraBackends();

		const PlanRead read = readPlan(m_planPath);
		for (const std::string& problem : read.problems)
			log::error("{}", problem);
		if (!read.plan)
			return 1;
		const lain::rigplan::RigPlan& plan = *read.plan;
		lain::rigplan::CaptureOptions options;
		options.out = m_out;
		if (m_render->count("--seed"))
			options.seed = m_seed;
		if (m_render->count("--deviation"))
			options.deviation = m_deviation;
		if (m_render->count("--noise"))
			options.noise = m_noise;
		if (m_render->count("--in-flight"))
			options.maxInFlight = m_inFlight;
		options.generator = string::format("{} {}", app.info().name, app.info().version.toString());

		const lain::rigplan::SizeEstimate estimate = lain::rigplan::estimateSize(plan, options);
		if (!estimate.problems.empty())
		{
			for (const std::string& problem : estimate.problems)
				log::error("{}", problem);
			return 1;
		}
		std::cout << estimate.toString() << "\n";
		if (m_estimateOnly)
			return 0;
		if (m_out.empty())
		{
			log::error("render: --out is required, unless --estimate");
			return 1;
		}

		options.onFrameWritten = [](const lain::rigplan::WrittenFrame& frame)
		{ log::debug("wrote {} ({} bytes)", frame.file.string(), frame.bytes); };
		const lain::rigplan::CaptureReport report = lain::rigplan::writeCapture(plan, app.device(), options);
		for (const std::string& problem : report.problems)
			log::error("{}", problem);
		if (!report.problems.empty())
		{
			if (!report.refused)
				log::error("{} is an incomplete capture: it has no capture.json", fs::absolute(options.out).string());
			return 1;
		}
		std::cout << string::format("wrote {} frames ({} bytes) of {} cameras into {}: {:.1f} s preparing, {:.1f} s rendering "
									"({:.1f} s capturing, {:.1f} s waiting for room among {} frames in flight)\n",
									report.framesWritten, report.bytesWritten, plan.cameras.size(), fs::absolute(options.out).string(),
									report.prepareSeconds, report.renderSeconds, report.captureSeconds, report.waitSeconds, report.maxInFlight);
		return 0;
	}
} // namespace rigplan
