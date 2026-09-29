// The runtime half of the OpenCV gate (ADR-0026).
//
// cmake/addOpenCV.cmake refuses an archive whose MANIFEST.txt is not the pinned configuration, at
// configure time and without executing anything. This asks the LINKED library the same questions
// through its own API — the same contract opencv-prebuilt's check.sh enforces before publishing —
// so two independent things must be wrong at once for an unapproved OpenCV to reach a build. For an
// OpenCV supplied through LAIN_OPENCV_ROOT, which has no manifest, this is the only gate there is:
// a typical system install fails it, by design, because it is not the configuration lain reviewed.
//
// Not a [gpu]-style skip-aware test: there is nothing to be absent. If this binary runs at all, the
// library loaded, and a wrong answer is a real failure.

#include <lain/camera/opencv/build.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <sstream>
#include <string>
#include <vector>

using namespace lain::camera::opencv;

namespace
{
	std::string trim(const std::string& text)
	{
		const auto first = text.find_first_not_of(" \t\r");
		if (first == std::string::npos)
			return {};
		const auto last = text.find_last_not_of(" \t\r");
		return text.substr(first, last - first + 1);
	}

	std::vector<std::string> lines(const std::string& text)
	{
		std::vector<std::string> out;
		std::istringstream stream(text);
		for (std::string line; std::getline(stream, line);)
			out.push_back(trim(line));
		return out;
	}

	// The value after "<label>:" on the first line that starts with it, trimmed; empty when absent.
	std::string field(const std::string& info, const std::string& label)
	{
		for (const std::string& line : lines(info))
			if (line.rfind(label + ":", 0) == 0)
				return trim(line.substr(label.size() + 1));
		return {};
	}

	// Words of a space-separated list, sorted, so two orderings of one set compare equal.
	std::vector<std::string> sortedWords(const std::string& text)
	{
		std::vector<std::string> out;
		std::istringstream stream(text);
		for (std::string word; stream >> word;)
			out.push_back(word);
		std::sort(out.begin(), out.end());
		return out;
	}
} // namespace

TEST_CASE("the linked OpenCV is the pinned version", "[opencv][build]")
{
	// An empty report would make every assertion below vacuously true — the failure mode a gate
	// must not have.
	REQUIRE_FALSE(buildInformation().empty());
	CHECK(version() == LAIN_OPENCV_PINNED_VERSION);
}

TEST_CASE("the linked OpenCV is shared, with exactly the pinned modules", "[opencv][build]")
{
	const std::string info = buildInformation();

	// Shared is ADR-0004's amendment: OpenCV's bundled zlib stays hidden inside opencv_core only
	// because the library is a DSO with hidden visibility.
	CHECK(field(info, "Built as dynamic libs?") == "YES");

	// Exactly, not "at least": a module nobody asked for is code nobody reviewed.
	CHECK(sortedWords(field(info, "To be built")) == sortedWords(LAIN_OPENCV_PINNED_MODULES));
	CHECK(field(info, "Non-free algorithms") == "NO");
}

TEST_CASE("the linked OpenCV carries no optional third party", "[opencv][build]")
{
	const std::string info = buildInformation();

	// zlib is compiled in from OpenCV's own source ("build (ver 1.3.2)"). A path here instead means
	// it links the system's libz, which is exactly what OpenCV does on Linux by default.
	CHECK(field(info, "ZLib").rfind("build (ver ", 0) == 0);

	// The built-in thread pool, never TBB or OpenMP: pthreads on Linux, GCD on macOS, Concurrency
	// on Windows. lain's one-pool rule (ADR-0024) is M9 slice 1's to reconcile with it.
	const std::string parallel = field(info, "Parallel framework");
	CHECK((parallel == "pthreads" || parallel == "GCD" || parallel == "Concurrency"));

	// The "Other third-party libraries" section lists every optional library that was considered;
	// with everything off it holds only "Custom HAL: NO".
	std::vector<std::string> others;
	bool inSection = false;
	for (const std::string& line : lines(info))
	{
		if (line == "Other third-party libraries:")
		{
			inSection = true;
			continue;
		}
		if (!inSection)
			continue;
		if (line.empty())
			break;
		if (field(line, "Custom HAL") == "NO")
			continue;
		others.push_back(line);
	}
	INFO("unexpected entries in 'Other third-party libraries'");
	CHECK(others.empty());

	// Anywhere in the report, a forbidden component may appear only as NO. Eigen (MPL-2.0) and
	// Intel IPP are the licence-relevant ones; the rest would make results depend on the machine.
	const std::vector<std::string> forbidden = {"Intel IPP", "IPP", "Eigen", "Lapack", "OpenCL", "TBB", "OpenMP",
												"ITT", "Halide", "OpenVINO", "KleidiCV", "Carotene", "FastCV",
												"ARM Perf Lib", "Protobuf", "NVIDIA CUDA", "OpenGL", "Vulkan", "VA"};
	for (const std::string& label : forbidden)
	{
		const std::string value = field(info, label);
		INFO(label << ": " << value);
		CHECK((value.empty() || value == "NO"));
	}
}
