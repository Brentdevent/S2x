#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "scheduler.hpp"
#include "game/game.hpp"

#include <utils/hook.hpp>

#include <array>
#include <cmath>
#include <mutex>

namespace lagometer
{
	namespace
	{
		constexpr size_t history_size = 128;
		constexpr size_t graph_columns = 48;
		
		const game::dvar_t* cg_draw_lagometer{};

		template <typename T>
		struct sample_history
		{
			std::array<T, history_size> samples{};
			size_t next{};
			size_t count{};

			void add(const T& sample)
			{
				samples[next] = sample;
				next = (next + 1) % history_size;
				count = std::min(count + 1, history_size);
			}

			const T& recent(const size_t age) const
			{
				return samples[(next + history_size - 1 - age) % history_size];
			}
		};

		struct snapshot_sample
		{
			int ping{};
			int flags{};
			bool dropped{};
		};

		struct lagometer_history
		{
			sample_history<int64_t> frames;
			sample_history<snapshot_sample> snapshots;
			
			std::optional<int> last_frame_time;

			void add_frame(const int time, const int latest_snapshot_time)
			{
				if (last_frame_time && time < *last_frame_time)
				{
					*this = {};
				}
				
				last_frame_time = time;
				frames.add(static_cast<int64_t>(time) - latest_snapshot_time);
			}

			void add_snapshots(const int before, const int after, const std::optional<snapshot_sample>& snapshot)
			{
				if (after < before)
				{
					*this = {};
					return;
				}

				const auto advanced = static_cast<int64_t>(after) - before;
				if (!advanced)
				{
					return;
				}

				// Do not count snapshot numbers preceding our first snapshot as packet loss.
				if (snapshots.count)
				{
					const auto dropped = std::min<int64_t>(advanced - (snapshot ? 1 : 0), history_size);
					for (int64_t i = 0; i < dropped; ++i)
					{
						snapshots.add({0, 0, true});
					}
				}

				if (snapshot)
				{
					snapshots.add(*snapshot);
				}
			}
		};

		std::mutex history_mutex;
		lagometer_history history;

		template <typename T>
		T read_field(const std::byte* base, const size_t offset)
		{
			T value;
			std::memcpy(&value, base + offset, sizeof(value));
			return value;
		}

		bool in_game(const int local_client_num)
		{
			return local_client_num == game::LOCAL_CLIENT_0 && !game::virtual_lobby_loaded() &&
				game::CL_IsLocalClientInGame(local_client_num);
		}

		void process_snapshots(const int local_client_num)
		{
			if (in_game(local_client_num))
			{
				const auto* cg = game::CG_GetLocalClient.call_safe(local_client_num);
				if (cg && read_field<const void*>(cg, 0x59A8))
				{
					// CG_DrawActiveFrame writes cg->time at 0x69006. CG_ProcessSnapshots
					// obtains cg->latestSnapshotTime at 0x43ADE4. Sample before processing,
					// as CoD4 does: negative = interpolation, positive = extrapolation.
					const auto time = read_field<int>(cg, 0x1E6B7C);
					const auto latest_snapshot_time = read_field<int>(cg, 0x5998);
					std::lock_guard lock(history_mutex);
					history.add_frame(time, latest_snapshot_time);
				}
			}

			game::CG_ProcessSnapshots(local_client_num);
		}

		const std::byte* read_next_snapshot(const int local_client_num)
		{
			if (!in_game(local_client_num))
			{
				return game::CG_ReadNextSnapshot(local_client_num);
			}
			
			const auto* cgs = game::CG_GetLocalClientStatic.call_safe(local_client_num);
			if (!cgs)
			{
				return game::CG_ReadNextSnapshot(local_client_num);
			}

			// S2 prefetches snapshots in CG_ProcessSnapshots, then can skip CL_GetSnapshot
			// when consuming the cached snapshot. Count consumption, including skipped
			// sequence numbers, using cgs->processedSnapshotNum (+0x1C).
			const auto before = read_field<int>(cgs, 0x1C);
			const auto* snapshot = game::CG_ReadNextSnapshot(local_client_num);
			const auto after = read_field<int>(cgs, 0x1C);
			
			std::optional<snapshot_sample> sample;
			
			if (snapshot)
			{
				// CL_GetSnapshot (0x7B0E0) copies snapFlags/ping to these S2 snapshot fields.
				sample = {std::max(0, read_field<int>(snapshot, 0x6D5C)), read_field<int>(snapshot, 0x6D58), false};
			}
			
			std::lock_guard lock(history_mutex);
			history.add_snapshots(before, after, sample);
			return snapshot;
		}

