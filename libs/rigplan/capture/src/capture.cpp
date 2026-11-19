#include "lain/rigplan/capture.h"

#include "outputs.h"

#include <lain/camera/serialize/cameramodel.h>
#include <lain/camera/serialize/registeredrig.h>
#include <lain/core/uri.h>
#include <lain/io/data/save.h>
#include <lain/io/framepattern.h>
#include <lain/io/image/save.h>
#include <lain/io/transport.h>
#include <lain/meta/enums.h>
#include <lain/render/renderer.h>
#include <lain/rigplan/scene.h>
#include <lain/rigplan/serialize/capture.h>
#include <lain/rigplan/serialize/truth.h>
#include <lain/rigplan/truth.h>
#include <lain/string/format.h>
#include <lain/string/pattern.h>
#include <lain/task/task.h>

#include <archimedes/acmDevice.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <exception>
#include <system_error>
#include <utility>

namespace lain::rigplan
{
	namespace fs = std::filesystem;
	using Clock = std::chrono::steady_clock;

	RigPlan capturedPlan(const RigPlan& plan, const CaptureOptions& options)
	{
		RigPlan captured = plan;
		if (options.seed)
			captured.seed = *options.seed;
		if (options.deviation)
		{
			captured.scales.intrinsic = *options.deviation;
			captured.scales.installation = *options.deviation;
			captured.scales.locked = true;
		}
		if (options.noise)
			captured.scales.noise = *options.noise;
		return captured;
	}

	static double secondsSince(Clock::time_point start)
	{
		return std::chrono::duration<double>(Clock::now() - start).count();
	}

	// Why the capture cannot go into `out`: it is not a folder, or it is a folder with something in it.
	static std::vector<std::string> folderProblems(const fs::path& out)
	{
		if (out.empty())
			return {"no folder to write the capture into"};
		std::error_code ec;
		if (!fs::exists(out, ec))
			return {};
		if (!fs::is_directory(out, ec))
			return {string::format("'{}' exists and is not a folder", out.string())};
		if (!fs::is_empty(out, ec))
			return {string::format("'{}' is not empty: a capture is written into a new or empty folder, never over another", out.string())};
		return {};
	}

	// Why the cameras' names would not bind back as M9 binds a rig: a name a rig folder's listing hides,
	// or two names that sort one way as folders (rig/) and the other way as documents (nominal/,
	// truth/models/), since M9 pairs a rig's footage with its models by sorted position.
	static std::vector<std::string> namingProblems(const RigPlan& plan)
	{
		std::vector<std::string> problems;
		for (const PlacedCamera& camera : plan.cameras)
		{
			if (!camera.name.empty() && camera.name.front() == '.')
				problems.push_back(string::format("camera '{}': a name starting with '.' is hidden from a rig folder's listing", camera.name));
		}
		for (std::size_t i = 0; i < plan.cameras.size(); ++i)
		{
			for (std::size_t j = i + 1; j < plan.cameras.size(); ++j)
			{
				const std::string& a = plan.cameras[i].name;
				const std::string& b = plan.cameras[j].name;
				const bool asFolders = fs::path(a) < fs::path(b);
				const bool asDocuments = fs::path(a + ".json") < fs::path(b + ".json");
				if (asFolders != asDocuments)
					problems.push_back(string::format("cameras '{}' and '{}' sort one way as folders and the other way as documents, so "
													  "a rig's footage and models would pair wrongly (M9 pairs them by sorted position); "
													  "rename one",
													  a, b));
			}
		}
		return problems;
	}

	// The device that renders, as capture.json names it: "hardware" or "cpu", as --device does.
	static std::string deviceKind(const acm::Device& device)
	{
		return device.deviceInfo().type == acm::PhysicalDeviceType::Cpu ? "cpu" : "hardware";
	}

