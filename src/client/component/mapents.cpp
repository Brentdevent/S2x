#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "game/game.hpp"

#include "console/console.hpp"
#include "filesystem.hpp"
#include "gsc/script_loading.hpp"

#include <utils/hook.hpp>
#include <utils/string.hpp>

namespace mapents
{
	namespace
	{
		std::optional<std::string> parse_mapents(const std::string& source)
		{
			static const std::regex expr(R"~((.+) "(.*)")~");

			std::string out_buffer{};
			out_buffer.reserve(source.size());

			const auto lines = utils::string::split(source, '\n');
			auto in_map_ent = false;
			auto in_comment = false;

			for (std::size_t i = 0; i < lines.size(); ++i)
			{
				const auto line_num = static_cast<int>(i + 1);
				auto line = lines[i];
				if (line.ends_with('\r'))
				{
					line.pop_back();
				}

				if (line.starts_with("/*"))
				{
					in_comment = true;
					continue;
				}

				if (line.ends_with("*/"))
				{
					in_comment = false;
					continue;
				}

				if (in_comment || line.empty() || line.starts_with("//"))
				{
					continue;
				}

				if (line[0] == '{' || line[0] == '}')
				{
					const auto opening = line[0] == '{';
					if (opening == in_map_ent)
					{
						console::error("[map_ents parser] Unexpected '%c' on line %i\n", line[0], line_num);
						return {};
					}

					in_map_ent = opening;
					out_buffer += line[0];

					// The final closing brace is not followed by a newline
					if (opening || i + 1 < lines.size())
					{
						out_buffer += '\n';
					}

					continue;
				}

				std::smatch match{};
				if (!std::regex_search(line, match, expr))
				{
					console::warn("[map_ents parser] Failed to parse line %i (%s)\n", line_num, line.data());
					continue;
				}

				const auto key = utils::string::to_lower(match[1].str());
				const auto value = match[2].str();

				if (key.empty())
				{
					console::warn("[map_ents parser] Invalid key ('%s') on line %i (%s)\n", key.data(), line_num, line.data());
					continue;
				}

				// Numeric and unquoted keys are passed through unchanged, including empty values
				if (utils::string::is_numeric(key) || key.size() < 3 || !key.starts_with('"') || !key.ends_with('"'))
				{
					out_buffer.append(line);
					out_buffer.append("\n");
					continue;
				}

				if (value.empty())
				{
					continue;
				}

				const auto key_ = key.substr(1, key.size() - 2);
				const auto id = gsc::gsc_ctx->token_id(key_);
				if (!id)
				{
					console::warn("[map_ents parser] Key '%s' not found, on line %i (%s)\n", key_.data(), line_num, line.data());
					continue;
				}

				out_buffer.append(std::format("{} \"{}\"\n", id, value));
			}

			return {std::move(out_buffer)};
		}

		std::string raw_ents;
		bool load_raw_mapents()
		{
			raw_ents.clear();

			const auto* map_ents = *game::cm_mapEnts;
			if (!map_ents || !map_ents->name)
			{
				return false;
			}

			// e.g. maps/mp/mp_shipment_s2.d3dbsp.ents
			const auto mapents_name = std::format("{}.ents", map_ents->name);
			if (!filesystem::read_file(mapents_name, &raw_ents) || raw_ents.empty())
			{
				return false;
			}

			console::debug("Reading raw ents file \"%s\"\n", mapents_name.data());
			return true;
		}

		// G_ResetEntityParsePoint keeps a pointer into these until it is called again,
		// so they are only replaced from inside the stub.
		std::string entity_string;

		// h1-mod clears its cached map entities in CM_Unload. Track which entity string the
		// cache was parsed from instead, so a newly loaded map never reuses the previous one.
		std::string entity_source;

		// Stock entity strings use numeric keys. Only zones built by custom tools carry
		// quoted key names that have to be converted to their token ids.
		bool has_quoted_keys(const char* ents)
		{
			for (auto* line = ents; line && *line;)
			{
				if (*line == '"')
				{
					return true;
				}

				line = std::strchr(line, '\n');
				if (line)
				{
					++line;
				}
			}

			return false;
		}

		const char* cm_entity_string_stub()
		{
			const char* ents = nullptr;
			if (load_raw_mapents())
			{
				entity_source.clear();
				ents = raw_ents.data();
			}
			else
			{
				ents = game::CM_EntityString();
				if (!ents)
				{
					return ents;
				}

				if (!entity_string.empty() && entity_source == ents)
				{
					return entity_string.data();
				}

				if (!has_quoted_keys(ents))
				{
					entity_source.clear();
					entity_string.clear();
					return ents;
				}

				entity_source = ents;
			}

			if (auto parsed = parse_mapents(ents))
			{
				entity_string = std::move(*parsed);
				return entity_string.data();
			}

			entity_string.clear();
			return ents;
		}
	}

	class component final : public generic_component
	{
	public:
		void post_unpack() override
		{
			// G_ResetEntityParsePoint -> CM_EntityString
			utils::hook::call(game::select(0x55E634, 0x372A54), cm_entity_string_stub);
		}
	};
}

REGISTER_COMPONENT(mapents::component)
