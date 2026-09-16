#include "../src/client/component/dedicated_settings_config.hpp"
#include <iostream>
#include <stdexcept>

namespace
{
	void require(const bool condition, const char* message)
	{
		if (!condition) throw std::runtime_error(message);
	}
}

int main()
{
	try
	{
		using dedicated_settings::detail::admin_config_files;
		admin_config_files files;
		require(files.insert("server.cfg"), "startup config must be tracked");
		require(files.insert("nested\\limits"), "nested admin config must be tracked");
		require(files.contains("NESTED/limits.cfg"), "exec names must normalize consistently");
		require(!files.insert("SERVER.CFG"), "repeated admin exec must not duplicate registration");

		// server.cfg: exec default_xboxlive.cfg; set scr_dom_scorelimit 250
		// Registration of the child must not turn the lifecycle's later read
		// of the same file into an admin write that replaces the saved 250.
		require(!files.insert("default_xboxlive.cfg"), "nested defaults must not enter the admin registry");
		require(!files.contains("default_xboxlive.cfg"), "rotation defaults must run without ledger actions");
		require(!files.insert("DEFAULT_XBOXLIVE"), "extensionless defaults must also be excluded");
		require(!files.contains("DEFAULT_XBOXLIVE.CFG"), "case variants must also be excluded");
		require(files.contains("server.cfg"), "default exclusion must preserve parent tracking");
		require(files.contains("nested/limits.cfg"), "default exclusion must preserve child tracking");
		require(files.size() == 2, "only admin configs should be counted");
		require(!files.insert(""), "empty config names must not be registered");
		std::cout << "dedicated settings config tests passed\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
