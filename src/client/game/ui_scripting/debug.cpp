#include <std_include.hpp>
#include "debug.hpp"
#include "component/console/console.hpp"

#include <utils/finally.hpp>
#include <utils/hook.hpp>

namespace ui_scripting::debug
{
	namespace
	{
		struct function_prototype : game::hks::ChunkHeader
		{
			uint16_t upvalue_count;
			uint16_t register_count;
			uint8_t parameter_count;
			uint8_t flags;
			char padding0[2];
			uint32_t instruction_count;
			char padding1[4];
			const game::hks::hksInstruction* instructions;
			uint32_t constant_count;
			char padding2[4];
			game::hks::HksObject* constants;
			uint32_t child_count;
			char padding3[4];
			function_prototype** children;
			uint32_t source_id;
			uint32_t function_id;
		};

		static_assert(sizeof(function_prototype) == 0x50);
		static_assert(offsetof(function_prototype, instruction_count) == 0x18);
		static_assert(offsetof(function_prototype, instructions) == 0x20);
		static_assert(offsetof(function_prototype, source_id) == 0x48);

		struct closure_header : game::hks::ChunkHeader
		{
			function_prototype* prototype;
		};

		static_assert(offsetof(closure_header, prototype) == 0x10);

		struct line_block
		{
			int storage[256];
			const int* data;
			line_block* next;
			line_block* previous;
		};

		static_assert(sizeof(line_block) == 0x418);
		static_assert(offsetof(line_block, data) == 0x400);
		static_assert(offsetof(line_block, next) == 0x408);

		struct line_buffer
		{
			game::hks::lua_State* state;
			line_block storage;
			line_block* first;
			line_block* current;
			int* last;
			int* next;
			uint32_t count;
			uint32_t capacity;
		};

		static_assert(sizeof(line_buffer) == 0x448);
		static_assert(offsetof(line_buffer, first) == 0x420);
		static_assert(offsetof(line_buffer, count) == 0x440);

		// Only the compiler fields used here are described.
		struct compiler_function
		{
			game::hks::lua_State* state;
			function_prototype* prototype;
			char padding[0x7F0 - 0x10];
			line_buffer lines;
			game::hks::InternString* name;
		};

		static_assert(offsetof(compiler_function, prototype) == 0x8);
		static_assert(offsetof(compiler_function, lines) == 0x7F0);
		static_assert(offsetof(compiler_function, name) == 0xC38);

		struct compiler_context
		{
			char padding0[0x10];
			game::hks::lua_State* state;
			char padding1[0x7DD0 - 0x18];
			compiler_function* current_function;
		};

		static_assert(offsetof(compiler_context, state) == 0x10);
		static_assert(offsetof(compiler_context, current_function) == 0x7DD0);

		struct source_info
		{
			std::string source;
			std::string name;
			std::vector<int> lines;
		};

		struct compilation
		{
			game::hks::lua_State* state;
			const std::string& source;
		};

		std::mutex source_mutex;
		std::unordered_map<const void*, source_info> sources;
		thread_local const compilation* current_compilation{};

		utils::hook::detour finish_function_hook;
		utils::hook::detour get_function_info_hook;
		utils::hook::detour allocator_hook;

		uint64_t finish_function_stub(compiler_context* compiler)
		{
			const auto result = finish_function_hook.invoke<uint64_t>(compiler);

			if (!current_compilation || compiler->state != current_compilation->state)
			{
				return result;
			}

			try
			{
				const auto function = compiler->current_function;

				if (function->lines.count != function->prototype->instruction_count)
				{
					throw std::runtime_error("Lua instruction and source-line counts differ");
				}

				source_info info{ current_compilation->source,
					function->name ? function->name->m_data : "(anonymous)", {} };
				info.lines.reserve(function->lines.count);

				for (auto block = function->lines.first; info.lines.size() < function->lines.count; block = block->next)
				{
					const auto count = std::min<size_t>(256, function->lines.count - info.lines.size());
					info.lines.insert(info.lines.end(), block->data, block->data + count);
				}

				const std::lock_guard lock(source_mutex);
				sources.insert_or_assign(function->prototype, std::move(info));
			}
			catch (const std::exception& e)
			{
				console::warn("Failed to retain Lua source information: %s\n", e.what());
			}

			return result;
		}

