// The capture pass's ray table (ADR-0028), on the CPU: lain's unprojection at every pixel corner, how
// well a blend of four of them stands for the model, and the faces and tiles of an intermediate image
// every sub-sample can be remapped from. Each case is judged against lain's own camera geometry.

#include "testcameras.h"

#include <lain/camera/projection.h>
#include <lain/render/capture.h>
#include <lain/testing/threadpool.h>

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

using namespace lain;
using namespace lain::render;

static constexpr std::array<FaceAxis, 5> kAxes = {FaceAxis::Front, FaceAxis::Right, FaceAxis::Left, FaceAxis::Down, FaceAxis::Up};

static camera::CameraModel modelOf(const camera::CameraModelParameters& parameters)
{
	const camera::ModelResult result = camera::CameraModel::create(parameters);
	REQUIRE(result.model.has_value());
	return *result.model;
}

static RayTable tableOf(const camera::CameraModel& model, const CaptureSettings& settings = {})
{
	RayTableResult result = RayTable::build(model, settings);
	for (const std::string& problem : result.problems)
		UNSCOPED_INFO(problem);
	REQUIRE(result.table.has_value());
	return std::move(*result.table);
}

// `d`'s component along the face `axis` looks along.
static double along(FaceAxis axis, const math::Vec3d& d)
{
	return math::dot(d, cameraFromFace(axis)[2]);
}

// The faces a remap's float arithmetic might give `d` to: those within `slack` of its largest
// component. A cube table must have each of them, and each must hold `d`'s bilinear footprint.
static std::vector<FaceAxis> plausibleAxes(const math::Vec3d& d, double slack)
{
	double best = -std::numeric_limits<double>::infinity();
	for (FaceAxis axis : kAxes)
		best = std::max(best, along(axis, d));
	std::vector<FaceAxis> axes;
	for (FaceAxis axis : kAxes)
	{
		if (along(axis, d) >= best - slack)
			axes.push_back(axis);
	}
	return axes;
}

// Whether `d` (a ray of pixel `pixel`) has exactly one tile on face `face`, which draws its whole
// bilinear footprint and visits that pixel. Reports why not.
static bool covered(const RayTable& table, std::size_t face, const math::Vec3d& d, const math::Vec2i* pixel, std::string& why)
{
	const math::Vec2d texel = table.onFace(face, d);
	const CaptureTile* owner = nullptr;
	int owners = 0;
	for (const CaptureTile& tile : table.tiles())
	{
		if (tile.face == face && tile.accepts(texel))
		{
			owner = &tile;
			++owners;
		}
	}
	if (owners != 1)
	{
		why = "taken by " + std::to_string(owners) + " tiles";
		return false;
	}
	const double u = std::floor(texel.x), v = std::floor(texel.y);
	if (!(u >= owner->first.x && u + 1 <= owner->last.x && v >= owner->first.y && v + 1 <= owner->last.y))
	{
		why = "its footprint is not drawn by its tile";
		return false;
	}
	if (pixel && !(pixel->x >= owner->pixelFirst.x && pixel->x < owner->pixelEnd.x && pixel->y >= owner->pixelFirst.y &&
				   pixel->y < owner->pixelEnd.y))
	{
		why = "its tile does not visit its pixel";
		return false;
	}
	return true;
}

// Every face `d` might be given to covers it (covered()), or the first that does not.
static bool coveredEverywhere(const RayTable& table, const math::Vec3d& d, const math::Vec2i* pixel, std::string& why)
{
	if (!table.cube())
		return covered(table, 0, d, pixel, why);
	for (FaceAxis axis : plausibleAxes(d, 1e-6))
	{
		std::size_t face = table.faces().size();
		for (std::size_t f = 0; f < table.faces().size(); ++f)
		{
			if (table.faces()[f].axis == axis)
				face = f;
		}
		if (face == table.faces().size())
		{
			why = "a face it may be given to is missing";
			return false;
		}
		if (!covered(table, face, d, pixel, why))
			return false;
	}
	return true;
}

