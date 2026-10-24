// The ADR-0028 claim, measured (M15 slice 4c): a board rendered through a camera's own model, by the
// capture pass on a device and by texturedscene.h's ray per sub-sample on the CPU, is detected where
// lain projects the truth, through all six distortion models. Each renderer is judged against the
// truth, never against the other: the CPU averages 8-bit sRGB codes and the GPU linear light, so
// their edges differ while their corners need not. And whether the density rigplan::scene bakes a
// procedural surface at is one SIFT can see (4d). Release only, since the CPU draws 11 million rays
// a picture.

#include "gpu.h"		   // libs/render/test
#include "testboard.h"	   // libs/camera/test
#include "testcameras.h"   // libs/render/test
#include "testscene.h"	   // libs/render/test
#include "texturedscene.h" // libs/camera/test

#include <lain/camera/backends.h>
#include <lain/camera/board/detection.h>
#include <lain/camera/board/rendering.h>
#include <lain/camera/feature/features.h>
#include <lain/camera/projection.h>
#include <lain/image/convert.h>
#include <lain/image/procedural.h>
#include <lain/image/transfercurve.h>
#include <lain/render/capture.h>
#include <lain/render/renderer.h>
#include <lain/rigplan/scene.h>

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <memory>
#include <string>
#include <vector>

using namespace lain;
using namespace lain::camera;

static void ensureBackends()
{
	static const bool once = []
	{
		registerCameraBackends();
		return true;
	}();
	(void)once;
}

static CameraModel modelOf(const CameraModelParameters& parameters)
{
	const ModelResult result = CameraModel::create(parameters);
	REQUIRE(result.model.has_value());
	return *result.model;
}

static board::Rendering renderingOf(const board::Specification& spec, std::uint32_t pixelsPerSquare)
{
	const board::RenderResult result = board::render(spec.pattern(), board::RenderRequest{pixelsPerSquare, 20});
	REQUIRE(result.rendering.has_value());
	return *result.rendering;
}

// The board's centre `distance` ahead of the camera along `axis`, its face toward the camera, then
// turned by `yaw` about its own Y and `pitch` about its X, so it is seen at an angle.
static math::RigidTransformd boardPose(const board::Specification& spec, const math::Vec3d& axis, double distance, double yaw,
									   double pitch)
{
	const board::PatternParameters& p = spec.pattern().parameters();
	const double square = spec.instance().squareLength.value.metres();
	const math::Vec3d centre{p.squaresX * square / 2, p.squaresY * square / 2, 0.0};
	const math::Vec3d z = math::normalize(axis);
	const math::Vec3d x = math::normalize(math::cross(math::Vec3d{0, 1, 0}, z));
	const math::Quatd facing = math::quat_cast(math::Mat3d{x, math::cross(z, x), z});
	const math::Quatd turn = facing * math::angleAxis(yaw, math::Vec3d{0, 1, 0}) * math::angleAxis(pitch, math::Vec3d{1, 0, 0});
	return math::RigidTransformd{turn, distance * z - turn * centre};
}

