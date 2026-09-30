#include "lain/camera/board/pattern.h"

#include <cmath>
#include <string>

namespace lain::camera::board
{
	// --- file-local helpers (named static, not an anonymous namespace) ----------

	// A ratio as whole millionths: the resolution the fingerprint records it at, which is also what
	// lets the description be written without formatting a double (locale-free and exact).
	static long long millionths(double ratio)
	{
		return std::llround(ratio * 1e6);
	}

	static const char* layoutName(CharucoLayout layout)
	{
		return layout == CharucoLayout::Legacy ? "legacy" : "standard";
	}

	// --- dictionaries -------------------------------------------------------------

	std::uint32_t markerCapacity(Dictionary dictionary)
	{
		switch (dictionary)
		{
			case Dictionary::Aruco4x4_50:
			case Dictionary::Aruco5x5_50:
			case Dictionary::Aruco6x6_50:
			case Dictionary::Aruco7x7_50:
				return 50;
			case Dictionary::Aruco4x4_100:
			case Dictionary::Aruco5x5_100:
			case Dictionary::Aruco6x6_100:
			case Dictionary::Aruco7x7_100:
				return 100;
			case Dictionary::Aruco4x4_250:
			case Dictionary::Aruco5x5_250:
			case Dictionary::Aruco6x6_250:
			case Dictionary::Aruco7x7_250:
				return 250;
			case Dictionary::Aruco4x4_1000:
			case Dictionary::Aruco5x5_1000:
			case Dictionary::Aruco6x6_1000:
			case Dictionary::Aruco7x7_1000:
				return 1000;
		}
		return 0;
	}

	std::string_view name(Dictionary dictionary)
	{
		switch (dictionary)
		{
			case Dictionary::Aruco4x4_50:
				return "ARUCO_4X4_50";
			case Dictionary::Aruco4x4_100:
				return "ARUCO_4X4_100";
			case Dictionary::Aruco4x4_250:
				return "ARUCO_4X4_250";
			case Dictionary::Aruco4x4_1000:
				return "ARUCO_4X4_1000";
			case Dictionary::Aruco5x5_50:
				return "ARUCO_5X5_50";
			case Dictionary::Aruco5x5_100:
				return "ARUCO_5X5_100";
			case Dictionary::Aruco5x5_250:
				return "ARUCO_5X5_250";
			case Dictionary::Aruco5x5_1000:
				return "ARUCO_5X5_1000";
			case Dictionary::Aruco6x6_50:
				return "ARUCO_6X6_50";
			case Dictionary::Aruco6x6_100:
				return "ARUCO_6X6_100";
			case Dictionary::Aruco6x6_250:
				return "ARUCO_6X6_250";
			case Dictionary::Aruco6x6_1000:
				return "ARUCO_6X6_1000";
			case Dictionary::Aruco7x7_50:
				return "ARUCO_7X7_50";
			case Dictionary::Aruco7x7_100:
				return "ARUCO_7X7_100";
			case Dictionary::Aruco7x7_250:
				return "ARUCO_7X7_250";
			case Dictionary::Aruco7x7_1000:
				return "ARUCO_7X7_1000";
		}
		return "?";
	}

	// --- Pattern ------------------------------------------------------------------

	Pattern::Pattern(const PatternParameters& parameters)
		: m_parameters(parameters)
		, m_fingerprint(core::sha256(description()))
	{
	}

	PatternResult Pattern::create(const PatternParameters& p)
	{
		PatternResult result;
		const auto problem = [&result](PatternProblem kind, std::string detail)
		{ result.diagnostics.push_back({kind, std::move(detail)}); };

		if (p.squaresX < 2 || p.squaresY < 2)
			problem(PatternProblem::TooFewSquares, "a ChArUco board needs at least 2x2 squares, not " +
													   std::to_string(p.squaresX) + "x" + std::to_string(p.squaresY));

		// Strictly inside (0, 1) at the fingerprint's resolution, so two patterns that print
		// identically cannot fingerprint apart and a marker never fills its square.
		const bool ratioValid = std::isfinite(p.markerToSquare) && millionths(p.markerToSquare) >= 1 &&
								millionths(p.markerToSquare) <= 999999;
		if (!ratioValid)
			problem(PatternProblem::MarkerRatioOutOfRange,
					"markerToSquare must lie strictly between 0 and 1, to a millionth; it is " + std::to_string(p.markerToSquare));

		const std::uint64_t markers = std::uint64_t(p.squaresX) * p.squaresY / 2;
		const std::uint64_t capacity = markerCapacity(p.dictionary);
		if (std::uint64_t(p.firstMarkerId) + markers > capacity)
			problem(PatternProblem::DictionaryTooSmall,
					std::to_string(markers) + " markers from id " + std::to_string(p.firstMarkerId) + " do not fit in " +
						std::string(name(p.dictionary)) + ", which holds " + std::to_string(capacity));

		if (result.diagnostics.empty())
			result.pattern = Pattern{p};
		return result;
	}

	std::string Pattern::description() const
	{
		// One "key value" per line. The first line names the format and its version; changing any
		// line's meaning is a new version, because this text is what the fingerprint hashes.
		const long long ratio = millionths(m_parameters.markerToSquare);
		std::string digits = std::to_string(ratio);
		digits.insert(0, 6 - digits.size(), '0'); // 750000 -> "0.750000"; 5 -> "0.000005"
		return "lain.board.charuco/1\n"
			   "dictionary " +
			   std::string(name(m_parameters.dictionary)) + "\nsquares " + std::to_string(m_parameters.squaresX) + "x" +
			   std::to_string(m_parameters.squaresY) + "\nmarkerToSquare 0." + digits + "\nfirstMarkerId " +
			   std::to_string(m_parameters.firstMarkerId) + "\nlayout " + layoutName(m_parameters.layout) + "\n";
	}
} // namespace lain::camera::board