TEST_CASE("a face is a proper rotation looking along its axis", "[render][capture]")
{
	for (FaceAxis axis : kAxes)
	{
		const math::Mat3d r = cameraFromFace(axis);
		CHECK(math::determinant(r) == 1.0);
		CHECK(math::transpose(r) * r == math::Mat3d{1.0});
		CHECK(majorAxis(r[2]) == axis);
	}
	// Ties go to the earlier axis, so a ray on a seam has one face.
	CHECK(majorAxis({1.0, 0.0, 1.0}) == FaceAxis::Front);
	CHECK(majorAxis({1.0, 1.0, 0.5}) == FaceAxis::Right);
	CHECK(majorAxis({-1.0, -1.0, 0.5}) == FaceAxis::Left);
	CHECK(majorAxis({0.0, -1.0, 0.5}) == FaceAxis::Up);
}

TEST_CASE("a pinhole's ray table is one front face at m times its longer focal length", "[render][capture]")
{
	camera::CameraModelParameters p;
	p.image = {160, 120};
	p.intrinsics = {150.0, 160.0, 79.25, 60.5};
	p.distortion = camera::NoDistortion{};
	const RayTable table = tableOf(modelOf(p));
	CHECK_FALSE(table.cube());
	REQUIRE(table.faces().size() == 1);
	CHECK(table.faces()[0].axis == FaceAxis::Front);
	// m = 2 by default, and a step of one pixel down spans the least on the face's plane.
	CHECK(std::abs(table.faces()[0].focal / (2 * 160.0) - 1) < 1e-4);
	// Even a pinhole is not interpolated exactly: blended unit directions are not unit.
	INFO("interpolation error: " << table.interpolationError() << " px");
	CHECK(table.interpolationError() < 1e-3);
	CHECK(table.tiles().size() == 1);
}

TEST_CASE("every model's interpolation error is measured, and no sub-sample is worse", "[render][capture]")
{
	for (int i = 0; i < render::testing::kStrongModelCount; ++i)
	{
		const camera::CameraModel model = modelOf(render::testing::strongCamera(i));
		const RayTable table = tableOf(model);
		INFO(camera::displayName(model.distortion()) << ": interpolation error " << table.interpolationError() << " px");
		// Measured at 640x480: none 3.3e-4 px, Brown-Conrady 5 1.5e-3, inverse 5.3e-4, modified 1.5e-3,
		// rational 2.4e-3, Kannala-Brandt 2.5e-4, all 30x or more under a detector's 0.07 px.
		INFO(camera::displayName(model.distortion()) << ": interpolation error " << table.interpolationError() << " px");
		CHECK(table.interpolationError() < 3e-3);

		// Every 4th row and column, plus the edges, at the default 4 x 4 rays a pixel.
		const int w = int(model.image().width), h = int(model.image().height), s = int(table.settings().samples);
		double worst = 0;
		std::size_t unprojectable = 0;
		for (int y = 0; y < h; ++y)
		{
			if (y % 4 != 0 && y != h - 1)
				continue;
			for (int x = 0; x < w; ++x)
			{
				if (x % 4 != 0 && x != w - 1 && y % 4 != 0)
					continue;
				for (int j = 0; j < s; ++j)
				{
					for (int k = 0; k < s; ++k)
					{
						const double u = x + (k + 0.5) / s - 0.5, v = y + (j + 0.5) / s - 0.5;
						const math::Vec3d d = table.direction(u, v);
						const camera::Projection<double> back = camera::project(model, d.x, d.y, d.z);
						if (!back.ok())
							++unprojectable;
						else
							worst = std::max(worst, std::hypot(back.u - u, back.v - v));
					}
				}
			}
		}
		CHECK(unprojectable == 0);
		INFO("worst sub-sample: " << worst << " px");
		CHECK(worst <= table.interpolationError() * (1 + 1e-6) + 1e-9);
	}
}