// The board's raster on a sheet at `cameraFromBoard`, lit so a texel's decoded value is the light it
// sends: ambient light 1 on a white material. The sheet spans the raster from its first texel's edge
// to its last's, so texel centres sit where texturedscene.h's sheet puts them. The background is the
// CPU's grey 128, decoded.
static render::Scene boardScene(const board::Rendering& rendering, const board::Specification& spec,
								const math::RigidTransformd& cameraFromBoard)
{
	const double perMetre = rendering.request.pixelsPerSquare / spec.instance().squareLength.value.metres();
	const double margin = rendering.request.marginPixels;
	const double x0 = -margin / perMetre, x1 = (rendering.raster.width() - margin) / perMetre;
	const double y0 = -margin / perMetre, y1 = (rendering.raster.height() - margin) / perMetre;
	math::Mat4d boardFromMesh{1.0};
	boardFromMesh[0][0] = (x1 - x0) / 2;
	boardFromMesh[1][1] = (y1 - y0) / 2;
	boardFromMesh[3][0] = (x0 + x1) / 2;
	boardFromMesh[3][1] = (y0 + y1) / 2;

	render::Scene scene;
	scene.meshes = {render::testing::square()};
	scene.textures = {std::make_shared<const image::Image>(rendering.raster)};
	render::Material material;
	material.diffuse = image::ColorRGBf{1.0f, 1.0f, 1.0f};
	material.texture = 0;
	scene.materials = {material};
	render::Instance instance;
	instance.mesh = 0;
	instance.materials = {0};
	instance.worldFromMesh = cameraFromBoard.matrix() * boardFromMesh;
	scene.instances = {instance};
	scene.ambient = image::ColorRGBf{1.0f, 1.0f, 1.0f};
	const float grey = image::transferCurve(image::ColorSpace::sRGB).toLinear(128.0f / 255.0f);
	scene.background = image::ColorRGBf{grey, grey, grey};
	return scene;
}

// What `model` records of the board through the capture pass, encoded as a camera would: linear light
// to grey, through lain's sRGB curve, to 8 bits.
static image::Image captured(render::Renderer& renderer, const render::Scene& scene, const CameraModel& model,
							 const render::CaptureSettings& settings = {})
{
	render::RayTableResult table = render::RayTable::build(model, settings);
	REQUIRE(table.table.has_value());
	render::UploadResult uploaded = renderer.upload(scene);
	REQUIRE(uploaded.problems.empty());
	render::GpuRayTableResult rays = renderer.upload(*table.table);
	REQUIRE(rays.problems.empty());
	render::RenderResult picture = renderer.capture(*uploaded.scene, *rays.table, {});
	REQUIRE(picture.problems.empty());
	const image::Image grey = image::convert(picture.image, image::PixelFormat::Gray32F);
	return image::convert(image::convert(grey, image::ColorSpace::sRGB), image::PixelFormat::Gray8);
}

// What `model` records of the board on the CPU: a ray per sub-sample, 4 x 4 a pixel.
static image::Image drawn(const board::Rendering& rendering, const board::Specification& spec, const CameraModel& model,
						  const math::RigidTransformd& cameraFromBoard)
{
	synthetic::Scene scene;
	scene.board = synthetic::boardSurface(std::make_shared<const board::Rendering>(rendering),
										  spec.instance().squareLength.value.metres(), {cameraFromBoard});
	return synthetic::render(scene, model, math::RigidTransformd{}, 0, 4);
}

// How far the detected corners are from where lain projects them, and how many were found.
struct CornerError
{
	std::size_t corners = 0;
	double mean = 0;
	double worst = 0;
};

static CornerError cornerError(const image::Image& picture, const board::Specification& spec, const CameraModel& model,
							   const math::RigidTransformd& cameraFromBoard)
{
	media::FrameRef frame;
	frame.source = core::Uri{"/synthetic/capture"};
	const board::DetectionReport report = board::detect(picture, frame, spec);
	CornerError error;
	if (!report.observation)
		return error;
	for (const board::FeatureObservation& f : report.observation->features)
	{
		const math::Vec3d p = cameraFromBoard.apply(*spec.cornerPosition(f.id));
		const Projection<double> truth = project(model, p.x, p.y, p.z);
		REQUIRE(truth.ok());
		const double distance = math::length(f.pixel - math::Vec2d{truth.u, truth.v});
		error.mean += distance;
		error.worst = std::max(error.worst, distance);
		++error.corners;
	}
	if (error.corners > 0)
		error.mean /= double(error.corners);
	return error;
}

// M9's own pinhole scene (test_charuco.cpp's angled board): 960x720, fx 900 and fy 905, the board 0.35 m
// away, turned 0.35 rad about Y and -0.2 about X.
static CameraModelParameters m9Pinhole()
{
	CameraModelParameters p;
	p.image = {960, 720};
	p.intrinsics = {900.0, 905.0, 479.5, 359.5};
	p.distortion = NoDistortion{};
	return p;
}

