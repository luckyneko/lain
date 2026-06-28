// Unit tests for lain::math's typed aliases. Compile-time alias checks plus a few
// behavioral ops that prove GLM's free functions are reachable as lain::math. No
// driver, no GLM internals beyond the public types.

#include <lain/math/types.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <type_traits>

namespace lm = lain::math;

TEST_CASE("per-dimension aliases match the named concretes", "[math]")
{
	STATIC_REQUIRE(std::is_same_v<lm::Vec2<float>, lm::Vec2f>);
	STATIC_REQUIRE(std::is_same_v<lm::Vec3<int>, lm::Vec3i>);
	STATIC_REQUIRE(std::is_same_v<lm::Vec4<double>, lm::Vec4d>);
	STATIC_REQUIRE(std::is_same_v<lm::Vec<2, unsigned int>, lm::Vec2u>);
}

TEST_CASE("GLM free functions are reachable as lain::math", "[math]")
{
	const lm::Vec2f v{3.0f, 4.0f};
	REQUIRE(lm::length(v) == Catch::Approx(5.0f));

	const lm::Vec3f a{1.0f, 0.0f, 0.0f};
	const lm::Vec3f b{0.0f, 1.0f, 0.0f};
	const lm::Vec3f c = lm::cross(a, b);
	REQUIRE(c.z == Catch::Approx(1.0f));
}

TEST_CASE("a 4x4 identity leaves a vector unchanged", "[math]")
{
	const lm::Mat4f id{1.0f};
	const lm::Vec4f v{2.0f, 3.0f, 4.0f, 1.0f};
	const lm::Vec4f r = id * v;
	REQUIRE(r.x == Catch::Approx(2.0f));
	REQUIRE(r.y == Catch::Approx(3.0f));
	REQUIRE(r.z == Catch::Approx(4.0f));
}