TEST_CASE("a model whose lattice interpolates it too coarsely is refused, saying by how much", "[render][capture]")
{
	// Brown-Conrady with k1 alone folds at a distorted radius of 0.7698; this camera's far corner is at
	// 0.765, just inside. CameraModel::create accepts it, since it checks only the image's corners.
	camera::CameraModelParameters p = render::testing::strongCamera(1);
	p.intrinsics.fx = 534.5;
	p.intrinsics.fy = 1.01 * 534.5;
	p.distortion = camera::BrownConrady5{-0.25, 0.0, 0.0, 0.0, 0.0};
	const camera::CameraModel model = modelOf(p);

	CaptureSettings loose;
	loose.interpolationTolerance = 1;
	const RayTable table = tableOf(model, loose);
	const double nearFold = table.interpolationError();
	const double clear = tableOf(modelOf(render::testing::strongCamera(1))).interpolationError();
	// Measured: 0.017 px near the fold, against 0.0015 clear of it, so the default tolerance refuses it.
	INFO("near the fold: " << nearFold << " px; clear of it: " << clear << " px");
	CHECK(nearFold > 2 * clear);

	CaptureSettings strict;
	strict.interpolationTolerance = nearFold / 2;
	const RayTableResult refused = RayTable::build(model, strict);
	CHECK_FALSE(refused.table.has_value());
	REQUIRE(refused.problems.size() == 1);
	CHECK(refused.problems[0].find("interpolate") != std::string::npos);
}

TEST_CASE("a narrow lens is one front face; a fisheye is the cube's", "[render][capture]")
{
	const RayTable pinhole = tableOf(modelOf(render::testing::strongCamera(0)));
	CHECK_FALSE(pinhole.cube());
	CHECK(pinhole.faces().size() == 1);

	const RayTable fisheye = tableOf(modelOf(render::testing::strongCamera(5)));
	CHECK(fisheye.cube());
	REQUIRE(fisheye.faces().size() == 5);
	for (std::size_t f = 0; f < fisheye.faces().size(); ++f)
	{
		const CaptureFace& face = fisheye.faces()[f];
		CHECK(face.axis == kAxes[f]);
		CHECK(face.focal == fisheye.faces()[0].focal); // one density, so nothing steps at a seam
		// The cube's major-axis faces serve rays at most 54.7 degrees off their axis, plus a cell's spread.
		INFO("near scale " << face.nearScale);
		CHECK(face.nearScale > 0.55);
		CHECK(face.nearScale <= 1.0);
	}
	for (int i = 1; i < render::testing::kStrongModelCount - 1; ++i)
	{
		const RayTable table = tableOf(modelOf(render::testing::strongCamera(i)));
		// Measured: each keeps the front face alone, 1773 x 1381 (inverse) to 2139 x 1589 texels (rational),
		// fewer texels than the cube's faces would take.
		INFO(camera::displayName(table.model().distortion()));
		CHECK_FALSE(table.cube());
	}
}

