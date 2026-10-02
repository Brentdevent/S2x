freetype = {
	source = path.join(dependencies.basePath, "freetype"),
}

function freetype.import()
	-- Only the launcher uses FreeType, through RmlUi.
end

function freetype.includes()
	includedirs { path.join(freetype.source, "include") }
end

function freetype.project()
	project "freetype"
		kind "StaticLib"
		language "C"
		warnings "Off"
		freetype.includes()
		defines { "FT2_BUILD_LIBRARY", "_CRT_SECURE_NO_WARNINGS" }

		-- The amalgamated modules used by FreeType's upstream Windows build.
		local modules = {
			"autofit/autofit", "base/ftbase", "base/ftbbox", "base/ftbdf",
			"base/ftbitmap", "base/ftcid", "base/ftdebug", "base/ftfstype",
			"base/ftgasp", "base/ftglyph", "base/ftgxval", "base/ftinit",
			"base/ftmm", "base/ftotval", "base/ftpatent", "base/ftpfr",
			"base/ftstroke", "base/ftsynth", "base/ftsystem", "base/fttype1",
			"base/ftwinfnt", "bdf/bdf", "cache/ftcache", "cff/cff",
			"cid/type1cid", "gzip/ftgzip", "lzw/ftlzw", "pcf/pcf",
			"pfr/pfr", "psaux/psaux", "pshinter/pshinter", "psnames/psmodule",
			"raster/raster", "sdf/sdf", "sfnt/sfnt", "smooth/smooth",
			"svg/svg", "truetype/truetype", "type1/type1", "type42/type42",
			"winfonts/winfnt",
		}
		for _, module in ipairs(modules) do
			files { path.join(freetype.source, "src", module .. ".c") }
		end
end

table.insert(dependencies, freetype)
