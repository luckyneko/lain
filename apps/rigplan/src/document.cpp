#include "document.h"

#include <lain/camera/distortion.h>
#include <lain/core/length.h>
#include <lain/core/uri.h>
#include <lain/io/data/load.h>
#include <lain/io/data/save.h>
#include <lain/rigplan/serialize/rigplan.h>
#include <lain/string/format.h>

#include <utility>

namespace rigplan
{
	using lain::core::Length;
	namespace fs = std::filesystem;
	namespace rp = lain::rigplan;

	rp::RigPlan newPlan()
	{
		// The specifications the rig plan tests place their cameras with (libs/rigplan/test/testplan.h), so
		// a new plan's first camera is one whose behaviour is already known.
		rp::RigPlan plan;
		rp::SensorSpecification sensor;
		sensor.name = "hd";
		sensor.image = {1280, 720};
		sensor.pixelPitch = Length::from<Length::Micrometres>(2.0);
		sensor.principalPointTolerance = Length::from<Length::Micrometres>(20.0);
		plan.specifications.sensors.push_back(sensor);

		rp::LensSpecification lens;
		lens.name = "4mm";
		lens.focalLength = Length::from<Length::Millimetres>(4.0);
		lens.distortion = lain::camera::BrownConrady5{-0.05, 0.01, 0.0, 0.0, 0.0};
		lens.focalLengthTolerance = 0.01;
		lens.distortionTolerance = 0.1;
		plan.specifications.lenses.push_back(lens);

		plan.specifications.cameras.push_back({"hd-4mm", "hd", "4mm"});

		rp::LightSpecification spot;
		spot.name = "flood";
		spot.kind = rp::LightKind::Spot;
		plan.specifications.lights.push_back(spot);

		plan.room = rp::Room{}; // 8 x 6 x 3 m, every surface a flat grey
		return plan;
	}

	Document newDocument()
	{
		Document document;
		document.plan = newPlan();
		document.history.reset(snapshot(document.plan));
		return document;
	}

	PlanRead readPlan(const fs::path& path)
	{
		const fs::path absolute = fs::absolute(path);
		PlanRead read;
		std::optional<lain::data::Value> value = lain::io::data::load(lain::core::Uri::fromPath(absolute));
		if (!value)
		{
			read.problems.push_back(lain::string::format("{}: could not be read as a document", absolute.string()));
			return read;
		}
		rp::resolvePaths(*value, absolute.parent_path());
		rp::RigPlanRead plan = rp::rigPlanFromValue(*value);
		for (const std::string& problem : plan.problems)
			read.problems.push_back(lain::string::format("{}: {}", absolute.string(), problem));
		read.plan = std::move(plan.plan);
		return read;
	}

	DocumentRead openDocument(const fs::path& path)
	{
		PlanRead read = readPlan(path);
		DocumentRead opened;
		opened.problems = std::move(read.problems);
		if (!read.plan)
			return opened;
		Document document;
		document.plan = std::move(*read.plan);
		document.path = fs::absolute(path);
		document.history.reset(snapshot(document.plan));
		opened.document = std::move(document);
		return opened;
	}

	std::vector<std::string> saveDocument(Document& document, const fs::path& path)
	{
		const fs::path absolute = fs::absolute(path);
		lain::data::Value value = rp::rigPlanToValue(document.plan);
		rp::relativizePaths(value, absolute.parent_path());
		if (!lain::io::data::save(lain::core::Uri::fromPath(absolute), value))
			return {lain::string::format("{}: could not be written", absolute.string())};
		document.path = absolute;
		document.dirty = false;
		return {};
	}

	lain::data::Value snapshot(const rp::RigPlan& plan)
	{
		return rp::rigPlanToValue(plan);
	}

	void commit(Document& document)
	{
		if (!document.pendingSnapshot)
			return;
		document.history.push(snapshot(document.plan));
		document.pendingSnapshot = false;
	}

	// The plan a snapshot holds. A snapshot is this build's own rigPlanToValue, so it always reads back;
	// one that did not would be a bug in the document's reader or writer, and the plan is left as it was.
	static bool restore(Document& document, const lain::data::Value& state)
	{
		rp::RigPlanRead read = rp::rigPlanFromValue(state);
		if (!read.plan)
			return false;
		document.plan = std::move(*read.plan);
		document.dirty = true;
		document.pendingSnapshot = false; // the step itself is history already
		return true;
	}

	bool undo(Document& document)
	{
		commit(document); // an edit not yet recorded is a step to come back to
		if (!document.history.canUndo())
			return false;
		return restore(document, document.history.undo());
	}

	bool redo(Document& document)
	{
		if (!document.history.canRedo())
			return false;
		return restore(document, document.history.redo());
	}
} // namespace rigplan