TEST_CASE("every step of a pixel spans at least m texels of the face its centre is given to", "[render][capture]")
{
	for (int i = 0; i < render::testing::kStrongModelCount; ++i)
	{
		const camera::CameraModel model = modelOf(render::testing::strongCamera(i, 0.5));
		const RayTable table = tableOf(model);
		const double m = table.settings().oversampling;
		const std::size_t w = model.image().width, h = model.image().height, row = w + 1;
		double least = std::numeric_limits<double>::infinity();
		std::size_t faceless = 0;
		for (std::size_t y = 0; y < h; ++y)
		{
			for (std::size_t x = 0; x < w; ++x)
			{
				const std::optional<std::size_t> face = table.faceOf(table.direction(double(x), double(y)));
				if (!face)
				{
					++faceless;
					continue;
				}
				const auto at = [&](std::size_t cx, std::size_t cy)
				{ return table.onFace(*face, math::Vec3d{table.corners()[cy * row + cx]}); };
				const math::Vec2d q00 = at(x, y), q10 = at(x + 1, y), q01 = at(x, y + 1), q11 = at(x + 1, y + 1);
				const math::Vec2d du = 0.5 * ((q10 - q00) + (q11 - q01)), dv = 0.5 * ((q01 - q00) + (q11 - q10));
				const double sum = math::dot(du, du) + math::dot(dv, dv);
				const double det = du.x * dv.y - du.y * dv.x;
				const double largest = std::sqrt(0.5 * (sum + std::sqrt(std::max(sum * sum - 4 * det * det, 0.0))));
				least = std::min(least, std::abs(det) / largest);
			}
		}
		CHECK(faceless == 0);
		// Measured: exactly m (to 1e-6) for every model, at the cell that sets the focal length.
		INFO(camera::displayName(model.distortion()) << ": the least stretch is " << least / m << " m");
		CHECK(least >= m * (1 - 1e-6));
		// Not over-provisioned: the cell that set the focal length is at the bound.
		CHECK(least <= m * (1 + 1e-6));
	}
}

TEST_CASE("every sub-sample belongs to exactly one tile of any face it may be given to", "[render][capture]")
{
	for (std::uint32_t side : {std::uint32_t{4096}, std::uint32_t{64}})
	{
		for (int i = 0; i < render::testing::kStrongModelCount; ++i)
		{
			const camera::CameraModel model = modelOf(render::testing::strongCamera(i, 0.5));
			CaptureSettings settings;
			settings.maxTileSide = side;
			const RayTable table = tableOf(model, settings);
			INFO(camera::displayName(model.distortion()) << ", tiles at most " << side << " a side: " << table.tiles().size()
														 << " tiles");
			std::size_t oversized = 0;
			for (const CaptureTile& tile : table.tiles())
			{
				if (std::uint32_t(tile.last.x - tile.first.x + 1) > side || std::uint32_t(tile.last.y - tile.first.y + 1) > side)
					++oversized;
			}
			CHECK(oversized == 0);

			const int w = int(model.image().width), h = int(model.image().height), s = int(settings.samples);
			std::size_t failures = 0;
			std::string first;
			for (int y = 0; y < h; ++y)
			{
				for (int x = 0; x < w; ++x)
				{
					const math::Vec2i pixel{x, y};
					for (int j = 0; j < s; ++j)
					{
						for (int k = 0; k < s; ++k)
						{
							std::string why;
							const math::Vec3d d = table.direction(x + (k + 0.5) / s - 0.5, y + (j + 0.5) / s - 0.5);
							if (!coveredEverywhere(table, d, &pixel, why) && failures++ == 0)
								first = "pixel (" + std::to_string(x) + ", " + std::to_string(y) + "): " + why;
						}
					}
				}
			}
			INFO(first);
			CHECK(failures == 0);
		}
	}
}

TEST_CASE("a ray on a seam between faces, or where three meet, is drawn by each face", "[render][capture]")
{
	// A seam between faces along axes a and b holds the directions a + b + t (a x b), up to where it
	// meets a third face at t = +-1. Those inside the fisheye's image are rays a remap may give to
	// either face, whichever its float arithmetic prefers.
	const camera::CameraModel model = modelOf(render::testing::strongCamera(5, 0.5));
	for (std::uint32_t side : {std::uint32_t{4096}, std::uint32_t{64}})
	{
		CaptureSettings settings;
		settings.maxTileSide = side;
		const RayTable table = tableOf(model, settings);
		REQUIRE(table.cube());
		std::size_t checked = 0, failures = 0;
		std::string first;
		for (std::size_t a = 0; a < kAxes.size(); ++a)
		{
			for (std::size_t b = a + 1; b < kAxes.size(); ++b)
			{
				const math::Vec3d axisA = cameraFromFace(kAxes[a])[2], axisB = cameraFromFace(kAxes[b])[2];
				if (math::dot(axisA, axisB) != 0.0)
					continue; // opposite faces share no seam
				const math::Vec3d across = math::cross(axisA, axisB);
				for (int k = -256; k <= 256; ++k)
				{
					const math::Vec3d d = math::normalize(axisA + axisB + (k / 256.0) * across);
					if (d.z <= 0)
						continue;
					const camera::Projection<double> pixel = camera::project(model, d.x, d.y, d.z);
					if (!pixel.ok() || !camera::contains(model, pixel.u, pixel.v))
						continue;
					++checked;
					std::string why;
					if (!coveredEverywhere(table, d, nullptr, why) && failures++ == 0)
						first = "seam " + std::to_string(a) + "/" + std::to_string(b) + " at " + std::to_string(k) + ": " + why;
				}
			}
		}
		INFO("tiles at most " << side << " a side: " << checked << " seam rays checked; " << first);
		CHECK(checked > 1000);
		CHECK(failures == 0);
	}
}

