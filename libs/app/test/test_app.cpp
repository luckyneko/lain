// Tests for lain::app. The headless path (an ApplicationDelegate that opens no
// windows -> onProcess runs once) is exercised end-to-end through a real flow Graph
// and through the CLI hook — no driver needed. The windowed smoke opens a real
// window on the live driver and is opt-in (LAIN_GUI_SMOKE=1) so GUI-less / driver-
// less CI stays green.

#include <archimedes/archimedes.h>
#include <lain/app/application.h>
#include <lain/app/applicationdelegate.h>
#include <lain/app/cli.h>
#include <lain/app/window.h>
#include <lain/app/windowdelegate.h>
#include <lain/flow/graph.h>
#include <lain/log/log.h>

#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <vector>

using namespace lain;

namespace
{
	struct ConstInt : flow::Node
	{
		int value;
		flow::PortIndex out;
		explicit ConstInt(int v)
			: Node("ConstInt")
			, value(v)
		{
			out = addOutput<int>("value");
		}
		void compute() override { output(out).set(value); }
	};

	struct AddInt : flow::Node
	{
		flow::PortIndex a, b, sum;
		AddInt()
			: Node("Add")
		{
			a = addInput<int>("a");
			b = addInput<int>("b");
			sum = addOutput<int>("sum");
		}
		void compute() override { output(sum).set(input(a).get<int>() + input(b).get<int>()); }
	};

	// Headless delegate: opens no windows, so run() invokes onProcess once.
	struct GraphApp : app::ApplicationDelegate
	{
		int result = -1;

		bool onStart(app::Application&) override { return true; } // no windows -> headless

		void onProcess(app::Application&) override
		{
			flow::Graph g;
			const flow::NodeId c1 = g.add<ConstInt>(2);
			const flow::NodeId c2 = g.add<ConstInt>(3);
			const flow::NodeId add = g.add<AddInt>();
			g.connect(c1, 0, add, 0);
			g.connect(c2, 0, add, 1);
			g.run();
			result = g.node(add).output(0).get<int>();
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
				app.quit();
		}
	};
} // namespace

TEST_CASE("headless: a windowless app runs onProcess and its graph", "[app]")
{
	GraphApp delegate;
	app::Application app(delegate, {"test-app", {0, 0, 0}});
	char arg0[] = "test-app";
	char* argv[] = {arg0};

	REQUIRE(app.run(1, argv) == 0);
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

	REQUIRE(app.run(4, argv) == 0);
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
		REQUIRE(app.run(1, argv) == 0);
		REQUIRE(log::level() == log::Level::Info);
	}
	SECTION("-v selects debug")
	{
		char a0[] = "test-app";
		char a1[] = "-v";
		char* argv[] = {a0, a1};
		REQUIRE(app.run(2, argv) == 0);
		REQUIRE(log::level() == log::Level::Debug);
	}
	SECTION("-vv selects trace")
	{
		char a0[] = "test-app";
		char a1[] = "-vv";
		char* argv[] = {a0, a1};
		REQUIRE(app.run(2, argv) == 0);
		REQUIRE(log::level() == log::Level::Trace);
	}

	log::setLevel(log::Level::Info); // restore for other tests
}

TEST_CASE("gui: opens a window and renders frames", "[app][gpu]")
{
	if (!std::getenv("LAIN_GUI_SMOKE"))
		SKIP("set LAIN_GUI_SMOKE=1 to run the windowed smoke (needs a display + driver)");

	SmokeApp delegate;
	app::Application app(delegate, {"test-app", {0, 0, 0}});
	char arg0[] = "test-app";
	char* argv[] = {arg0};

	REQUIRE(app.run(1, argv) == 0);
}
