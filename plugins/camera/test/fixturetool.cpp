// camera-fixture-tool: what a person runs to maintain a real-camera fixture.
//
//   camera-fixture-tool template <fixture-dir> <name>
//       Write a fixture.json to fill in, for the board apps/flowview/examples/render-board.json
//       prints and two sessions "a" and "b". Refuses to overwrite one that is there.
//
//   camera-fixture-tool manifest <dataset-dir> <name> <out.json>
//       Hash every file of an extended dataset into a manifest, to commit beside the compact fixture
//       as extended.json. The dataset is then looked for as $LAIN_CAMERA_FIXTURE_DATA/<name>.
//
//   camera-fixture-tool verify <extended.json>
//       Check the dataset a committed manifest pins, against $LAIN_CAMERA_FIXTURE_DATA.
//
// Writing and verifying share manifest.cpp, so they cannot disagree about a file's hash.

#include "fixture/fixture.h"
#include "fixture/manifest.h"

#include <lain/io/data/codecs.h>

#include <cstdio>
#include <filesystem>
#include <string>

using namespace lain::camera::fixture;
namespace fs = std::filesystem;

static int usage()
{
	std::fprintf(stderr, "usage:\n"
						 "  camera-fixture-tool template <fixture-dir> <name>\n"
						 "  camera-fixture-tool manifest <dataset-dir> <name> <out.json>\n"
						 "  camera-fixture-tool verify <extended.json>\n");
	return 2;
}

int main(int argc, char** argv)
{
	lain::io::data::registerDataCodecs();
	if (argc < 2)
		return usage();
	const std::string command = argv[1];

	if (command == "template" && argc == 4)
	{
		const fs::path root = argv[2];
		std::error_code ec;
		if (fs::exists(root / kFixtureFile, ec))
		{
			std::fprintf(stderr, "'%s' already exists\n", (root / kFixtureFile).generic_string().c_str());
			return 1;
		}
		fs::create_directories(root, ec);
		if (!writeFixtureDocument(root, templateDocument(argv[3])))
			return 1;
		std::printf("wrote '%s': measure the printed square and fill in the capture records\n",
					(root / kFixtureFile).generic_string().c_str());
		return 0;
	}

	if (command == "manifest" && argc == 5)
	{
		const fs::path dataset = argv[2];
		std::error_code ec;
		if (!fs::is_directory(dataset, ec))
		{
			std::fprintf(stderr, "'%s' is not a folder\n", dataset.generic_string().c_str());
			return 1;
		}
		const Manifest manifest = manifestOf(dataset, argv[3]);
		if (!writeManifest(argv[4], manifest))
			return 1;
		std::printf("%zu files pinned as '%s'\n", manifest.files.size(), manifest.name.c_str());
		return 0;
	}

	if (command == "verify" && argc == 3)
	{
		const ManifestRead read = readManifest(argv[2]);
		for (const std::string& problem : read.problems)
			std::fprintf(stderr, "%s\n", problem.c_str());
		if (!read.manifest)
			return 1;
		const ExtendedTier tier = verifyExtended(*read.manifest, dataRootFromEnvironment());
		switch (tier.status)
		{
			case TierStatus::Verified:
				std::printf("verified: %zu files in '%s'\n", read.manifest->files.size(),
							tier.root.generic_string().c_str());
				return 0;
			case TierStatus::Unavailable:
				std::printf("unavailable: %s\n", tier.reason.c_str());
				return 0;
			case TierStatus::Mismatch:
				std::fprintf(stderr, "MISMATCH: %s\n", tier.reason.c_str());
				return 1;
		}
		return 1;
	}
	return usage();
}
