// Tests for lain::app. The headless path (an ApplicationDelegate that opens no
// windows -> onProcess runs once) is exercised end-to-end through a real flow Graph
// and through the CLI hook — no driver needed. The windowed cases (the smoke, and the
// shutdown drain) open a real window on the live driver and are opt-in (LAIN_GUI_SMOKE=1)
// so GUI-less / driver-less CI stays green.

#include "lain/app/application.h"
#include "lain/app/applicationdelegate.h"
#include "lain/app/cli.h"
#include "lain/app/window.h"
#include "lain/app/windowdelegate.h"

#include <lain/core/range.h>
#include <lain/flow/evaluation.h>
#include <lain/flow/graph.h>
#include <lain/flow/scheduler.h>
#include <lain/log/log.h>
#include <lain/meta/enums.h>
#include <lain/task/task.h>

#include <archimedes/archimedes.h>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string>
#include <thread>
#include <vector>

using namespace lain;

namespace
{
	struct ConstInt : flow::Node
	{
		int value;
		flow::PortId out;
		explicit ConstInt(int v)
			: Node("ConstInt")
			, value(v)
		{
			out = addOutput<int>("value");
		}
		std::unique_ptr<flow::Node> clone() const override { return std::make_unique<ConstInt>(*this); }
		void compute(flow::NodeEvaluation& evaluation) const override { evaluation.output(out).set(value); }
	};

	struct AddInt : flow::Node
	{
		flow::PortId a, b, sum;
		AddInt()
			: Node("Add")
		{
			a = addInput<int>("a");
			b = addInput<int>("b");
			sum = addOutput<int>("sum");
		}
		std::unique_ptr<flow::Node> clone() const override { return std::make_unique<AddInt>(*this); }
		void compute(flow::NodeEvaluation& evaluation) const override
		{
			evaluation.output(sum).set(evaluation.input(a).get<int>() + evaluation.input(b).get<int>());
		}
	};

	// Headless delegate: opens no windows, so run() invokes onProcess once.
	struct GraphApp : app::ApplicationDelegate
	{
		int result = -1;

		bool onStart(app::Application&) override { return true; } // no windows -> headless

		int onProcess(app::Application&) override
		{
			flow::Graph g;
			const flow::NodeId c1 = g.add<ConstInt>(2);
			const flow::NodeId c2 = g.add<ConstInt>(3);
			const flow::NodeId add = g.add<AddInt>();
			g.connect(c1, 0, add, 0);
			g.connect(c2, 0, add, 1);
			flow::Evaluation e{g};
			flow::SerialScheduler{}.run(g, e);
			result = e.value(flow::PortAddress{add, g.node(add).output(0).id()}).get<int>();
			return 0;
		}
	};

	// Windowed smoke: clear the frame.
	struct ClearWindow : app::WindowDelegate
	{
		void onRender(app::Window& window, const app::TimeState&) override
		{
			window.renderer().render([](acm::CommandBuffer, uint32_t) {});
		}
	};

	struct SmokeApp : app::ApplicationDelegate
	{
		ClearWindow window;

		bool onStart(app::Application& app) override
		{
			app::WindowSpec spec;
			spec.title = "lain::app smoke";
			spec.width = 320;
			spec.height = 240;
			app.createWindow(spec, window);
			return true;
		}

		void onUpdate(app::Application& app, const app::TimeState& time, const app::InputState&) override
		{
			if (time.frame >= 3)
				app.exit();
		}
	};

	// onShutdown is where a delegate releases what its frames read, including objects acm does not
	// own and so cannot defer: ImGui's backend frees its buffers, font image and pipeline there,
	// and freeing one while the last frame still runs is a GPU page fault — flowview quitting at
	// --frames 1 lost the device every time. So the frames must be FINISHED by then, not merely
	// submitted. The last frame hands the GPU a real copy and onShutdown asks whether it landed;
	// the copy is large so that, undrained, it is still running when the CPU gets there.
	struct DrainProbe : app::WindowDelegate
	{
		static constexpr std::uint32_t kSize = 2048;
		static constexpr std::uint8_t kFill = 0xA5;
		std::uint64_t lastFrame = 0;
		acm::Texture source;
		acm::Buffer readback;
		bool landed = false;