		void draw_lagometer()
		{
			if (!in_game(game::LOCAL_CLIENT_0))
			{
				std::lock_guard lock(history_mutex);
				history = {};
				return;
			}

			if (!cg_draw_lagometer || !cg_draw_lagometer->current.enabled)
			{
				return;
			}

			lagometer_history display;
			{
				std::lock_guard lock(history_mutex);
				display = history;
			}

			const auto* placement = game::ScrPlace_GetViewPlacement(game::LOCAL_CLIENT_0);
			auto* white = game::Material_RegisterHandle("white");

			if (!placement || !white)
			{
				return;
			}

			const auto sx = placement->scaleVirtualToReal[0];
			const auto sy = placement->scaleVirtualToReal[1];

			if (sx <= 0.0f || sy <= 0.0f)
			{
				return;
			}

			const auto x = placement->realViewportPosition[0] + placement->realViewportSize[0] - 54.0f * sx;
			// Keep the graph above S2's killstreak HUD.
			const auto y = placement->realViewportPosition[1] + placement->realViewportSize[1] - 270.0f * sy;
			const auto draw = [&](const float left, const float top, const float width, const float height,
				const float* color)
			{
				game::R_AddCmdDrawStretchPic(x + left * sx, y + top * sy, width * sx, height * sy,
					0.0f, 0.0f, 0.0f, 0.0f, color, white);
			};

			constexpr float background[4] = {0.0f, 0.0f, 0.0f, 0.5f};
			constexpr float axis[4] = {1.0f, 1.0f, 1.0f, 0.25f};
			constexpr float yellow[4] = {1.0f, 1.0f, 0.0f, 1.0f};
			constexpr float blue[4] = {0.0f, 0.4f, 1.0f, 1.0f};
			constexpr float green[4] = {0.0f, 1.0f, 0.0f, 1.0f};
			constexpr float red[4] = {1.0f, 0.0f, 0.0f, 1.0f};
			
			draw(0.0f, 0.0f, 48.0f, 48.0f, background);
			draw(0.0f, 16.0f, 48.0f, 1.0f / sy, axis);

			// Classic CoD4 scales: +/-300 ms for frame timing, 900 ms for snapshot ping.
			for (size_t i = 0; i < std::min(display.frames.count, graph_columns); ++i)
			{
				const auto value = static_cast<float>(display.frames.recent(i));
				const auto height = std::min(std::abs(value) * (16.0f / 300.0f), 16.0f);
				if (value != 0.0f)
				{
					draw(47.0f - static_cast<float>(i), value > 0.0f ? 16.0f - height : 16.0f,
						1.0f, height, value > 0.0f ? yellow : blue);
				}
			}
			
			for (size_t i = 0; i < std::min(display.snapshots.count, graph_columns); ++i)
			{
				const auto& sample = display.snapshots.recent(i);
				// SV_WriteSnapshotToClient (0x6F3B40) sets bit 0 when client->rateDelayed is nonzero.
				const auto* color = sample.dropped ? red : ((sample.flags & 1) ? yellow : green);
				const auto height = sample.dropped ? 24.0f :
					std::min(std::max(static_cast<float>(sample.ping) * (24.0f / 900.0f), 1.0f / sy), 24.0f);
				draw(47.0f - static_cast<float>(i), 48.0f - height, 1.0f, height, color);
			}
		}
	}

	class component final : public multiplayer_component
	{
	public:
		void post_unpack() override
		{
			if (game::environment::is_dedicated())
			{
				return;
			}

			cg_draw_lagometer = game::Dvar_RegisterBool("cg_drawLagometer", false, game::DVAR_FLAG_SAVED);
			
			utils::hook::call(0x6910B_g, process_snapshots);
			utils::hook::call(0x43AE54_g, read_next_snapshot);
			utils::hook::call(0x43AF21_g, read_next_snapshot);

			scheduler::loop(draw_lagometer, scheduler::renderer);
		}
	};
}

REGISTER_COMPONENT(lagometer::component)
