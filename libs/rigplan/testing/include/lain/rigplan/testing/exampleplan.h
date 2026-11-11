#pragma once

#include <lain/core/uri.h>
#include <lain/data/value.h>
#include <lain/io/data/load.h>
#include <lain/rigplan/rigplan.h>
#include <lain/rigplan/serialize/rigplan.h>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <optional>
#include <string>

namespace lain::rigplan::testing
{
	// A committed example rig plan, apps/rigplan/examples/<name>.json, read as `rigplan render` reads a
	// document: its paths against its folder, its shape strictly. Needs a json codec registered
	// (lain::io::data::registerDataCodecs).
	inline RigPlan examplePlan(const std::string& name)
	{
		const std::filesystem::path file = std::filesystem::path(LAIN_RIGPLAN_EXAMPLES_DIR) / (name + ".json");
		std::optional<data::Value> document = io::data::load(core::Uri::fromPath(file));
		INFO("the example rig plan " << file.string());
		REQUIRE(document.has_value());
		resolvePaths(*document, file.parent_path());
		const RigPlanRead read = rigPlanFromValue(*document);
		INFO("problems: " << read.problems.size() << (read.problems.empty() ? "" : ", the first " + read.problems.front()));
		REQUIRE(read.plan.has_value());
		return *read.plan;
	}
} // namespace lain::rigplan::testing
