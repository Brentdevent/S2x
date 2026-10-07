#pragma once

#include <functional>
#include <memory>
#include <string>

namespace Rml
{
	class ElementDocument;
	class Event;
}

// Owns the native window, RmlUi context and renderer for the launcher's lifetime.
class rml_window final
{
public:
	rml_window(const std::string& title, int width, int height);
	~rml_window();
	rml_window(const rml_window&) = delete;
	rml_window& operator=(const rml_window&) = delete;

	Rml::ElementDocument* load_document(const std::string& name);
	void set_event_handler(std::function<void(Rml::Event&)> handler);
	void run() const;
	void close();

private:
	class implementation;
	std::unique_ptr<implementation> impl_;
};