static math::RigidTransformd m9Pose(const board::Specification& spec)
{
	return boardPose(spec, {0.0, 0.0, 1.0}, 0.35, 0.35, -0.2);
}

// Two poses a model: the board ahead, turned; and the board out toward the image's lower right
// corner, where distortion is strongest, facing the camera and turned a little. Squares about 62 px
// across where they are drawn ahead, as in M9's scene, and its raster 80 px a square, so the board is
// magnified both in the intermediate image (m = 2 doubles 62) and between the CPU's 4 x 4 rays.
struct Pose
{
	std::string name;
	math::RigidTransformd cameraFromBoard;
};

static std::vector<Pose> posesFor(const board::Specification& spec, const CameraModel& model)
{
	const double f = model.intrinsics().fx;
	const double distance = f * spec.instance().squareLength.value.metres() / 62;
	const Intrinsics& k = model.intrinsics();
	const double w = model.image().width, h = model.image().height;
	const Unprojection<double> out = unproject(model, k.cx + 0.5 * (w - 1 - k.cx), k.cy + 0.5 * (h - 1 - k.cy));
	REQUIRE(out.ok());
	return {{"ahead", boardPose(spec, {0.0, 0.0, 1.0}, distance, 0.35, -0.2)},
			{"toward the corner", boardPose(spec, {out.x, out.y, out.z}, distance, 0.2, 0.15)}};
}

TEST_CASE("M9's pinhole board, captured, is detected as accurately as M9 drew it", "[camera][capture][gpu]")
{
#ifndef NDEBUG
	SKIP("Release only: the capture is detected at full resolution");
#endif
	ensureBackends();
	acm::Device& device = render::testing::device();
	render::Renderer renderer(device);
	REQUIRE(renderer.problems().empty());
	const board::Specification spec = testing::specification();
	const board::Rendering rendering = renderingOf(spec, 80);
	const CameraModel model = modelOf(m9Pinhole());
	const math::RigidTransformd pose = m9Pose(spec);
	const CornerError error = cornerError(captured(renderer, boardScene(rendering, spec, pose), model), spec, model, pose);
	// M9 drew this scene by an OpenCV warp, 4x supersampled: mean 0.068 px, worst 0.136. Measured here
	// on Metal and the CPU device: mean 0.067, worst 0.177.
	INFO("captured: " << error.corners << " corners, mean " << error.mean << " px, worst " << error.worst << " px");
	CHECK(error.corners == 24);
	CHECK(error.mean < 0.1);
	CHECK(error.worst < 0.25);
}

TEST_CASE("a board through every model, drawn on the CPU, is detected where lain projects it", "[camera][capture]")
{
#ifndef NDEBUG
	SKIP("Release only: 11 million rays a picture");
#endif
	ensureBackends();
	const board::Specification spec = testing::specification();
	const board::Rendering rendering = renderingOf(spec, 80);
	for (int i = 0; i < render::testing::kStrongModelCount; ++i)
	{
		const CameraModel model = modelOf(render::testing::strongCamera(i, 1.5));
		for (const Pose& pose : posesFor(spec, model))
		{
			const CornerError error = cornerError(drawn(rendering, spec, model, pose.cameraFromBoard), spec, model, pose.cameraFromBoard);
			// Measured: means 0.067 (Kannala-Brandt ahead) to 0.099 (inverse Brown-Conrady toward the
			// corner), worst corners 0.17 to 0.33 (Kannala-Brandt toward the corner). 15 to 24 corners.
			INFO(displayName(model.distortion()) << ", " << pose.name << ": " << error.corners << " corners, mean " << error.mean
												 << " px, worst " << error.worst << " px");
			CHECK(error.corners >= 12);
			CHECK(error.mean < 0.1);
			CHECK(error.worst < 0.4);
		}
	}
}

