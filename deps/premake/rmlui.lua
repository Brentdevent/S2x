rmlui = {
	source = path.join(dependencies.basePath, "rmlui"),
}

function rmlui.includes()
	includedirs {
		path.join(rmlui.source, "Include"),
		path.join(rmlui.source, "Backends"),
	}
	-- Keep the renderer and launcher capability checks in sync. Mesa's software
	-- renderer can support 4x MSAA while rejecting the upstream 2x default.
	defines { "RMLUI_STATIC_LIB", "NUM_MSAA_SAMPLES=4" }
end

function rmlui.import()
	-- Keep the UI dependencies out of the common library and TLS helper.
end

function rmlui.link()
	rmlui.includes()
	links { "rmlui", "freetype", "d3d11", "dxgi", "d3dcompiler", "windowscodecs", "shlwapi", "imm32" }
end

function rmlui.project()
	project "rmlui"
		kind "StaticLib"
		language "C++"
		warnings "Off"
		rmlui.includes()
		freetype.includes()
		defines { 'RMLUI_VERSION="6.3"', "RMLUI_FONT_ENGINE_FREETYPE", "NOMINMAX" }
		files {
			path.join(rmlui.source, "Include/**.h"),
			path.join(rmlui.source, "Source/Core/**.cpp"),
			path.join(rmlui.source, "Backends/RmlUi_Platform_Win32.cpp"),
			path.join(rmlui.source, "Backends/RmlUi_Renderer_DX11.cpp"),
		}
		removefiles {
			path.join(rmlui.source, "Source/Core/precompiled.cpp"),
			path.join(rmlui.source, "Source/Core/Elements/ElementLottie.cpp"),
			path.join(rmlui.source, "Source/Core/Elements/ElementSVG.cpp"),
		}
end

table.insert(dependencies, rmlui)
