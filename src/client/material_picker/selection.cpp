#include <std_include.hpp>

#include "selection.hpp"
#include "game/game.hpp"
#include "images/bindings.hpp"

namespace material_picker
{
	namespace
	{
		void collect_images(const game::XAssetHeader header, void* data) noexcept
		{
			auto& picked = *static_cast<picked_material*>(data);
			if (picked.error)
			{
				return;
			}

			const auto* material = header.material;
			if (!material || !material->name)
			{
				return;
			}

			const std::string_view name(material->name);

			// S2's collision names can omit the render usage prefix: for example,
			// mp_lon_dock_wood_02 -> wc/mp_lon_dock_wood_02 (verified live).
			const auto slash = name.find('/');
			if (name != picked.name && (slash == name.npos || name.substr(slash + 1) != picked.name))
			{
				return;
			}

			try
			{
				picked.matches.push_back(copy_material(*material));
			}
			catch (...)
			{
				picked.error = std::current_exception();
			}
		}
	}

	render_material copy_material(const game::Material& material)
	{
		render_material match;
		match.name = material.name;
		if (material.technique_set && material.technique_set->name)
		{
			match.techset = material.technique_set->name;
		}

		if (!material.image_table)
		{
			return match;
		}

		for (unsigned int i = 0; i < material.image_count; ++i)
		{
			const auto& binding = material.image_table[i];
			if (!binding.image || !binding.image->name)
			{
				continue;
			}

			match.images.push_back(images::bindings::format(i, binding.semantic_hash, binding.image->name));
		}

		return match;
	}

	void resolve_material(picked_material& picked)
	{
		game::DB_EnumXAssets_FastFile(game::ASSET_TYPE_MATERIAL, collect_images, &picked, false);
		if (picked.error)
		{
			std::rethrow_exception(picked.error);
		}

		// A complete exact asset name takes precedence over usage variants.
		if (std::any_of(picked.matches.begin(), picked.matches.end(),
				[&](const auto& match)
				{
					return match.name == picked.name;
				}))
		{
			std::erase_if(picked.matches,
				[&](const auto& match)
				{
					return match.name != picked.name;
				});
		}
	}
}