TEST_CASE("a band of the ray table fits the least storage buffer", "[render][capture]")
{
	const std::size_t limit = std::size_t{1} << 27;
	for (std::uint32_t width : {64u, 960u, 1920u, 7680u, 16384u})
	{
		const std::size_t rows = RayTable::bandRowsFor(width);
		INFO("width " << width << ": " << rows << " rows a band");
		CHECK(rows >= 1);
		CHECK((rows + 1) * (width + 1) * 16 <= limit); // its corners
		CHECK(rows * width * 16 <= limit);			   // its pixels' results
		CHECK((rows + 2) * (width + 1) * 16 > limit);  // and no more
	}
}

TEST_CASE("a ray table is the same however many threads build it", "[render][capture]")
{
	const camera::CameraModel model = modelOf(render::testing::strongCamera(5, 0.5));
	CaptureSettings settings;
	settings.maxTileSide = 64;
	const RayTable serial = tableOf(model, settings);
	lain::testing::ThreadPool pool(4);
	const RayTable pooled = tableOf(model, settings);
	CHECK(pooled.corners() == serial.corners());
	CHECK(pooled.interpolationError() == serial.interpolationError());
	REQUIRE(pooled.faces().size() == serial.faces().size());
	for (std::size_t f = 0; f < pooled.faces().size(); ++f)
	{
		CHECK(pooled.faces()[f].focal == serial.faces()[f].focal);
		CHECK(pooled.faces()[f].first == serial.faces()[f].first);
		CHECK(pooled.faces()[f].last == serial.faces()[f].last);
	}
	REQUIRE(pooled.tiles().size() == serial.tiles().size());
	for (std::size_t t = 0; t < pooled.tiles().size(); ++t)
	{
		CHECK(pooled.tiles()[t].pixelFirst == serial.tiles()[t].pixelFirst);
		CHECK(pooled.tiles()[t].pixelEnd == serial.tiles()[t].pixelEnd);
	}
}

TEST_CASE("a ray table refuses settings it cannot work with, naming them", "[render][capture]")
{
	const camera::CameraModel model = modelOf(render::testing::strongCamera(0, 0.25));
	const auto refusal = [&](CaptureSettings settings)
	{
		const RayTableResult result = RayTable::build(model, settings);
		CHECK_FALSE(result.table.has_value());
		REQUIRE(result.problems.size() == 1);
		return result.problems[0];
	};
	CaptureSettings settings;
	settings.samples = 0;
	CHECK(refusal(settings).find("ray a pixel") != std::string::npos);
	settings = {};
	settings.oversampling = 0.5;
	CHECK(refusal(settings).find("oversampling") != std::string::npos);
	settings.oversampling = std::numeric_limits<double>::quiet_NaN();
	CHECK(refusal(settings).find("oversampling") != std::string::npos);
	settings = {};
	settings.interpolationTolerance = 0;
	CHECK(refusal(settings).find("tolerance") != std::string::npos);
	settings = {};
	settings.maxTileSide = 4;
	CHECK(refusal(settings).find("tiles") != std::string::npos);
}