		bool onInit(app::Window& window) override
		{
			acm::Device& device = window.app().device();
			const std::size_t bytes = std::size_t{kSize} * kSize * 4;
			source = device.createTexture(acm::Format::R8G8B8A8_Unorm, acm::Extent2D{kSize, kSize});
			readback = device.createBuffer(bytes, acm::BufferUsage::TransferDst);
			if (!source.valid() || !readback.valid())
				return false;
			const std::vector<std::uint8_t> pixels(bytes, kFill);
			source.upload(pixels.data(), pixels.size()); // synchronous; leaves it ShaderReadOnly
			const std::vector<std::uint8_t> zeros(bytes, 0);
			return !readback.write(zeros.data(), zeros.size());
		}

		void onRender(app::Window& window, const app::TimeState& time) override
		{
			if (time.frame != lastFrame)
			{
				window.renderer().render([](acm::CommandBuffer&, uint32_t) {});
				return;
			}
			// A copy is a transfer, so it goes in the pre-pass, outside the rendering scope.
			window.renderer().render(
				[this](acm::CommandBuffer& cmd, uint32_t)
				{
					cmd.transitionImage(source, acm::ImageLayout::ShaderReadOnly, acm::ImageLayout::TransferSrc);
					cmd.copyTextureToBuffer(source, readback);
				},
				[](acm::CommandBuffer&, uint32_t) {});
		}

		void onShutdown(app::Window&) override
		{
			const auto* bytes = static_cast<const std::uint8_t*>(readback.map());
			landed = bytes != nullptr && std::all_of(bytes, bytes + readback.size(), [](std::uint8_t b)
													 { return b == kFill; });
			readback.unmap();
			readback.reset();
			source.reset();
		}
	};

	struct DrainApp : app::ApplicationDelegate
	{
		DrainProbe window;

		bool onStart(app::Application& app) override
		{
			app::WindowSpec spec;
			spec.title = "lain::app drain";
			spec.width = 320;
			spec.height = 240;
			app.createWindow(spec, window);
			return true;
		}

		// Quit in the second frame, as `flowview --frames 1` does: the frame that asks to stop is
		// still rendered, so it is the last one, and the one that holds the copy.
		void onUpdate(app::Application& app, const app::TimeState& time, const app::InputState&) override
		{
			if (time.frame >= window.lastFrame)
				app.exit();
		}
	};
} // namespace

// The three phases in the order main writes them, so a case reads as one line and the suite
// drives the same sequence a real app does. Named runApp rather than run so it cannot be
// mistaken for Application::run, which is only the middle phase.
static int runApp(app::Application& app, int argc, char** argv)
{
	if (const std::optional<int> code = app.initialise(argc, argv))
		return *code;
	const int status = app.run();
	app.shutdown();
	return status;
}

TEST_CASE("headless: a windowless app runs onProcess and its graph", "[app]")
{
	GraphApp delegate;
	app::Application app(delegate, {"test-app", {0, 0, 0}});
	char arg0[] = "test-app";
	char* argv[] = {arg0};

	REQUIRE(runApp(app, 1, argv) == 0);
	REQUIRE(delegate.result == 5);
}

TEST_CASE("cli: the delegate registers options the base app parses", "[app]")
{
	struct FlagApp : app::ApplicationDelegate
	{
		bool flag = false;
		int value = 0;
		bool onInit(app::Application&, app::cli::App& cli) override
		{
			cli.add_flag("--flag", flag, "an example flag");
			cli.add_option("--value", value, "a value");
			return true;
		}
		bool onStart(app::Application&) override { return true; }
	} delegate;

	app::Application app(delegate, {"test-app", {0, 0, 0}});
	char a0[] = "test-app";
	char a1[] = "--flag";
	char a2[] = "--value";
	char a3[] = "42";
	char* argv[] = {a0, a1, a2, a3};

	REQUIRE(runApp(app, 4, argv) == 0);
	REQUIRE(delegate.flag);
	REQUIRE(delegate.value == 42);
}

TEST_CASE("cli: -v/--verbose raises the log level", "[app][log]")
{
	GraphApp delegate; // headless; we only care that run() applies the flag
	app::Application app(delegate, {"test-app", {0, 0, 0}});

	log::setLevel(log::Level::Info); // known starting point

	SECTION("no flag leaves the level untouched")
	{
		char a0[] = "test-app";
		char* argv[] = {a0};
		REQUIRE(runApp(app, 1, argv) == 0);
		REQUIRE(app.verbosity() == 0);
		REQUIRE(log::level() == log::Level::Info);
	}
	SECTION("-v selects debug")
	{
		char a0[] = "test-app";
		char a1[] = "-v";
		char* argv[] = {a0, a1};
		REQUIRE(runApp(app, 2, argv) == 0);
		REQUIRE(app.verbosity() == 1);
		REQUIRE(log::level() == log::Level::Debug);
	}
	SECTION("-vv selects trace")
	{
		char a0[] = "test-app";
		char a1[] = "-vv";
		char* argv[] = {a0, a1};
		REQUIRE(runApp(app, 2, argv) == 0);
		REQUIRE(app.verbosity() == 2);
		REQUIRE(log::level() == log::Level::Trace);
	}

	log::setLevel(log::Level::Info); // restore for other tests
}