TEST_CASE("a board through every model, captured, is detected where lain projects it", "[camera][capture][gpu]")
{
#ifndef NDEBUG
	SKIP("Release only: the capture is detected at full resolution");
#endif
	ensureBackends();
	acm::Device& device = render::testing::device();
	render::Renderer renderer(device);
	REQUIRE(renderer.problems().empty());
	const board::Specification spec = testing::specification();
	const board::Rendering rendering = renderingOf(spec, 80);
	for (int i = 0; i < render::testing::kStrongModelCount; ++i)
	{
		const CameraModel model = modelOf(render::testing::strongCamera(i, 1.5));
		for (const Pose& pose : posesFor(spec, model))
		{
			const image::Image picture = captured(renderer, boardScene(rendering, spec, pose.cameraFromBoard), model);
			const CornerError error = cornerError(picture, spec, model, pose.cameraFromBoard);
			// Measured, the same on Metal and the CPU device to 0.002: means 0.068 (modified Brown-Conrady
			// toward the corner) to 0.086 (inverse Brown-Conrady toward the corner), worst corners 0.15 to
			// 0.37 (inverse Brown-Conrady toward the corner, a board the frame cuts to 15 corners). The CPU
			// baseline's means are 0.067 to 0.099 over the same poses, so the capture adds nothing a
			// detector sees; each renderer's worst corner falls at a different pose.
			INFO(displayName(model.distortion()) << ", " << pose.name << ": " << error.corners << " corners, mean " << error.mean
												 << " px, worst " << error.worst << " px");
			CHECK(error.corners >= 12);
			CHECK(error.mean < 0.1);
			CHECK(error.worst < 0.4);
		}
	}
}

TEST_CASE("how the corner error follows the oversampling and the rays a pixel", "[.sweep][camera][capture][gpu]")
{
	ensureBackends();
	acm::Device& device = render::testing::device();
	render::Renderer renderer(device);
	REQUIRE(renderer.problems().empty());
	const board::Specification spec = testing::specification();
	const board::Rendering rendering = renderingOf(spec, 80);
	for (int i = 0; i < render::testing::kStrongModelCount; ++i)
	{
		const CameraModel model = modelOf(render::testing::strongCamera(i, 1.5));
		for (const Pose& pose : posesFor(spec, model))
		{
			const render::Scene scene = boardScene(rendering, spec, pose.cameraFromBoard);
			for (double m : {1.0, 1.5, 2.0, 3.0})
			{
				for (std::uint32_t s : {1u, 2u, 4u})
				{
					render::CaptureSettings settings;
					settings.oversampling = m;
					settings.samples = s;
					const CornerError error = cornerError(captured(renderer, scene, model, settings), spec, model, pose.cameraFromBoard);
					WARN(displayName(model.distortion()) << ", " << pose.name << ", m " << m << ", s " << s << ": " << error.corners
														 << " corners, mean " << error.mean << " px, worst " << error.worst << " px");
				}
			}
		}
	}
}

// --- 4d: the procedural bake density -----------------------------------------------------------

// A procedural plane, `width` x `height` metres, its corner at `cameraFromPlane`'s origin and its sides
// along that pose's X (right) and Y (down), facing the camera. On the device it is the texture baked at
// `texelsPerMetre`, sampled as rigplan::scene samples a bake: texel (i, j) is the texture at
// ((i + 0.5) / density, (j + 0.5) / density) metres from the corner, lit to send its value.
static render::Scene proceduralScene(const image::ProceduralTexture& texture, double width, double height, double texelsPerMetre,
									 const math::RigidTransformd& cameraFromPlane)
{
	const int texelsAcross = int(std::lround(width * texelsPerMetre));
	const int texelsDown = int(std::lround(height * texelsPerMetre));
	math::Mat4d planeFromMesh{1.0};
	planeFromMesh[0][0] = width / 2;
	planeFromMesh[1][1] = height / 2;
	planeFromMesh[3][0] = width / 2;
	planeFromMesh[3][1] = height / 2;

	render::Scene scene;
	scene.meshes = {render::testing::square()};
	scene.textures = {std::make_shared<const image::Image>(image::bakeProceduralTexture(texture, texelsAcross, texelsDown, texelsPerMetre))};
	render::Material material;
	material.diffuse = image::ColorRGBf{1.0f, 1.0f, 1.0f};
	material.texture = 0;
	scene.materials = {material};
	render::Instance instance;
	instance.mesh = 0;
	instance.materials = {0};
	instance.worldFromMesh = cameraFromPlane.matrix() * planeFromMesh;
	scene.instances = {instance};
	scene.ambient = image::ColorRGBf{1.0f, 1.0f, 1.0f};
	scene.background = image::ColorRGBf{128.0f / 255, 128.0f / 255, 128.0f / 255}; // texturedscene.h's, as a grey level
	return scene;
}

