#pragma once

// Optional one-liner declarations for the lain::data reflection layer (see data.h). Each expands
// to exactly the hand-written serialize() visitor — a wrapper, never the only route. Requires
// archive.h (which includes this header), and <string_view> for the variant-arm macro.
//
//   struct BlurNode { float radius; std::filesystem::path path; };
//   LAIN_SERIALIZE(BlurNode, radius, path)   // at namespace scope -> free serialize(), field = key
//
//   class Secret { int a, b; public: LAIN_SERIALIZE_INTRUSIVE(a, b) };  // private members
//
//   using Shape = std::variant<Circle, Square>;   // + at namespace scope, one per arm:
//   LAIN_SERIALIZE_VARIANT_ARM(Circle, "circle")
//   LAIN_SERIALIZE_VARIANT_ARM(Square, "square")
//
// FOR_EACH supports up to 32 fields; hand-write serialize() for more. The EXPAND indirection is
// the MSVC __VA_ARGS__ workaround, so the machinery is portable across MSVC / clang / gcc.

#define LAIN_DATA_DETAIL_EXPAND(x) x

#define LAIN_DATA_DETAIL_CAT(a, b) LAIN_DATA_DETAIL_CAT_(a, b)
#define LAIN_DATA_DETAIL_CAT_(a, b) a##b

// Count the variadic args (1..32) by shifting them past a fixed marker list.
#define LAIN_DATA_DETAIL_NARG(...) LAIN_DATA_DETAIL_EXPAND(LAIN_DATA_DETAIL_NARG_(__VA_ARGS__, LAIN_DATA_DETAIL_RSEQ_N()))
#define LAIN_DATA_DETAIL_NARG_(...) LAIN_DATA_DETAIL_EXPAND(LAIN_DATA_DETAIL_ARG_N(__VA_ARGS__))
#define LAIN_DATA_DETAIL_ARG_N(                                                             \
	_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16,                  \
	_17, _18, _19, _20, _21, _22, _23, _24, _25, _26, _27, _28, _29, _30, _31, _32, N, ...) \
	N
#define LAIN_DATA_DETAIL_RSEQ_N()                                   \
	32, 31, 30, 29, 28, 27, 26, 25, 24, 23, 22, 21, 20, 19, 18, 17, \
		16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0