TEST_CASE("cli: an enum option is validated from the enum's names", "[app]")
{
	enum class Mode
	{
		Off,
		Fast,
		Slow,
	};

	// The idiomatic CLI11 pattern, built from lain::meta::enums::nameValueMap — no
	// special-case wrapper, just add_option(...)->transform(CheckedTransformer(...)).
	struct EnumApp : app::ApplicationDelegate
	{
		Mode mode = Mode::Off;
		bool onInit(app::Application&, app::cli::App& cli) override
		{
			cli.add_option("--mode", mode, "run mode")
				->transform(app::cli::CheckedTransformer(meta::enums::nameValueMap<Mode>(), app::cli::ignore_case));
			return true;
		}
		bool onStart(app::Application&) override { return true; }
	} delegate;

	app::Application app(delegate, {"test-app", {0, 0, 0}});

	SECTION("a valid name (case-insensitive) maps to its enumerator")
	{
		char a0[] = "test-app";
		char a1[] = "--mode";
		char a2[] = "fast"; // lower-case still matches Fast
		char* argv[] = {a0, a1, a2};
		REQUIRE(runApp(app, 3, argv) == 0);
		REQUIRE(delegate.mode == Mode::Fast);
	}
	SECTION("an unknown value is rejected by the parse")
	{
		char a0[] = "test-app";
		char a1[] = "--mode";
		char a2[] = "sideways";
		char* argv[] = {a0, a1, a2};
		REQUIRE(runApp(app, 3, argv) != 0); // CLI11 validation failure -> non-zero exit
		REQUIRE(delegate.mode == Mode::Off);
	}
}

TEST_CASE("cli: re-registering a reserved flag fails gracefully", "[app]")
{
	// A delegate that re-registers a framework-reserved flag makes CLI11 throw on
	// construction; run() must report it and return 1, not terminate.
	struct BadApp : app::ApplicationDelegate
	{
		bool dummy = false;
		bool onInit(app::Application&, app::cli::App& cli) override
		{
			cli.add_flag("--verbose", dummy, "collides with the reserved flag");
			return true;
		}
		bool onStart(app::Application&) override { return true; }
	} delegate;

	app::Application app(delegate, {"test-app", {0, 0, 0}});
	char a0[] = "test-app";
	char* argv[] = {a0};
	REQUIRE(runApp(app, 1, argv) == 1);
}

TEST_CASE("gui: opens a window and renders frames", "[app][gpu]")
{
	if (!std::getenv("LAIN_GUI_SMOKE"))
		SKIP("set LAIN_GUI_SMOKE=1 to run the windowed smoke (needs a display + driver)");

	SmokeApp delegate;
	app::Application app(delegate, {"test-app", {0, 0, 0}});
	char arg0[] = "test-app";
	char* argv[] = {arg0};

	REQUIRE(runApp(app, 1, argv) == 0);
}

TEST_CASE("gui: a window delegate shuts down after the GPU has finished its frames", "[app][gpu]")
{
	if (!std::getenv("LAIN_GUI_SMOKE"))
		SKIP("set LAIN_GUI_SMOKE=1 to run the windowed smoke (needs a display + driver)");

	DrainApp delegate;
	delegate.window.lastFrame = 1;
	app::Application app(delegate, {"test-app", {0, 0, 0}});
	char arg0[] = "test-app";
	char* argv[] = {arg0};

	REQUIRE(runApp(app, 1, argv) == 0);
	REQUIRE(delegate.window.landed);
}

TEST_CASE("headless: onProcess's status is the process's exit code", "[app]")
{
	// The whole reason the hook returns a status: a caller who sees only the exit code must be able
	// to tell a run that did its work from one that failed partway (a render stopped on a missing
	// frame, say). Aborting from onStart already returns 1; this covers failing after the work
	// began.
	struct FailingApp : app::ApplicationDelegate
	{
		int status = 0;
		bool onStart(app::Application&) override { return true; } // no windows -> headless
		int onProcess(app::Application&) override { return status; }
	} delegate;

	char arg0[] = "test-app";
	char* argv[] = {arg0};

	SECTION("success reports 0")
	{
		delegate.status = 0;
		app::Application app(delegate, {"test-app", {0, 0, 0}});
		REQUIRE(runApp(app, 1, argv) == 0);
	}
	SECTION("a failure reaches the caller")
	{
		delegate.status = 3;
		app::Application app(delegate, {"test-app", {0, 0, 0}});
		REQUIRE(runApp(app, 1, argv) == 3);
	}
}