// A capture as grey levels, linear light times 255: the quantity texturedscene.h averages, since it
// treats the procedural texture's grey as the value a pixel records.
static image::Image greyLevels(render::Renderer& renderer, const render::Scene& scene, const CameraModel& model)
{
	render::RayTableResult table = render::RayTable::build(model);
	REQUIRE(table.table.has_value());
	render::UploadResult uploaded = renderer.upload(scene);
	REQUIRE(uploaded.problems.empty());
	render::GpuRayTableResult rays = renderer.upload(*table.table);
	REQUIRE(rays.problems.empty());
	const render::RenderResult picture = renderer.capture(*uploaded.scene, *rays.table, {});
	REQUIRE(picture.problems.empty());
	image::Image grey(picture.image.width(), picture.image.height(), image::PixelFormat::Gray8, image::ColorSpace::sRGB);
	const float* light = reinterpret_cast<const float*>(picture.image.data());
	for (std::size_t p = 0; p < std::size_t(grey.width()) * std::size_t(grey.height()); ++p)
		grey.data()[p] = std::uint8_t(std::clamp(std::lround(light[4 * p] * 255.0f), 0L, 255L));
	return grey;
}

struct GreyDifference
{
	double rms = 0;
	int worst = 0;
};

static GreyDifference difference(const image::Image& a, const image::Image& b)
{
	GreyDifference d;
	const std::size_t n = std::size_t(a.width()) * std::size_t(a.height());
	for (std::size_t p = 0; p < n; ++p)
	{
		const int e = int(a.data()[p]) - int(b.data()[p]);
		d.rms += double(e) * e;
		d.worst = std::max(d.worst, std::abs(e));
	}
	d.rms = std::sqrt(d.rms / double(n));
	return d;
}

// How many of `a`'s features `b` found too: one within `radius` px, its size within a quarter of
// a's. The rest are where the two pictures disagree enough to move a detector.
static double agreement(const feature::Features& a, const feature::Features& b, double radius)
{
	if (a.size() == 0)
		return 0;
	std::size_t found = 0;
	for (const feature::Keypoint& k : a.keypoints)
	{
		for (const feature::Keypoint& m : b.keypoints)
		{
			if (math::length(k.pixel - m.pixel) <= radius && std::abs(m.size - k.size) <= 0.25 * k.size)
			{
				++found;
				break;
			}
		}
	}
	return double(found) / double(a.size());
}

static feature::Features featuresOf(const image::Image& grey)
{
	const feature::ExtractResult result = feature::extract(grey);
	REQUIRE(result.ok());
	return result.features;
}

