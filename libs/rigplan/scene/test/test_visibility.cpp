// The visibility map, counted over distances this file casts on the CPU, so each count is judged
// against the rule it states with nothing drawn: lain's projection, the grazing cutoff, and whether a
// segment from the camera to the cell meets a box. The last case draws the distances on a device
// through renderDistance, and SKIPs where the machine has none.

#include "gpu.h"
#include "testcameras.h"
#include "testplan.h"

#include <lain/camera/projection.h>
#include <lain/render/renderer.h>
#include <lain/rigplan/scene.h>
#include <lain/rigplan/visibility.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>
#include <vector>

using namespace lain;
using namespace lain::rigplan;

static constexpr double kPi = 3.14159265358979323846;

// A flat rectangle of the scene the distances are cast against: centred on `centre`, `width` metres
// along `across` and `height` along `up`.
struct Rect
{
	math::Vec3d centre;
	math::Vec3d across;
	math::Vec3d up;
	double width;
	double height;
};

static Rect floorRect(double half = 100)
{
	return {{0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, 2 * half, 2 * half};
}

// An axis-aligned box standing on the floor.
struct Block
{
	math::Vec3d least;
	math::Vec3d most;
};

static std::vector<Rect> facesOf(const Block& box)
{
	const math::Vec3d c = (box.least + box.most) / 2.0;
	const math::Vec3d s = box.most - box.least;
	const math::Vec3d X{1.0, 0.0, 0.0}, Y{0.0, 1.0, 0.0}, Z{0.0, 0.0, 1.0};
	return {
		{c + Z * (s.z / 2), X, Y, s.x, s.y},
		{c - Z * (s.z / 2), X, Y, s.x, s.y},
		{c + X * (s.x / 2), Y, Z, s.y, s.z},
		{c - X * (s.x / 2), Y, Z, s.y, s.z},
		{c + Y * (s.y / 2), X, Z, s.x, s.z},
		{c - Y * (s.y / 2), X, Z, s.x, s.z},
	};
}

// How far along the unit ray from `origin` it meets the nearest rectangle, or infinity.
static double cast(const std::vector<Rect>& rects, const math::Vec3d& origin, const math::Vec3d& direction)
{
	double nearest = std::numeric_limits<double>::infinity();
	for (const Rect& rect : rects)
	{
		const math::Vec3d normal = math::cross(rect.across, rect.up);
		const double facing = math::dot(direction, normal);
		if (std::abs(facing) < 1e-15)
			continue;
		const double t = math::dot(rect.centre - origin, normal) / facing;
		if (t <= 0 || t >= nearest)
			continue;
		const math::Vec3d p = origin + t * direction - rect.centre;
		if (std::abs(math::dot(p, rect.across)) <= rect.width / 2 && std::abs(math::dot(p, rect.up)) <= rect.height / 2)
			nearest = t;
	}
	return nearest;
}

// Every face visibilityFaces gives `camera`, its distances cast through each texel's centre.
static std::vector<FaceDistances> castFaces(const VisibilityCamera& camera, const std::vector<Rect>& rects, std::uint32_t size)
{
	std::vector<FaceDistances> faces;
	for (const render::CaptureFace& face : visibilityFaces(camera.model, size))
	{
		image::Image distances(int(size), int(size), image::PixelFormat::Gray32F);
		float* out = reinterpret_cast<float*>(distances.data());
		const math::Mat3d cameraFromFace = render::cameraFromFace(face.axis);
		for (std::uint32_t j = 0; j < size; ++j)
		{
			for (std::uint32_t i = 0; i < size; ++i)
			{
				const math::Vec3d inFace{(i - face.principalPoint.x) / face.focal, (j - face.principalPoint.y) / face.focal, 1.0};
				const math::Vec3d d = camera.planFromCamera.rotate(cameraFromFace * math::normalize(inFace));
				out[std::size_t(j) * size + i] = float(cast(rects, camera.planFromCamera.translation(), d));
			}
		}
		faces.push_back({face, std::move(distances)});
	}
	return faces;
}

// A camera of `parameters` at `eye` looking along `forward`, its image's right level.
static VisibilityCamera looking(const camera::CameraModelParameters& parameters, const math::Vec3d& eye, const math::Vec3d& forward)
{
	const math::Vec3d z = math::normalize(forward);
	const math::Vec3d hint = std::abs(z.z) > 0.99 ? math::Vec3d{0.0, 1.0, 0.0} : math::Vec3d{0.0, 0.0, 1.0};
	const math::Vec3d x = math::normalize(math::cross(z, hint));
	const math::Vec3d y = math::cross(z, x);
	const math::Mat3d rotation{x, y, z};
	return {core::Uuid::generate(), "cam", *camera::CameraModel::create(parameters).model, {math::quat_cast(rotation), eye}};
}

// Whether the rule's first two tests let `camera` see cell `p`, ignoring occlusion: within a capture's
// range and the grazing cutoff, and inside the image by lain's projection. Unset where a cell lies too
// near one of those edges for float arithmetic to decide it.
static std::optional<bool> inView(const VisibilityCamera& camera, const math::Vec3d& p)
{
	const math::Vec3d toCell = p - camera.planFromCamera.translation();
	const double r = math::length(toCell);
	const double cosine = std::abs(toCell.z) / r;
	const double grazing = std::cos(kGrazingDegrees * kPi / 180);
	if (std::abs(cosine - grazing) < 1e-9)
		return std::nullopt;
	if (cosine < grazing)
		return false;
	const math::Vec3d d = camera.planFromCamera.inverse().rotate(toCell) / r;
	const camera::Projection<double> pixel = camera::project(camera.model, d.x, d.y, d.z);
	if (!pixel.ok())
		return false;
	const camera::ImageGeometry& image = camera.model.image();
	const double edge = std::min({pixel.u + 0.5, pixel.v + 0.5, image.width - 0.5 - pixel.u, image.height - 0.5 - pixel.v});
	if (std::abs(edge) < 1e-6)
		return std::nullopt;
	return edge > 0;
}

// Whether the segment from `a` to `b` meets `box` grown by `grow` (shrunk, when negative).
static bool meets(const math::Vec3d& a, const math::Vec3d& b, const Block& box, double grow)
{
	double enter = 0, leave = 1;
	for (int k = 0; k < 3; ++k)
	{
		const double lo = box.least[k] - grow, hi = box.most[k] + grow;
		const double d = b[k] - a[k];
		if (std::abs(d) < 1e-15)
		{
			if (a[k] < lo || a[k] > hi)
				return false;
			continue;
		}
		double t0 = (lo - a[k]) / d, t1 = (hi - a[k]) / d;
		if (t0 > t1)
			std::swap(t0, t1);
		enter = std::max(enter, t0);
		leave = std::min(leave, t1);
	}
	return enter <= leave;
}

static bool at(const std::vector<bool>& seen, const VisibilityGrid& grid, int column, int row)
{
	return seen[std::size_t(row) * std::size_t(grid.columns) + std::size_t(column)];
}

static VisibilityGrid grid(math::Vec2d origin, double cell, int columns, int rows)
{
	VisibilityGrid g;
	g.origin = origin;
	g.cell = cell;
	g.columns = columns;
	g.rows = rows;
	return g;
}

TEST_CASE("the static scene keeps what never moves, and drops boards, path props and clip props", "[rigplan][visibility]")
{
	RigPlan plan = testplan::furnished(); // a room, a partition, a crate, a walking clip prop, a pole
	plan.boards.push_back(testplan::board());
	Prop statue;
	statue.id = core::Uuid::generate();
	statue.name = "statue";
	statue.mesh = core::Uri{"statue.obj"};
	statue.position = {1.0, 1.0, 0.0};
	Prop cart = statue;
	cart.id = core::Uuid::generate();
	cart.name = "cart";
	cart.path = Path{{{0, {1.0, 0.0, 0.0}, {}}}}; // one key: still a mover
	plan.props.push_back(statue);
	plan.props.push_back(cart);

	// Meshes only: no picture, no bake, no board drawing, which a static scene must not ask for.
	SceneAssets assets;
	sceneGeometry(plan, assets);
	auto clipOfOne = [&]
	{
		mesh::Mesh square;
		square.positions = {{0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}};
		square.indices = {0, 1, 2};
		square.parts = {{0, 0, 3}};
		square.materials = {mesh::Material{}, mesh::Material{}};
		auto clip = std::make_shared<mesh::MeshClip>();
		clip->frames = {square};
		return PropMesh{clip, ""};
	};
	for (const Prop& prop : plan.props)
		assets.props[prop.id] = clipOfOne();

	const SceneResult scene = staticScene(plan, nominalLayout(plan), assets);
	CHECK(scene.problems.empty());
	std::size_t room = 0, planes = 0, boxes = 0, poles = 0;
	for (const SceneOwner& owner : scene.owners)
	{
		room += owner.object == SceneObject::Room ? 1 : 0;
		planes += owner.object == SceneObject::Plane ? 1 : 0;
		boxes += owner.object == SceneObject::Box ? 1 : 0;
		poles += owner.object == SceneObject::Pole ? 1 : 0;
		CHECK(owner.object != SceneObject::Board);
		if (owner.object == SceneObject::Prop)
			CHECK(owner.id == statue.id);
	}
	CHECK(room == 5);	// no ceiling
	CHECK(planes == 1); // one rectangle, drawn from both sides
	CHECK(boxes == 6);
	CHECK(poles == 1);
	CHECK(std::count_if(scene.owners.begin(), scene.owners.end(), [](const SceneOwner& o)
						{ return o.object == SceneObject::Prop; }) == 1);
	CHECK(scene.scene.textures.empty());
	CHECK(scene.scene.lights.empty());
	for (const render::Material& material : scene.scene.materials)
		CHECK_FALSE(material.texture);
	CHECK(render::validate(scene.scene).empty());

	// A static prop whose mesh is not loaded yet is named: the map waits for it.
	assets.props.erase(statue.id);
	const SceneResult waiting = staticScene(plan, nominalLayout(plan), assets);
	REQUIRE(waiting.problems.size() == 1);
	CHECK(waiting.problems[0].find("statue") != std::string::npos);
}

TEST_CASE("a camera's faces are the ones its image reaches", "[rigplan][visibility]")
{
	const camera::CameraModel pinhole = *camera::CameraModel::create(render::testing::strongCamera(0)).model;
	const std::vector<render::CaptureFace> narrow = visibilityFaces(pinhole, 64);
	REQUIRE(narrow.size() == 1);
	CHECK(narrow[0].axis == render::FaceAxis::Front);
	CHECK(narrow[0].focal == 32);
	CHECK(narrow[0].principalPoint.x == 31.5);
	const camera::CameraModel fisheye = *camera::CameraModel::create(render::testing::strongCamera(5)).model;
	CHECK(visibilityFaces(fisheye, 64).size() == 5);

	// Every ray of every strong camera's image falls on a face it has.
	for (int m = 0; m < render::testing::kStrongModelCount; ++m)
	{
		const camera::CameraModel model = *camera::CameraModel::create(render::testing::strongCamera(m)).model;
		const std::vector<render::CaptureFace> faces = visibilityFaces(model, 64);
		int missing = 0;
		for (int v = 0; v < 480; v += 7)
		{
			for (int u = 0; u < 640; u += 7)
			{
				const camera::Unprojection<double> ray = camera::unproject(model, double(u), double(v));
				REQUIRE(ray.ok());
				const render::FaceAxis axis = render::majorAxis({ray.x, ray.y, ray.z});
				missing += std::none_of(faces.begin(), faces.end(), [&](const render::CaptureFace& f)
										{ return f.axis == axis; })
							   ? 1
							   : 0;
			}
		}
		INFO("model " << m);
		CHECK(missing == 0);
	}
}

TEST_CASE("a bare floor under one camera counts 1 exactly where project() lands in its image, to the grazing cutoff", "[rigplan][visibility]")
{
	// A wide pinhole 1.2 m up, tilted down 20 degrees, so its image holds the horizon: the floor meets
	// the grazing cutoff 13.7 m out, inside the image.
	const VisibilityCamera camera = looking(render::testing::strongCamera(0), {0.0, 0.0, 1.2}, {0.0, std::cos(0.35), -std::sin(0.35)});
	const VisibilityGrid g = grid({-20.0, -2.0}, 0.2, 200, 210);
	const std::vector<FaceDistances> faces = castFaces(camera, {floorRect()}, 512);
	const std::vector<bool> seen = seenBy(g, camera, faces);

	int counted = 0, grazed = 0, wrong = 0;
	for (int row = 0; row < g.rows; ++row)
	{
		for (int column = 0; column < g.columns; ++column)
		{
			const math::Vec3d p = g.centre(column, row);
			const std::optional<bool> expected = inView(camera, p);
			if (!expected)
				continue;
			const bool s = at(seen, g, column, row);
			counted += s ? 1 : 0;
			wrong += s != *expected ? 1 : 0;
			// In the image, beyond the cutoff.
			const math::Vec3d d = camera.planFromCamera.inverse().rotate(p - camera.planFromCamera.translation());
			const camera::Projection<double> pixel = camera::project(camera.model, d.x, d.y, d.z);
			if (!*expected && pixel.ok() && camera::contains(camera.model, pixel.u, pixel.v))
				++grazed;
		}
	}
	CHECK(counted > 2500);
	CHECK(grazed > 1000);
	CHECK(wrong == 0);
}

TEST_CASE("a box's shadow counts 0", "[rigplan][visibility]")
{
	// A camera 3 m up and 2.5 m back, looking down past a metre-high box on the floor.
	const VisibilityCamera camera = looking(render::testing::strongCamera(0), {0.0, -2.5, 3.0}, {0.0, 2.5, -3.0});
	const Block box{{-0.5, 0.0, 0.0}, {0.5, 1.0, 1.0}};
	std::vector<Rect> rects = facesOf(box);
	rects.push_back(floorRect());
	const VisibilityGrid g = grid({-3.0, -2.0}, 0.05, 120, 140);
	const std::vector<bool> seen = seenBy(g, camera, castFaces(camera, rects, 512));

	// A cell within 3 cm of the box's outline, as the camera sees it, is left to the rasteriser.
	int shadow = 0, lit = 0, wrong = 0;
	for (int row = 0; row < g.rows; ++row)
	{
		for (int column = 0; column < g.columns; ++column)
		{
			const math::Vec3d p = g.centre(column, row);
			const std::optional<bool> visible = inView(camera, p);
			const math::Vec3d eye = camera.planFromCamera.translation();
			const bool inner = meets(eye, p, box, -0.03), outer = meets(eye, p, box, 0.03);
			if (!visible || !*visible || inner != outer)
				continue;
			const bool s = at(seen, g, column, row);
			(inner ? shadow : lit) += 1;
			wrong += s == inner ? 1 : 0;
		}
	}
	CHECK(shadow > 300);
	CHECK(lit > 2000);
	CHECK(wrong == 0);
}

TEST_CASE("a fisheye counts nothing outside its own image", "[rigplan][visibility]")
{
	// The 168 degree fisheye 1 m up, looking straight down: its five faces see the whole floor below
	// the horizon, its image a rectangle of it.
	const VisibilityCamera camera = looking(render::testing::strongCamera(5), {0.0, 0.0, 1.0}, {0.0, 0.0, -1.0});
	const VisibilityGrid g = grid({-12.0, -12.0}, 0.1, 240, 240);
	const std::vector<FaceDistances> faces = castFaces(camera, {floorRect()}, 256);
	REQUIRE(faces.size() == 5);
	const std::vector<bool> seen = seenBy(g, camera, faces);

	int inside = 0, outside = 0, wrong = 0;
	for (int row = 0; row < g.rows; ++row)
	{
		for (int column = 0; column < g.columns; ++column)
		{
			const std::optional<bool> expected = inView(camera, g.centre(column, row));
			if (!expected)
				continue;
			(*expected ? inside : outside) += 1;
			wrong += at(seen, g, column, row) != *expected ? 1 : 0;
		}
	}
	CHECK(inside > 3000);
	CHECK(outside > 5000);
	CHECK(wrong == 0);
}

TEST_CASE("two cameras count 2 where they overlap, and each cell names its cameras", "[rigplan][visibility]")
{
	const VisibilityCamera left = looking(render::testing::strongCamera(0), {-0.8, 0.0, 2.0}, {0.3, 0.0, -1.0});
	const VisibilityCamera right = looking(render::testing::strongCamera(1), {0.8, 0.0, 2.0}, {-0.3, 0.0, -1.0});
	const VisibilityGrid g = grid({-4.0, -3.0}, 0.1, 80, 60);
	VisibilityMap map(g);
	map.add(left.id, seenBy(g, left, castFaces(left, {floorRect()}, 256)));
	map.add(right.id, seenBy(g, right, castFaces(right, {floorRect()}, 256)));
	REQUIRE(map.cameras() == std::vector<core::Uuid>{left.id, right.id});

	int both = 0, one = 0, wrong = 0;
	for (int row = 0; row < g.rows; ++row)
	{
		for (int column = 0; column < g.columns; ++column)
		{
			const std::optional<bool> a = inView(left, g.centre(column, row));
			const std::optional<bool> b = inView(right, g.centre(column, row));
			if (!a || !b)
				continue;
			std::vector<core::Uuid> expected;
			if (*a)
				expected.push_back(left.id);
			if (*b)
				expected.push_back(right.id);
			both += expected.size() == 2 ? 1 : 0;
			one += expected.size() == 1 ? 1 : 0;
			wrong += map.count(column, row) != expected.size() || map.camerasAt(column, row) != expected ? 1 : 0;
		}
	}
	CHECK(both > 300);
	CHECK(one > 300);
	CHECK(wrong == 0);

	// Counted again, a camera replaces what it saw rather than adding to it.
	map.add(left.id, std::vector<bool>(g.cells(), false));
	for (int row = 0; row < g.rows; ++row)
	{
		for (int column = 0; column < g.columns; ++column)
			CHECK(map.count(column, row) == (map.camerasAt(column, row).empty() ? 0u : 1u));
	}
}

TEST_CASE("a cell is judged by the texel nearest its ray, never a blend of its neighbours", "[rigplan][visibility]")
{
	const VisibilityCamera camera = looking(render::testing::strongCamera(0), {0.0, 0.0, 2.0}, {0.0, 0.0, -1.0});
	const VisibilityGrid g = grid({0.3, -0.2}, 0.1, 1, 1); // one cell, off the camera's axis
	std::vector<FaceDistances> faces = castFaces(camera, {floorRect()}, 64);
	REQUIRE(faces.size() == 1);
	REQUIRE(seenBy(g, camera, faces)[0]);

	const math::Vec3d toCell = g.centre(0, 0) - camera.planFromCamera.translation();
	const math::Vec2d texel = render::onFace(faces[0].face, camera.planFromCamera.inverse().rotate(toCell) / math::length(toCell));
	const int ni = int(std::floor(texel.x + 0.5)), nj = int(std::floor(texel.y + 0.5));
	float* d = reinterpret_cast<float*>(faces[0].distances.data());
	const std::vector<float> floor(d, d + 64 * 64);

	// Something a centimetre from the camera at every texel round the nearest, but not on it.
	for (int j = nj - 1; j <= nj + 1; ++j)
	{
		for (int i = ni - 1; i <= ni + 1; ++i)
			d[j * 64 + i] = (i == ni && j == nj) ? d[j * 64 + i] : 0.01f;
	}
	CHECK(seenBy(g, camera, faces)[0]);

	// And on it alone.
	std::copy(floor.begin(), floor.end(), d);
	d[nj * 64 + ni] = 0.01f;
	CHECK_FALSE(seenBy(g, camera, faces)[0]);
}

TEST_CASE("the map's plane is the room's floor in whole cells, or what the plan places", "[rigplan][visibility]")
{
	RigPlan plan = testplan::plan(); // a pole at (2, 3) and a wall bracket at (6, 3)
	const VisibilityGrid placed = visibilityGrid(plan, 1.0, 0.5);
	CHECK(placed.columns == 8);
	CHECK(placed.rows == 1);
	CHECK(placed.origin.x == 2.0);
	CHECK(placed.origin.y == 2.75);
	CHECK(placed.height == 1.0);

	Room room;
	room.centre = {1.0, -0.5};
	room.size = {10.0, 8.0, 3.0};
	plan.room = room;
	const VisibilityGrid floor = visibilityGrid(plan);
	CHECK(floor.columns == 200);
	CHECK(floor.rows == 160);
	CHECK(std::abs(floor.origin.x - -4.0) < 1e-12);
	CHECK(std::abs(floor.origin.y - -4.5) < 1e-12);
	CHECK(floor.height == 0);
	CHECK(std::abs(floor.centre(0, 0).x - -3.975) < 1e-12);

	CHECK(visibilityGrid(RigPlan{}).cells() == 0);
}

TEST_CASE("a rig plan's map counts what renderDistance draws of its static scene", "[rigplan][visibility][gpu]")
{
	acm::Device& device = render::testing::device();
	render::Renderer renderer(device);
	REQUIRE(renderer.problems().empty());

	// A camera 2.5 m above a floor, looking straight down past a crate, in a room of floor alone.
	RigPlan plan = testplan::plan();
	plan.mounts.clear();
	plan.cameras.clear();
	plan.lights.clear();
	plan.specifications.lenses[0].focalLength = core::Length::from<core::Length::Millimetres>(2.0); // 1000 px
	Room room;
	room.size = {6.0, 6.0, 3.0};
	room.ceiling.present = room.north.present = room.south.present = room.east.present = room.west.present = false;
	plan.room = room;
	Box crate;
	crate.id = core::Uuid::generate();
	crate.centre = {0.35, 0.1, 0.25};
	crate.size = {0.3, 0.4, 0.5};
	plan.boxes.push_back(crate);
	Mount above;
	above.id = core::Uuid::generate();
	above.kind = FixedPoint{{0.0, 0.0, 2.5}};
	plan.mounts.push_back(above);
	plan.cameras.push_back(testplan::camera(core::Uuid::generate(), "down", above.id, {0.0, 0.0, 0.0}, {0.0, -90.0, 0.0}));

	const NominalLayout layout = nominalLayout(plan);
	const std::vector<VisibilityCamera> cameras = visibilityCameras(layout);
	REQUIRE(cameras.size() == 1);
	SceneAssets assets;
	sceneGeometry(plan, assets);
	const SceneResult scene = staticScene(plan, layout, assets);
	REQUIRE(scene.problems.empty());
	render::UploadResult uploaded = renderer.upload(scene.scene);
	REQUIRE(uploaded.scene);

	std::vector<FaceDistances> faces;
	for (const render::CaptureFace& face : visibilityFaces(cameras[0].model))
	{
		render::DistanceResult drawn = renderer.renderDistance(*uploaded.scene, faceView(face, cameras[0].planFromCamera));
		REQUIRE(drawn.problems.empty());
		faces.push_back({face, std::move(drawn.image)});
	}
	REQUIRE(faces.size() == 1);
	const VisibilityGrid g = visibilityGrid(plan);
	VisibilityMap map(g);
	map.add(cameras[0].id, seenBy(g, cameras[0], faces));

	const Block box{crate.centre - crate.size / 2.0, crate.centre + crate.size / 2.0};
	int shadow = 0, lit = 0, wrong = 0;
	for (int row = 0; row < g.rows; ++row)
	{
		for (int column = 0; column < g.columns; ++column)
		{
			const math::Vec3d p = g.centre(column, row);
			const std::optional<bool> visible = inView(cameras[0], p);
			const math::Vec3d eye = cameras[0].planFromCamera.translation();
			const bool inner = meets(eye, p, box, -0.02), outer = meets(eye, p, box, 0.02);
			if (!visible || inner != outer)
				continue;
			const bool expected = *visible && !inner;
			if (*visible)
				(inner ? shadow : lit) += 1;
			wrong += (map.count(column, row) == 1) != expected ? 1 : 0;
		}
	}
	CHECK(shadow > 30);
	CHECK(lit > 1000);
	CHECK(wrong == 0);
}