TEST_CASE("a delegate's cli pointers stay valid past initialise", "[app]")
{
	// add_subcommand hands back a raw cli::App* owned by the parser, and a delegate is entitled
	// to keep it: flowview's `run` and `list` store theirs in onInit and read them in onProcess.
	// So the parser has to outlive the phase that built it.
	//
	// This is a regression test for a real defect, not a hypothetical. Splitting run(argc, argv)
	// into initialise/run/shutdown left the cli::App a local of initialise, so every later phase
	// read freed memory — and the symptom was a HANG in onProcess with nothing for a compiler to
	// object to, while --version still worked because it exits before anything reads a pointer.
	struct SubcommandApp : app::ApplicationDelegate
	{
		app::cli::App* sub = nullptr;
		std::string seenName;
		bool sawParse = false;

		bool onInit(app::Application&, app::cli::App& cli) override
		{
			sub = cli.add_subcommand("work", "a subcommand whose handle outlives onInit");
			return true;
		}
		bool onStart(app::Application&) override { return true; } // no windows -> headless
		int onProcess(app::Application&) override
		{
			// Reading through the stored pointer is the whole point: under the defect this is a
			// use-after-free, so the assertions below are about it being READABLE at all.
			seenName = sub->get_name();
			sawParse = sub->parsed();
			return 0;
		}
	} delegate;

	char arg0[] = "test-app";
	char work[] = "work";
	char* argv[] = {arg0, work};

	app::Application app(delegate, {"test-app", {0, 0, 0}});
	REQUIRE(runApp(app, 2, argv) == 0);
	REQUIRE(delegate.seenName == "work");
	REQUIRE(delegate.sawParse); // the subcommand named on the command line was matched
}

TEST_CASE("initialise answers carry-on, stop-with-0, or stop-with-a-failure", "[app]")
{
	// Three outcomes is why initialise returns an optional and not an int: --licenses and a
	// refused command line BOTH end the process, and a bare 0 could not tell the first of them
	// from "carry on". Nothing asked this before the lifecycle was split.
	struct PlainApp : app::ApplicationDelegate
	{
		bool onStart(app::Application&) override { return true; }
		int onProcess(app::Application&) override { return 0; }
	};

	char arg0[] = "test-app";

	SECTION("a plain command line carries on")
	{
		PlainApp delegate;
		char* argv[] = {arg0};
		app::Application app(delegate, {"test-app", {0, 0, 0}});
		REQUIRE_FALSE(app.initialise(1, argv).has_value()); // nullopt: run() is next
		app.shutdown();
	}
	SECTION("--licenses answers, and answers with success")
	{
		PlainApp delegate;
		char licenses[] = "--licenses";
		char* argv[] = {arg0, licenses};
		app::Application app(delegate, {"test-app", {0, 0, 0}});
		const std::optional<int> code = app.initialise(2, argv);
		REQUIRE(code.has_value());
		REQUIRE(*code == 0);
	}
	SECTION("a command line it refuses answers with a failure")
	{
		PlainApp delegate;
		char bogus[] = "--no-such-flag";
		char* argv[] = {arg0, bogus};
		app::Application app(delegate, {"test-app", {0, 0, 0}});
		const std::optional<int> code = app.initialise(2, argv);
		REQUIRE(code.has_value());
		REQUIRE(*code != 0);
	}
}

TEST_CASE("shutdown is total: idempotent, and a no-op on a run that never started", "[app]")
{
	// The destructor calls shutdown() if the caller did not, so shutdown() has to be safe on
	// every path a caller can leave by — including the two where nothing was ever brought up.
	// Without that the net would be worse than no net: it would tear down state that is not there.
	struct PlainApp : app::ApplicationDelegate
	{
		bool onStart(app::Application&) override { return true; }
		int onProcess(app::Application&) override { return 0; }
	};

	char arg0[] = "test-app";

	SECTION("twice after a real run")
	{
		PlainApp delegate;
		char* argv[] = {arg0};
		app::Application app(delegate, {"test-app", {0, 0, 0}});
		REQUIRE_FALSE(app.initialise(1, argv).has_value());
		REQUIRE(app.run() == 0);
		app.shutdown();
		app.shutdown(); // the second one must do nothing at all
	}
	SECTION("after an initialise that answered instead")
	{
		PlainApp delegate;
		char version[] = "--licenses";
		char* argv[] = {arg0, version};
		app::Application app(delegate, {"test-app", {0, 0, 0}});
		REQUIRE(app.initialise(2, argv).has_value());
		app.shutdown(); // nothing was started, so there is nothing to stop
	}
	SECTION("on an Application that was never initialised")
	{
		PlainApp delegate;
		app::Application app(delegate, {"test-app", {0, 0, 0}});
		app.shutdown();
	}
}