	// A camera's frame file name: "frame.<frame:0N>.<ext>", N the digits of the last frame, at least 4.
	static std::string framePattern(std::size_t frames, const std::string& extension)
	{
		std::size_t digits = 1;
		for (std::size_t last = frames == 0 ? 0 : frames - 1; last >= 10; last /= 10)
			++digits;
		return string::format("frame.<{}:0{}>.{}", io::frameKey, std::max<std::size_t>(digits, 4), extension);
	}

	// Writes `value` to `path` as a document, or says why not.
	static void saveDocument(const fs::path& path, const data::Value& value, std::vector<std::string>& problems)
	{
		if (!io::data::save(core::Uri::fromPath(path), value))
			problems.push_back(string::format("could not write '{}'", path.string()));
	}

	// The capture record of one camera: a synthetic device, its stream, and its nominal model as the
	// imported manufacturer model.
	static camera::CaptureRecord captureRecord(const RigPlan& plan, const detail::OutputCamera& camera,
											   const camera::CameraModel& nominal)
	{
		camera::CaptureRecord record;
		record.device = {"synthetic", camera.specification->name, camera.placed->id.toString()};
		const camera::ImageGeometry& image = camera.sensor->image;
		record.stream.width = image.width;
		record.stream.height = image.height;
		record.stream.format = std::string(meta::enums::name(camera.recorder->pixelFormat()));
		if (plan.timeline.rate.specified())
			record.stream.framesPerSecond = plan.timeline.rate.hz();
		record.properties = {{"camera", camera.specification->name}, {"sensor", camera.sensor->name}, {"lens", camera.lens->name}};
		record.importedModel = camera::ImportedModel{string::format("the nominal model of camera specification '{}'", camera.specification->name),
													 nominal.parameters()};
		return record;
	}