// Apply macro m to each field. Each FE_n emits m(x) then recurses on the rest.
#define LAIN_DATA_DETAIL_FE_1(m, x) m(x)
#define LAIN_DATA_DETAIL_FE_2(m, x, ...) m(x) LAIN_DATA_DETAIL_EXPAND(LAIN_DATA_DETAIL_FE_1(m, __VA_ARGS__))
#define LAIN_DATA_DETAIL_FE_3(m, x, ...) m(x) LAIN_DATA_DETAIL_EXPAND(LAIN_DATA_DETAIL_FE_2(m, __VA_ARGS__))
#define LAIN_DATA_DETAIL_FE_4(m, x, ...) m(x) LAIN_DATA_DETAIL_EXPAND(LAIN_DATA_DETAIL_FE_3(m, __VA_ARGS__))
#define LAIN_DATA_DETAIL_FE_5(m, x, ...) m(x) LAIN_DATA_DETAIL_EXPAND(LAIN_DATA_DETAIL_FE_4(m, __VA_ARGS__))
#define LAIN_DATA_DETAIL_FE_6(m, x, ...) m(x) LAIN_DATA_DETAIL_EXPAND(LAIN_DATA_DETAIL_FE_5(m, __VA_ARGS__))
#define LAIN_DATA_DETAIL_FE_7(m, x, ...) m(x) LAIN_DATA_DETAIL_EXPAND(LAIN_DATA_DETAIL_FE_6(m, __VA_ARGS__))
#define LAIN_DATA_DETAIL_FE_8(m, x, ...) m(x) LAIN_DATA_DETAIL_EXPAND(LAIN_DATA_DETAIL_FE_7(m, __VA_ARGS__))
#define LAIN_DATA_DETAIL_FE_9(m, x, ...) m(x) LAIN_DATA_DETAIL_EXPAND(LAIN_DATA_DETAIL_FE_8(m, __VA_ARGS__))
#define LAIN_DATA_DETAIL_FE_10(m, x, ...) m(x) LAIN_DATA_DETAIL_EXPAND(LAIN_DATA_DETAIL_FE_9(m, __VA_ARGS__))
#define LAIN_DATA_DETAIL_FE_11(m, x, ...) m(x) LAIN_DATA_DETAIL_EXPAND(LAIN_DATA_DETAIL_FE_10(m, __VA_ARGS__))
#define LAIN_DATA_DETAIL_FE_12(m, x, ...) m(x) LAIN_DATA_DETAIL_EXPAND(LAIN_DATA_DETAIL_FE_11(m, __VA_ARGS__))
#define LAIN_DATA_DETAIL_FE_13(m, x, ...) m(x) LAIN_DATA_DETAIL_EXPAND(LAIN_DATA_DETAIL_FE_12(m, __VA_ARGS__))
#define LAIN_DATA_DETAIL_FE_14(m, x, ...) m(x) LAIN_DATA_DETAIL_EXPAND(LAIN_DATA_DETAIL_FE_13(m, __VA_ARGS__))
#define LAIN_DATA_DETAIL_FE_15(m, x, ...) m(x) LAIN_DATA_DETAIL_EXPAND(LAIN_DATA_DETAIL_FE_14(m, __VA_ARGS__))
#define LAIN_DATA_DETAIL_FE_16(m, x, ...) m(x) LAIN_DATA_DETAIL_EXPAND(LAIN_DATA_DETAIL_FE_15(m, __VA_ARGS__))
#define LAIN_DATA_DETAIL_FE_17(m, x, ...) m(x) LAIN_DATA_DETAIL_EXPAND(LAIN_DATA_DETAIL_FE_16(m, __VA_ARGS__))
#define LAIN_DATA_DETAIL_FE_18(m, x, ...) m(x) LAIN_DATA_DETAIL_EXPAND(LAIN_DATA_DETAIL_FE_17(m, __VA_ARGS__))
#define LAIN_DATA_DETAIL_FE_19(m, x, ...) m(x) LAIN_DATA_DETAIL_EXPAND(LAIN_DATA_DETAIL_FE_18(m, __VA_ARGS__))
#define LAIN_DATA_DETAIL_FE_20(m, x, ...) m(x) LAIN_DATA_DETAIL_EXPAND(LAIN_DATA_DETAIL_FE_19(m, __VA_ARGS__))
#define LAIN_DATA_DETAIL_FE_21(m, x, ...) m(x) LAIN_DATA_DETAIL_EXPAND(LAIN_DATA_DETAIL_FE_20(m, __VA_ARGS__))
#define LAIN_DATA_DETAIL_FE_22(m, x, ...) m(x) LAIN_DATA_DETAIL_EXPAND(LAIN_DATA_DETAIL_FE_21(m, __VA_ARGS__))
#define LAIN_DATA_DETAIL_FE_23(m, x, ...) m(x) LAIN_DATA_DETAIL_EXPAND(LAIN_DATA_DETAIL_FE_22(m, __VA_ARGS__))
#define LAIN_DATA_DETAIL_FE_24(m, x, ...) m(x) LAIN_DATA_DETAIL_EXPAND(LAIN_DATA_DETAIL_FE_23(m, __VA_ARGS__))
#define LAIN_DATA_DETAIL_FE_25(m, x, ...) m(x) LAIN_DATA_DETAIL_EXPAND(LAIN_DATA_DETAIL_FE_24(m, __VA_ARGS__))
#define LAIN_DATA_DETAIL_FE_26(m, x, ...) m(x) LAIN_DATA_DETAIL_EXPAND(LAIN_DATA_DETAIL_FE_25(m, __VA_ARGS__))
#define LAIN_DATA_DETAIL_FE_27(m, x, ...) m(x) LAIN_DATA_DETAIL_EXPAND(LAIN_DATA_DETAIL_FE_26(m, __VA_ARGS__))
#define LAIN_DATA_DETAIL_FE_28(m, x, ...) m(x) LAIN_DATA_DETAIL_EXPAND(LAIN_DATA_DETAIL_FE_27(m, __VA_ARGS__))
#define LAIN_DATA_DETAIL_FE_29(m, x, ...) m(x) LAIN_DATA_DETAIL_EXPAND(LAIN_DATA_DETAIL_FE_28(m, __VA_ARGS__))
#define LAIN_DATA_DETAIL_FE_30(m, x, ...) m(x) LAIN_DATA_DETAIL_EXPAND(LAIN_DATA_DETAIL_FE_29(m, __VA_ARGS__))
#define LAIN_DATA_DETAIL_FE_31(m, x, ...) m(x) LAIN_DATA_DETAIL_EXPAND(LAIN_DATA_DETAIL_FE_30(m, __VA_ARGS__))
#define LAIN_DATA_DETAIL_FE_32(m, x, ...) m(x) LAIN_DATA_DETAIL_EXPAND(LAIN_DATA_DETAIL_FE_31(m, __VA_ARGS__))

#define LAIN_DATA_DETAIL_FOR_EACH(m, ...) \
	LAIN_DATA_DETAIL_EXPAND(LAIN_DATA_DETAIL_CAT(LAIN_DATA_DETAIL_FE_, LAIN_DATA_DETAIL_NARG(__VA_ARGS__))(m, __VA_ARGS__))

#define LAIN_DATA_DETAIL_MEMBER(f) ar.member(#f, v.f);
#define LAIN_DATA_DETAIL_MEMBER_THIS(f) ar.member(#f, this->f);

// A free serialize(): field name = JSON key. Invoke at the type's namespace scope.
#define LAIN_SERIALIZE(Type, ...)                                       \
	inline void serialize(::lain::data::Archive& ar, Type& v)           \
	{                                                                   \
		LAIN_DATA_DETAIL_FOR_EACH(LAIN_DATA_DETAIL_MEMBER, __VA_ARGS__) \
	}

// A member serialize() — the private-member escape hatch. Invoke inside the class body.
#define LAIN_SERIALIZE_INTRUSIVE(...)                                        \
	void serialize(::lain::data::Archive& ar)                                \
	{                                                                        \
		LAIN_DATA_DETAIL_FOR_EACH(LAIN_DATA_DETAIL_MEMBER_THIS, __VA_ARGS__) \
	}

// The stable discriminator key for one std::variant arm. Invoke at namespace scope (ADL finds it
// via the arm type). Not meta::typeName — that is display-only / unstable across compilers.
#define LAIN_SERIALIZE_VARIANT_ARM(Type, Key) \
	constexpr ::std::string_view variantArmKey(::lain::data::VariantArm<Type>) { return Key; }
