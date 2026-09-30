#pragma once

#include <cstdint>

namespace lain::core
{
	// A physical length: internally an exact signed int64 count of nanometres, read and written in
	// named units. The camera code states board dimensions and other metric evidence in it rather
	// than in unitless doubles.
	//
	// EXACT storage is the point, as it is for core::Time. Two lengths that are the same quantity
	// compare equal however they were built: fromMillimetres(24) == fromMetres(0.024), and 0.1 mm +
	// 0.2 mm == 0.3 mm, which a double in metres gets wrong. The unit a value was typed in is
	// display metadata, never part of its identity. One nanometre resolution covers the finest
	// measurement a printed board could carry; the int64 range is about +/-9.2e9 m.
	class Length
	{
	public:
		constexpr Length() = default;

		// Exact.
		static constexpr Length fromNanometres(std::int64_t nanometres) { return Length{nanometres}; }

		// Rounded to the nearest nanometre.
		//
		// PRECONDITION: finite and within the representable range. A NaN or an infinity is a
		// programming error, not data: whatever turns untrusted input into a Length (a document
		// load, a node parameter) checks it and refuses it first. So it asserts in debug, and in
		// release it is made defined rather than undefined, NaN becoming zero and anything out of
		// range saturating. core is std-only, so this is <cassert>, as core::Range does.
		static Length fromMicrometres(double micrometres) { return Length{toNanometres(micrometres, 1e3)}; }
		static Length fromMillimetres(double millimetres) { return Length{toNanometres(millimetres, 1e6)}; }
		static Length fromMetres(double metres) { return Length{toNanometres(metres, 1e9)}; }

		constexpr std::int64_t nanometres() const { return m_nm; }
		double micrometres() const { return static_cast<double>(m_nm) * 1e-3; }
		double millimetres() const { return static_cast<double>(m_nm) * 1e-6; }
		double metres() const { return static_cast<double>(m_nm) * 1e-9; }

		constexpr Length operator+(Length rhs) const { return Length{m_nm + rhs.m_nm}; }
		constexpr Length operator-(Length rhs) const { return Length{m_nm - rhs.m_nm}; }
		constexpr Length operator-() const { return Length{-m_nm}; }
		// Scaled through metres and re-rounded, so a fractional factor does not truncate.
		Length operator*(double scale) const { return fromMetres(metres() * scale); }
		// A length divided by a length is a plain number: the ratio of two quantities.
		double operator/(Length rhs) const { return static_cast<double>(m_nm) / static_cast<double>(rhs.m_nm); }

		constexpr bool operator==(Length rhs) const { return m_nm == rhs.m_nm; }
		constexpr bool operator!=(Length rhs) const { return m_nm != rhs.m_nm; }
		constexpr bool operator<(Length rhs) const { return m_nm < rhs.m_nm; }
		constexpr bool operator<=(Length rhs) const { return m_nm <= rhs.m_nm; }
		constexpr bool operator>(Length rhs) const { return m_nm > rhs.m_nm; }
		constexpr bool operator>=(Length rhs) const { return m_nm >= rhs.m_nm; }

	private:
		explicit constexpr Length(std::int64_t nanometres)
			: m_nm(nanometres)
		{
		}

		// `value * scale` rounded to the nearest nanometre, under the precondition above.
		static std::int64_t toNanometres(double value, double scale);

		std::int64_t m_nm = 0;
	};
} // namespace lain::core