	CaptureReport writeCapture(const RigPlan& source, acm::Device& device, const CaptureOptions& options)
	{
		CaptureReport report;
		report.refused = true;
		std::vector<std::string>& problems = report.problems;
		const Clock::time_point preparing = Clock::now();
		const RigPlan plan = capturedPlan(source, options);

		// Absolute from here on: the manifest's rig plan is written relative to it (ADR-0027), and a
		// folder spelled relative to the working directory would name nothing to a reader elsewhere.
		const fs::path out = options.out.empty() ? fs::path{} : fs::absolute(options.out);

		// 1. Everything that can refuse, before anything is written.
		problems = folderProblems(out);
		if (options.maxInFlight && *options.maxInFlight == 0)
			problems.push_back("maxInFlight is 0: no frame could ever be written");
		report.maxInFlight = options.maxInFlight.value_or(std::max<std::size_t>(task::threadCount(), 1));
		for (const PlanDiagnostic& diagnostic : check(plan))
			problems.push_back(diagnostic.detail);
		if (!problems.empty())
			return report;
		const RealisationResult drawn = realise(plan);
		for (const RealisationDiagnostic& diagnostic : drawn.diagnostics)
			problems.push_back(diagnostic.detail);
		if (!drawn.realisation)
			return report;
		const Realisation& realisation = *drawn.realisation;

		for (std::string& problem : namingProblems(plan))
			problems.push_back(std::move(problem));
		if (!io::data::writerRegistry().contains("json"))
			problems.push_back("this build has no json writer, so the truth and the manifest cannot be written");
		std::vector<detail::OutputCamera> cameras = detail::outputCameras(plan, problems);

		AssetsResult assets = loadSceneAssets(plan);
		for (std::string& problem : assets.problems)
			problems.push_back(std::move(problem));
		// Every frame's scene, built once: they share every asset by pointer, so each holds only what
		// placed the assets at its frame.
		std::vector<render::Scene> scenes;
		if (assets.problems.empty())
		{
			for (std::size_t frame = 0; frame < plan.timeline.frames; ++frame)
			{
				SceneResult scene = renderScene(plan, realisation, assets.assets, frame);
				for (const std::string& problem : scene.problems)
					problems.push_back(string::format("frame {}: {}", frame, problem));
				scenes.push_back(std::move(scene.scene));
			}
		}
		// Every camera's ray table, built once here to be sure it can be and dropped again: a table is
		// large (a direction per pixel corner) and a capture holds one at a time, so it is built again
		// when its camera renders. A refusal found an hour into a capture is a capture lost.
		for (const RealisedCamera& camera : realisation.cameras)
		{
			for (const std::string& problem : render::RayTable::build(camera.model, options.settings).problems)
				problems.push_back(string::format("camera '{}': {}", camera.name, problem));
		}
		render::Renderer renderer(device);
		for (const std::string& problem : renderer.problems())
			problems.push_back(problem);
		if (!problems.empty())
			return report;
		report.refused = false;

		// 2. The truth and the nominal models: no GPU, the same on every machine.
		std::error_code ec;
		for (const fs::path& folder : {out / "nominal", out / "truth" / "models"})
		{
			fs::create_directories(folder, ec);
			if (ec)
				problems.push_back(string::format("could not create '{}': {}", folder.string(), ec.message()));
		}
		for (const detail::OutputCamera& camera : cameras)
		{
			fs::create_directories(out / "rig" / camera.placed->name, ec);
			if (ec)
				problems.push_back(string::format("could not create '{}': {}", (out / "rig" / camera.placed->name).string(), ec.message()));
		}
		if (!problems.empty())
			return report;
		std::vector<camera::CameraModel> nominals;
		for (std::size_t c = 0; c < cameras.size(); ++c)
		{
			const std::string& name = cameras[c].placed->name;
			nominals.push_back(*nominalModel(*cameras[c].sensor, *cameras[c].lens).model);
			saveDocument(out / "nominal" / (name + ".json"), camera::cameraModelToValue(nominals.back()), problems);
			saveDocument(out / "truth" / "models" / (name + ".json"), camera::cameraModelToValue(realisation.cameras[c].model), problems);
		}
		saveDocument(out / "truth" / "poses.json", posesToValue(cameraPoses(realisation)), problems);
		saveDocument(out / "truth" / "rig.json", camera::registration::registeredRigToValue(truthRig(plan, realisation)), problems);
		std::vector<BoardTruth> boards;
		for (std::size_t frame = 0; frame < plan.timeline.frames; ++frame)
		{
			for (BoardTruth& board : boardTruth(plan, realisation, frame))
				boards.push_back(std::move(board));
		}
		saveDocument(out / "truth" / "board.json", boardTruthToValue(boards), problems);
		if (!problems.empty())
			return report;
		report.prepareSeconds = secondsSince(preparing);

		// 3. Camera by camera: one ray table on the device at a time, the frames' scenes updated into one
		// GpuScene, and each frame's light handed to the pool, at most maxInFlight at once.
		const Clock::time_point rendering = Clock::now();
		std::atomic<std::size_t> inFlight{0};
		std::atomic<std::size_t> framesWritten{0};
		std::atomic<std::uint64_t> bytesWritten{0};
		std::atomic<bool> writeFailed{false}; // so rendering stops once a frame could not be written
		std::vector<task::Handle<std::string>> pending;
		std::optional<render::GpuScene> scene;
		const double noiseScale = plan.scales.noise;
		for (std::size_t c = 0; c < cameras.size() && problems.empty() && !writeFailed; ++c)
		{
			const detail::OutputCamera& camera = cameras[c];
			const RealisedCamera& realised = realisation.cameras[c];
			render::RayTableResult table = render::RayTable::build(realised.model, options.settings);
			std::optional<render::GpuRayTable> rays;
			if (table.table)
			{
				render::GpuRayTableResult uploaded = renderer.upload(*table.table);
				rays = std::move(uploaded.table);
				for (const std::string& problem : uploaded.problems)
					problems.push_back(string::format("camera '{}': {}", realised.name, problem));
			}
			for (const std::string& problem : table.problems)
				problems.push_back(string::format("camera '{}': {}", realised.name, problem));
			report.peakRayTables = std::max<std::size_t>(report.peakRayTables, rays ? 1 : 0);
			const string::Pattern pattern(framePattern(plan.timeline.frames, camera.formatKey));
			const fs::path folder = out / "rig" / realised.name;
			io::image::ImageWriterOptions writing;
			writing.quality = camera.sensor->sensorModel.output.quality;

			for (std::size_t frame = 0; frame < plan.timeline.frames && problems.empty() && !writeFailed; ++frame)
			{
				if (!scene)
				{
					render::UploadResult uploaded = renderer.upload(scenes[frame]);
					scene = std::move(uploaded.scene);
					problems = std::move(uploaded.problems);
				}
				else
				{
					problems = renderer.update(*scene, scenes[frame]).problems;
				}
				if (!problems.empty())
					break;
				const Clock::time_point capturing = Clock::now();
				render::RenderResult light = renderer.capture(*scene, *rays, {realised.planFromCamera});
				report.captureSeconds += secondsSince(capturing);
				for (const std::string& problem : light.problems)
					problems.push_back(string::format("camera '{}' at frame {}: {}", realised.name, frame, problem));
				if (!problems.empty())
					break;

				// Back-pressure: the device renders the next frame only once there is room for it.
				const Clock::time_point waiting = Clock::now();
				task::waitUntil([&]
								{ return inFlight.load() < report.maxInFlight; });
				report.waitSeconds += secondsSince(waiting);
				report.peakInFlight = std::max(report.peakInFlight, ++inFlight);
				const fs::path file = folder / *pattern.format(string::Dictionary{}.set(io::frameKey, frame));
				const NoiseKey key{plan.seed, realised.id, frame};
				pending.push_back(task::async(
					[&, &recorder = *camera.recorder, &formatKey = camera.formatKey, writing, key, file, picture = std::move(light.image),
					 name = realised.name]() mutable -> std::string
					{
						std::string problem;
						try
						{
							const image::Image recorded = recorder.record(picture, key, noiseScale);
							picture = image::Image{}; // the light is four times the recording: let it go before encoding

							const std::optional<memory::Buffer> bytes = io::image::encode(formatKey, recorded, writing);
							if (!bytes)
								problem = string::format("camera '{}' at frame {}: {} would not encode it", name, key.frame, formatKey);
							else if (!io::write(core::Uri::fromPath(file), *bytes))
								problem = string::format("could not write '{}'", file.string());
							else
							{
								++framesWritten;
								bytesWritten += bytes->size();
								if (options.onFrameWritten)
									options.onFrameWritten({name, key.frame, file, bytes->size()});
							}
						}
						catch (const std::exception& e)
						{
							problem = string::format("camera '{}' at frame {}: {}", name, key.frame, e.what());
						}
						if (!problem.empty())
							writeFailed = true;
						--inFlight;
						return problem;
					}));
			}
		}
		// Every frame handed to the pool is waited for, even after a failure: each references this frame.
		for (task::Handle<std::string>& handle : pending)
		{
			task::waitAll(handle);
			std::string problem;
			handle.get(&problem);
			if (!problem.empty())
				problems.push_back(std::move(problem));
		}
		report.framesWritten = framesWritten.load();
		report.bytesWritten = bytesWritten.load();
		report.renderSeconds = secondsSince(rendering);
		if (!problems.empty())
			return report;

		// 4. The manifest, last: a capture folder with one is whole.
		CaptureManifest manifest;
		manifest.generator = options.generator;
		manifest.device = device.deviceInfo().name;
		manifest.deviceKind = deviceKind(device);
		manifest.plan = plan;
		for (std::size_t c = 0; c < cameras.size(); ++c)
			manifest.cameras.push_back({cameras[c].placed->id, cameras[c].placed->name, cameras[c].sensor->sensorModel.output,
										captureRecord(plan, cameras[c], nominals[c])});
		saveDocument(out / "capture.json", captureManifestToValue(manifest, out), problems);
		return report;
	}
} // namespace lain::rigplan
