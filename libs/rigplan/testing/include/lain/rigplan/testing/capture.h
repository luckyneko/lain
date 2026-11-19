#pragma once

#include <lain/rigplan/capture.h>
#include <lain/rigplan/realisation.h>
#include <lain/rigplan/rigplan.h>
#include <lain/rigplan/scene.h>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <utility>

namespace lain::rigplan::testing
{
	// `plan`'s synthetic capture written into `options.out` on `device`, as `rigplan render` writes one,
	// and required whole: no problem, every camera's every frame, and capture.json.
	inline void renderCapture(const RigPlan& plan, acm::Device& device, const CaptureOptions& options)
	{
		const CaptureReport written = writeCapture(plan, device, options);
		INFO("capture problems: " << written.problems.size() << (written.problems.empty() ? "" : ", the first " + written.problems.front()));
		REQUIRE(written.problems.empty());
		REQUIRE(written.framesWritten == plan.cameras.size() * plan.timeline.frames);
		REQUIRE(std::filesystem::exists(options.out / "capture.json"));
	}

	// What a capture of a rig plan was rendered from: the plan as captured (capturedPlan: its seed and
	// scales the options'), the one build of it the capture shows (realise() is deterministic in the
	// plan), and its assets, exactly as writeCapture makes them. So a test asks what a pixel of the
	// capture shows (scenecast.h) without a device.
	struct CaptureScene
	{
		RigPlan plan;
		Realisation realisation;
		SceneAssets assets;
	};

	// The same as a capture of `plan` under `options` is rendered from, required whole.
	inline CaptureScene captureScene(const RigPlan& plan, const CaptureOptions& options = {})
	{
		CaptureScene out;
		out.plan = capturedPlan(plan, options);
		RealisationResult drawn = realise(out.plan);
		INFO("realisation problems: " << drawn.diagnostics.size() << (drawn.diagnostics.empty() ? "" : ", the first " + drawn.diagnostics.front().detail));
		REQUIRE(drawn.realisation.has_value());
		out.realisation = std::move(*drawn.realisation);
		AssetsResult assets = loadSceneAssets(out.plan);
		INFO("asset problems: " << assets.problems.size() << (assets.problems.empty() ? "" : ", the first " + assets.problems.front()));
		REQUIRE(assets.problems.empty());
		out.assets = std::move(assets.assets);
		return out;
	}
} // namespace lain::rigplan::testing
