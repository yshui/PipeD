#include <cassert>
#include <deai/c++/builtins.hh>
#include <deai/c++/deai.hh>
#include <iostream>
#include <string_view>

#include <unistd.h>
#include <climits>

using namespace deai::type;
using namespace deai::util;
using namespace std::literals;

DEAI_CPP_PLUGIN_ENTRY_POINT(di) {
	di.raw_members()["lua"].erase();

	util::call_deai_method<void>(di, "load_plugin", "./plugins/lua/di_lua.so"sv);

	auto luam = di["lua"]->object_ref().value();

	auto err = util::call_deai_method<Ref<Object>>(luam, "load_script",
	                                               "../tests/invalid.lua"sv);
	assert(err["errmsg"].has_value());
	std::cout << "Error message is: " << err["errmsg"]->to<std::string>().value() << "\n";

	return 0;
}