		std::optional<size_t> get_current_instruction(game::hks::lua_State* state,
			const function_prototype* prototype)
		{
			CONTEXT context{};
			RtlCaptureContext(&context);

			const auto tib = reinterpret_cast<const NT_TIB*>(NtCurrentTeb());
			const auto stack_limit = reinterpret_cast<uintptr_t>(tib->StackLimit);
			const auto stack_base = reinterpret_cast<uintptr_t>(tib->StackBase);

			const auto on_stack = [&](const uintptr_t address)
			{
				return address >= stack_limit && address <= stack_base - sizeof(uintptr_t);
			};

			const auto shared = game::select(0xB0F000, 0x840000);
			const auto secure = game::select(0xB12890, 0x843890);
			const auto unshared = game::select(0xB16490, 0x847490);
			const auto end = game::select(0xB1A670, 0x84B670);

			// Recover the interpreter's saved PC only when its active Lua frame is inspected.
			for (auto depth = 0; depth < 128 && context.Rip && on_stack(context.Rsp); ++depth)
			{
				if (context.Rip >= shared && context.Rip < end)
				{
					const auto state_slot = context.Rbp + (context.Rip < unshared ? 0x3F0 : 0x4F0);
					const auto pc_slot = state_slot + sizeof(uintptr_t);

					if (on_stack(state_slot) && on_stack(pc_slot) &&
						*reinterpret_cast<game::hks::lua_State**>(state_slot) == state)
					{
						const auto next_pc = context.Rip >= secure && context.Rip < unshared
							? context.Rdi : *reinterpret_cast<const uintptr_t*>(pc_slot);
						const auto instructions = reinterpret_cast<uintptr_t>(prototype->instructions);
						const auto offset = next_pc - instructions;

						if (next_pc > instructions && offset % sizeof(game::hks::hksInstruction) == 0 &&
							offset / sizeof(game::hks::hksInstruction) <= prototype->instruction_count)
						{
							return offset / sizeof(game::hks::hksInstruction) - 1;
						}

						return {};
					}
				}

				const auto previous_rsp = context.Rsp;
				DWORD64 image_base{};

				if (const auto entry = RtlLookupFunctionEntry(context.Rip, &image_base, nullptr))
				{
					void* handler_data{};
					DWORD64 frame{};
					RtlVirtualUnwind(UNW_FLAG_NHANDLER, image_base, context.Rip, entry,
						&context, &handler_data, &frame, nullptr);
				}
				else
				{
					context.Rip = *reinterpret_cast<const uintptr_t*>(context.Rsp);
					context.Rsp += sizeof(uintptr_t);
				}

				if (context.Rsp <= previous_rsp)
				{
					break;
				}
			}

			return {};
		}

		int get_function_info_stub(game::hks::lua_State* state, closure_header* closure,
			game::hks::lua_Debug* info, const char* what)
		{
			const auto result = get_function_info_hook.invoke<int>(state, closure, info, what);

			if (!result)
			{
				return result;
			}

			const std::lock_guard lock(source_mutex);
			const auto entry = sources.find(closure->prototype);

			if (entry == sources.end())
			{
				return result;
			}

			const auto& source = entry->second;
			const auto main_chunk = source.name == "(main chunk)";

			if (std::strchr(what, 'S'))
			{
				info->source = source.source.c_str();
				strncpy_s(info->short_src, source.source.c_str(), _TRUNCATE);
				info->what = main_chunk ? "main" : "Lua";
			}

			if (std::strchr(what, 'n'))
			{
				info->name = main_chunk ? nullptr : source.name.c_str();
				info->namewhat = "";
			}

			if (std::strchr(what, 'l') && info->callstack_level >= 0)
			{
				// The stock debug API returns a bytecode offset for stripped functions.
				auto instruction = static_cast<size_t>(info->currentline);
				const auto& stack = state->m_callStack;

				if (!info->is_tail_call && info->callstack_level == stack.m_current - stack.m_records &&
					info->callstack_level != stack.m_hook_level)
				{
					if (const auto current = get_current_instruction(state, closure->prototype))
					{
						instruction = *current;
					}
				}

				info->currentline = !info->is_tail_call && instruction < source.lines.size()
					? source.lines[instruction] : -1;
			}

			return result;
		}

		void* allocator_stub(void* user_data, void* pointer, const size_t old_size, const size_t new_size)
		{
			if (pointer && !new_size && old_size == sizeof(function_prototype))
			{
				const std::lock_guard lock(source_mutex);
				sources.erase(pointer);
			}

			return allocator_hook.invoke<void*>(user_data, pointer, old_size, new_size);
		}
	}

	int load_buffer(game::hks::lua_State* state, const game::hks::HksCompilerSettings* settings,
		const std::string& name, const std::string& data)
	{
		const compilation context{ state, name };
		const auto previous = std::exchange(current_compilation, &context);

		const auto restore = utils::finally([&]()
		{
			current_compilation = previous;
		});

		return game::hks::hksi_hksL_loadbuffer(state, settings, data.data(),
			static_cast<unsigned int>(data.size()), name.c_str());
	}

	void clear()
	{
		const std::lock_guard lock(source_mutex);
		sources.clear();
	}

	void initialize()
	{
		finish_function_hook.create(game::select(0x2CB1E0, 0x13F5A0), finish_function_stub);
		get_function_info_hook.create(game::select(0x2E3CE0, 0x1581D0), get_function_info_stub);
		allocator_hook.create(game::select(0x2D7A60, 0x14BE20), allocator_stub);
	}
}