TEST_CASE("a procedural surface baked at rigplan's density captures as it does at twice it", "[camera][capture][gpu]")
{
#ifndef NDEBUG
	SKIP("Release only: the CPU draws the texture exactly, 11 million rays a picture");
#endif
	ensureBackends();
	acm::Device& device = render::testing::device();
	render::Renderer renderer(device);
	REQUIRE(renderer.problems().empty());

	// rigplan::scene's default texture and its bake density, against twice it and against the texture
	// drawn exactly. Near, a camera pixel is a fortieth of the finest noise cell and the bake is
	// magnified, which is where its density shows; far, five times as far, a pixel is an eighth of a cell.
	const image::ProceduralTexture texture{11, 0.04};
	const double density = rigplan::kBakedTexelsPerCell / texture.cell;
	struct Setting
	{
		std::string name;
		int model;
		double distance;
		double width, height;
	};
	const std::vector<Setting> settings = {{"pinhole, near", 0, 0.6, 1.2, 1.0},
										   {"pinhole, far", 0, 3.0, 6.0, 4.5},
										   {"Brown-Conrady, near", 1, 0.6, 1.4, 1.1},
										   {"Brown-Conrady, far", 1, 3.0, 7.0, 5.5}};
	for (const Setting& setting : settings)
	{
		const CameraModel model = modelOf(render::testing::strongCamera(setting.model, 1.5));
		const math::Quatd turn = math::angleAxis(0.3, math::Vec3d{0, 1, 0});
		const math::Vec3d centre{0.0, 0.0, setting.distance};
		const math::RigidTransformd cameraFromPlane{turn, centre - turn * math::Vec3d{setting.width / 2, setting.height / 2, 0.0}};

		const image::Image baked = greyLevels(renderer, proceduralScene(texture, setting.width, setting.height, density, cameraFromPlane), model);
		const image::Image finer =
			greyLevels(renderer, proceduralScene(texture, setting.width, setting.height, 2 * density, cameraFromPlane), model);
		// The CPU's exact drawing does not depend on the device, so it is drawn once a setting.
		static std::map<std::string, image::Image> exact;
		if (exact.count(setting.name) == 0)
		{
			synthetic::Scene cpu;
			cpu.planes.push_back({cameraFromPlane.translation(), cameraFromPlane.rotate(math::Vec3d{1, 0, 0}),
								  cameraFromPlane.rotate(math::Vec3d{0, 1, 0}), setting.width, setting.height, texture});
			exact.emplace(setting.name, synthetic::render(cpu, model, math::RigidTransformd{}, 0, 4));
		}
		const image::Image& drawnExactly = exact.at(setting.name);

		const GreyDifference bakedToFiner = difference(baked, finer);
		const GreyDifference bakedToExact = difference(baked, drawnExactly);
		const GreyDifference finerToExact = difference(finer, drawnExactly);
		const feature::Features fBaked = featuresOf(baked), fFiner = featuresOf(finer), fExact = featuresOf(drawnExactly);
		const double bakedAgrees = agreement(fBaked, fExact, 0.5), finerAgrees = agreement(fFiner, fExact, 0.5);
		INFO(setting.name << ": grey levels baked to finer rms " << bakedToFiner.rms << " worst " << bakedToFiner.worst
						  << "; baked to exact rms " << bakedToExact.rms << "; finer to exact rms " << finerToExact.rms << ". Features "
						  << fBaked.size() << ", " << fFiner.size() << ", " << fExact.size() << "; baked found in finer "
						  << agreement(fBaked, fFiner, 0.5) << "; found in exact: baked " << bakedAgrees << ", finer " << finerAgrees);
		// Measured on Metal and the CPU device: baked to finer 0.31 to 0.47 grey levels rms, 85 to 100% of the
		// baked capture's features within 0.5 px in the finer; to the exact texture 0.57 to 1.0 rms baked and
		// 0.45 to 1.08 finer; and the share of features the exact texture also finds is 0.95 to 0.98 baked,
		// 0.94 to 1.0 finer. So the density is not what parts the capture from the texture: what does is the
		// 8-bit sRGB texture store and the averaging of light rather than grey codes, which doubling it
		// leaves alone. Near, a pixel being a fortieth of a cell, SIFT finds only 34 to 55 features.
		CHECK(fBaked.size() > 30);
		CHECK(bakedToFiner.rms < 1.0);
		CHECK(agreement(fBaked, fFiner, 0.5) > 0.8);
		CHECK(bakedToExact.rms < 1.5);
		CHECK(std::abs(bakedAgrees - finerAgrees) < 0.06);
	}
}
