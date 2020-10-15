#include <cassert>
#include <deai/c++/builtins.hh>
#include <deai/c++/deai.hh>

#include <unistd.h>
#include <climits>

using namespace deai::type;
thread_local static int result = 0;
auto test_function(int a) -> int {
	result = a;
	return a;
}
DEAI_CPP_PLUGIN_ENTRY_POINT(di) {
	auto log = *di["log"];

	// Copy
	auto log2 = log.object_ref();
	assert(log.is<Ref<Object>>());

	// Move
	auto log3 = std::move(log).object_ref();
	assert(log3.has_value());

	assert(log.is<void>());        // NOLINT(bugprone-use-after-move)

	auto log_module = std::move(*log3).downcast<deai::builtin::log::Log>().value();
	auto file_target = log_module->file_target("/tmp/file", false);

	di->chdir("/tmp");

	std::array<char, PATH_MAX> path;
	assert(strcmp(getcwd(path.data(), path.size()), "/tmp") == 0);
	auto object = deai::Object::create();
	Ref<ListenHandle> lh = ([&]() {
		// Drop closure after `.on`, to make sure it's indeed kept alive
		auto closure = util::to_di_closure<test_function>();
		assert(closure.call<int>(10) == 10);

		return object.on("test_signal", closure);
	})();
	object.emit("test_signal", 20);        // NOLINT
	assert(result == 20);
	return 0;
}
