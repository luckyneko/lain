#pragma once

#include "lain/core/details/rounding.h"

#include <cstdint>
#include <ratio>

namespace lain::core
{
	// A physical length: internally an exact signed int64 count of nanometres, read and written in
	// units the way core::Time is. The camera code states board dimensions and other metric evidence
	// in it rather than in unitless doubles.
	//
	// EXACT storage is the point, as it is for core::Time. Two lengths that are the same quantity
	// compare equal however they were built: from<Millimetres>(24) == from<Metres>(0.024), and
	// 0.1 mm + 0.2 mm == 0.3 mm, which a double in metres gets wrong. The unit a value was typed in is
	// display metadata, never part of its identity. One nanometre resolution covers the finest
	// measurement a printed board could carry; the int64 range is about +/-9.2e9 m.
	class Length
	{
	public:
		// Unit tags for from<>() / as<>(), each a ratio of the metre as Time's are of the second.
		using Nanometres = std::nano;
		using Micrometres = std::micro;
		using Millimetres = std::milli;
		using Metres = std::ratio<1>;

		constexpr Length() = default;

		// Build from a unit value, rounded to the nearest nanometre: Length::from<Millimetres>(23.7).
		//
		// PRECONDITION: finite and within the representable range, as detail::roundToInt64 states.
		// Whatever turns untrusted input into a Length (a document load, a node parameter) checks it
		// and refuses it first. One that gets here anyway asserts in debug; in release a NaN is zero
		// and anything out of range saturates.
		template <class Units>
		static Length from(double value)
		{
			return Length{detail::roundToInt64(value * nanometresPer<Units>())};
		}

		// Read as a unit value: length.as<Millimetres>(). It divides, as std::chrono does, rather than
		// multiplying by a reciprocal like 1e-6 that a double cannot hold exactly. A division is
		// correctly rounded, so a whole number of nanometres reads back as the double nearest its
		// decimal: what was typed as 23.7 mm reads as 23.7, which is what a document writes to disk.
		template <class Units>
		double as() const
		{
			return static_cast<double>(m_nm) / nanometresPer<Units>();
		}
		// The default external reading, as Time::seconds() is.
		double metres() const { return as<Metres>(); }

		// The representation: the exact nanometre count, as Time::chrono() is Time's. Equality and
		// order are this, where as<Nanometres>() is a double.
		constexpr std::int64_t nanometres() const { return m_nm; }

		constexpr Length operator+(Length rhs) const { return Length{m_nm + rhs.m_nm}; }
		constexpr Length operator-(Length rhs) const { return Length{m_nm - rhs.m_nm}; }
		constexpr Length operator-() const { return Length{-m_nm}; }
		// The count is scaled and re-rounded, so a fractional factor does not truncate. No unit is
		// involved: scaling a quantity is not a reading of it.
		Length operator*(double scale) const { return Length{detail::roundToInt64(static_cast<double>(m_nm) * scale)}; }
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

		// How many nanometres one of Units is: 1e6 for Millimetres.
		template <class Units>
		static constexpr double nanometresPer()
		{
			using Ratio = std::ratio_divide<Units, std::nano>;
			return static_cast<double>(Ratio::num) / static_cast<double>(Ratio::den);
		}

		std::int64_t m_nm = 0;
	};
} // namespace lain::core
