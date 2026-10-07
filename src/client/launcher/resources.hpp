#pragma once

#include <RmlUi/Core/FileInterface.h>
#include <string_view>

namespace launcher_resources
{
	// All documents, styles, fonts and artwork are embedded; no current-directory dependency.
	std::string_view load(const std::string& name);

	class file_interface final : public Rml::FileInterface
	{
	public:
		Rml::FileHandle Open(const Rml::String& path) override;
		void Close(Rml::FileHandle file) override;
		size_t Read(void* buffer, size_t size, Rml::FileHandle file) override;
		bool Seek(Rml::FileHandle file, long offset, int origin) override;
		size_t Tell(Rml::FileHandle file) override;
		size_t Length(Rml::FileHandle file) override;
	};
}
