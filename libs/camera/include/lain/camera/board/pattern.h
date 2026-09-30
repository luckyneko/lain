#pragma once

#include <lain/core/sha256.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace lain::camera::board
{
	// The predefined ArUco marker dictionaries a ChArUco board can draw its markers from, named by
	// marker grid and dictionary size. More are added when a real board needs one.
	enum class Dictionary : std::uint8_t
	{
		Aruco4x4_50,
		Aruco4x4_100,
		Aruco4x4_250,
		Aruco4x4_1000,
		Aruco5x5_50,
		Aruco5x5_100,
		Aruco5x5_250,
		Aruco5x5_1000,
		Aruco6x6_50,
		Aruco6x6_100,
		Aruco6x6_250,
		Aruco6x6_1000,
		Aruco7x7_50,
		Aruco7x7_100,
		Aruco7x7_250,
		Aruco7x7_1000,
	};

	// How many distinct markers a dictionary holds.
	std::uint32_t markerCapacity(Dictionary dictionary);

	// The dictionary's stable name ("ARUCO_5X5_100"), which the pattern fingerprint uses. Stable:
	// renaming one changes the fingerprint of every pattern drawn from it.
	std::string_view name(Dictionary dictionary);

	// Which of the two ChArUco layouts a board was printed with. OpenCV changed where the first
	// marker sits on boards with an even number of rows in 4.6; boards printed before that (and by
	// tools that followed it) are Legacy. The two are different patterns: detecting one as the other
	// finds markers and no corners.
	enum class CharucoLayout : std::uint8_t
	{
		Standard,
		Legacy,
	};

	// A ChArUco pattern's parameters with nothing checked (CONTEXT.md, "Board pattern").
	struct PatternParameters
	{
		Dictionary dictionary = Dictionary::Aruco5x5_100;
		std::uint32_t squaresX = 0; // chessboard squares across
		std::uint32_t squaresY = 0; // chessboard squares down
		// A marker's side as a fraction of a square's: relative geometry, so the pattern has no
		// units. The physical square length belongs to the board INSTANCE.
		double markerToSquare = 0;
		std::uint32_t firstMarkerId = 0; // markers take consecutive ids from here
		CharucoLayout layout = CharucoLayout::Standard;
	};

	enum class PatternProblem
	{
		TooFewSquares,		   // fewer than 2 squares in a direction: no inner corner
		MarkerRatioOutOfRange, // not strictly between 0 and 1, or not finite
		DictionaryTooSmall,	   // the markers do not fit in the dictionary from firstMarkerId
	};

	struct PatternDiagnostic
	{
		PatternProblem problem;
		std::string detail;
	};

	class Pattern;
	struct PatternResult;

	// A ChArUco board pattern: the reusable layout that every physical board printed from it shares.
	// Valid by construction, and identified by its fingerprint.
	class Pattern
	{
	public:
		static PatternResult create(const PatternParameters& parameters);

		const PatternParameters& parameters() const { return m_parameters; }

		// Markers sit on the white squares: half the squares, rounded down.
		std::uint32_t markerCount() const { return m_parameters.squaresX * m_parameters.squaresY / 2; }
		// The features a detector reports are the inner chessboard corners, ids 0 to count - 1,
		// row by row from the top left.
		std::uint32_t cornerCount() const { return (m_parameters.squaresX - 1) * (m_parameters.squaresY - 1); }

		// The canonical, versioned text of everything that makes this pattern what it is. Stable by
		// contract: a change to it must change the version line, since it is what fingerprint()
		// hashes.
		std::string description() const;

		// SHA-256 of description(). Two patterns share a fingerprint exactly when every printed board
		// of one is a printed board of the other.
		const core::Sha256Digest& fingerprint() const { return m_fingerprint; }

	private:
		Pattern(const PatternParameters& parameters);

		PatternParameters m_parameters;
		core::Sha256Digest m_fingerprint;
	};

	struct PatternResult
	{
		std::optional<Pattern> pattern;
		std::vector<PatternDiagnostic> diagnostics; // empty exactly when `pattern` is set
	};
} // namespace lain::camera::board
