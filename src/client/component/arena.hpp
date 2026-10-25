#pragma once

namespace arena
{
	// Maps registered while a scope is active are removed from the arena catalog
	// again when the scope ends, unless the operation that needed them calls keep().
	// Scopes are only used from the main thread.
	class map_registration_scope final
	{
	public:
		map_registration_scope();
		~map_registration_scope();

		map_registration_scope(const map_registration_scope&) = delete;
		map_registration_scope& operator=(const map_registration_scope&) = delete;

		void keep();

	private:
		friend bool register_map(const std::string& map_name);

		map_registration_scope* previous_;
		std::vector<std::string> maps_;
		bool keep_ = false;
	};

	bool register_map(const std::string& map_name);
}
