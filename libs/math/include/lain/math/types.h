#pragma once

// lain::math — typed names over GLM. A generic core (Vec<N,T> / Mat<C,R,T> /
// Quat<T>), per-dimension templates (Vec2/3/4<T>), and named concretes across the
// scalar types we use. `using namespace glm` at the bottom re-exposes GLM's free
// functions (normalize / dot / cross / transpose, the transform helpers, ...) as
// lain::math, so a consumer names only lain::math. GLM's own types stay reachable
// (glm::vec2 etc.), but new code should use the lain:: names.
//
// GLM headers come in SYSTEM (see cmake/addGLM.cmake), so its template machinery
// never trips lain's strict warnings.

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_precision.hpp>

namespace lain::math
{
	// --- generic core ---
	using length_t = glm::length_t; // vector dimension / component index
	template <glm::length_t N, typename T>
	using Vec = glm::vec<N, T>;
	template <glm::length_t C, glm::length_t R, typename T>
	using Mat = glm::mat<C, R, T>;
	template <typename T>
	using Quat = glm::qua<T>;

	// --- Vec2 ---
	template <typename T>
	using Vec2 = Vec<2, T>;
	using Vec2f = glm::fvec2;
	using Vec2d = glm::dvec2;
	using Vec2i = glm::ivec2;
	using Vec2i8 = glm::i8vec2;
	using Vec2i16 = glm::i16vec2;
	using Vec2i32 = glm::i32vec2;
	using Vec2u = glm::uvec2;
	using Vec2u8 = glm::u8vec2;
	using Vec2u16 = glm::u16vec2;
	using Vec2u32 = glm::u32vec2;

	// --- Vec3 ---
	template <typename T>
	using Vec3 = Vec<3, T>;
	using Vec3f = glm::fvec3;
	using Vec3d = glm::dvec3;
	using Vec3i = glm::ivec3;
	using Vec3i8 = glm::i8vec3;
	using Vec3i16 = glm::i16vec3;
	using Vec3i32 = glm::i32vec3;
	using Vec3u = glm::uvec3;
	using Vec3u8 = glm::u8vec3;
	using Vec3u16 = glm::u16vec3;
	using Vec3u32 = glm::u32vec3;

	// --- Vec4 ---
	template <typename T>
	using Vec4 = Vec<4, T>;
	using Vec4f = glm::fvec4;
	using Vec4d = glm::dvec4;
	using Vec4i = glm::ivec4;
	using Vec4i8 = glm::i8vec4;
	using Vec4i16 = glm::i16vec4;
	using Vec4i32 = glm::i32vec4;
	using Vec4u = glm::uvec4;
	using Vec4u8 = glm::u8vec4;
	using Vec4u16 = glm::u16vec4;
	using Vec4u32 = glm::u32vec4;

	// --- matrices + quaternion ---
	using Mat3f = glm::mat3;
	using Mat4f = glm::mat4;
	using Mat3d = glm::dmat3;
	using Mat4d = glm::dmat4;
	using Quatf = glm::quat;
	using Quatd = glm::dquat;

	using namespace glm; // re-expose GLM's free functions as lain::math
} // namespace lain::math
