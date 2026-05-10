asmjit = {
	source = path.join(dependencies.basePath, "asmjit"),
}

function asmjit.import()
	links {
		"asmjit"
	}

	asmjit.includes()
end

function asmjit.includes()
	includedirs {
		asmjit.source,
		path.join(asmjit.source, "asmjit"),
	}

	defines {
		"ASMJIT_STATIC",
		"ASMJIT_NO_FOREIGN",
		"ASMJIT_NO_AARCH64",
	}
end

function asmjit.project()
	project "asmjit"
		language "C++"
		cppdialect "C++17"
		kind "StaticLib"

		asmjit.includes()

		files {
			path.join(asmjit.source, "asmjit/**.cpp"),
			path.join(asmjit.source, "asmjit/**.h"),
		}
		
		removefiles {
			path.join(asmjit.source, "asmjit-testing/**"),
			path.join(asmjit.source, "tools/**"),
			path.join(asmjit.source, "db/**"),
		}

		warnings "Off"
end

table.insert(dependencies, asmjit)