TEST_CASE("the task pool is running by onProcess, and --threads sizes it", "[app]")
{
	// lain::app owns the process pool (ADR-0024), so a delegate must find it already started —
	// otherwise every task::parallel / each / async in the program silently runs inline and the
	// only symptom is that nothing is faster. Nothing else in the tree asks whether --threads is
	// CONNECTED: the option could parse into a member that reaches no one and every other test
	// would still pass.
	struct PoolApp : app::ApplicationDelegate
	{
		std::size_t workers = 0;
		bool sawParallel = false;
		bool onStart(app::Application&) override { return true; } // no windows -> headless
		int onProcess(app::Application&) override
		{
			workers = lain::task::threadCount();
			int a = 0, b = 0;
			lain::task::parallel([&]
								 { a = 1; }, [&]
								 { b = 2; });
			sawParallel = (a == 1 && b == 2);
			return 0;
		}
	} delegate;

	char arg0[] = "test-app";
	char threadsFlag[] = "--threads";

	SECTION("the default starts workers")
	{
		char* argv[] = {arg0};
		app::Application app(delegate, {"test-app", {0, 0, 0}});
		REQUIRE(runApp(app, 1, argv) == 0);
		// multi's default is hardware_concurrency() - 1; on a single-core host that is 0, and the
		// inline path is then correct rather than a failure — so this asserts the pairing, not a
		// number the machine happens to have.
		if (std::thread::hardware_concurrency() > 1)
			REQUIRE(delegate.workers == std::thread::hardware_concurrency() - 1);
		REQUIRE(delegate.sawParallel);
	}
	SECTION("--threads sizes it")
	{
		char count[] = "3";
		char* argv[] = {arg0, threadsFlag, count};
		app::Application app(delegate, {"test-app", {0, 0, 0}});
		REQUIRE(runApp(app, 3, argv) == 0);
		REQUIRE(delegate.workers == 3);
		REQUIRE(delegate.sawParallel);
	}
	SECTION("--threads 0 leaves it inactive, and dispatch still runs")
	{
		char zero[] = "0";
		char* argv[] = {arg0, threadsFlag, zero};
		app::Application app(delegate, {"test-app", {0, 0, 0}});
		REQUIRE(runApp(app, 3, argv) == 0);
		REQUIRE(delegate.workers == 0);
		REQUIRE(delegate.sawParallel); // inline on the caller, which is the documented behaviour
	}
}

TEST_CASE("cli: a custom option type converts through its own lexical_cast", "[app]")
{
	// core::Range is a TYPED option, the same way an enum is: the value type carries its own
	// literal form and CLI11 finds the conversion by ADL, so the app registers the option and
	// nothing stages a string to be parsed somewhere else later.
	struct RangeApp : app::ApplicationDelegate
	{
		lain::core::Range range;
		bool onInit(app::Application&, app::cli::App& cli) override
		{
			cli.add_option("--frame", range, "a frame range");
			return true;
		}
		bool onStart(app::Application&) override { return true; }
	} delegate;

	app::Application app(delegate, {"test-app", {0, 0, 0}});

	SECTION("a well-formed range parses into the value")
	{
		char a0[] = "test-app";
		char a1[] = "--frame";
		char a2[] = "0-9x3";
		char* argv[] = {a0, a1, a2};
		REQUIRE(runApp(app, 3, argv) == 0);
		REQUIRE(delegate.range == lain::core::Range{0, 9, 3});
	}
	SECTION("a malformed one is refused by the PARSE, before any work begins")
	{
		// This is what the typed option buys over a std::string member: the command line rejects
		// "10-2" itself, so no later layer has to re-check it and none can disagree about the
		// syntax.
		char a0[] = "test-app";
		char a1[] = "--frame";
		char a2[] = "10-2"; // reversed
		char* argv[] = {a0, a1, a2};
		REQUIRE(runApp(app, 3, argv) != 0);
	}
}
