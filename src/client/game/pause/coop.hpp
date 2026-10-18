#pragma once

#include <array>
#include <charconv>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace pause::coop::protocol
{
	constexpr std::size_t capacity = 4;

	constexpr std::uint64_t version = 1;
	constexpr std::uint64_t heartbeat_ms = 250;
	constexpr std::uint64_t presence_ms = 5000;
	constexpr std::uint64_t prepare_ms = 10000;

	// '$' has no stock CG_DeployServerCommandString case. Old clients ignore
	// the capability probe instead of interpreting it as a native 's' command.
	// Keep the wire names compatible with earlier co-op test builds.
	constexpr std::string_view bootstrap_command = "$s2x_pause_hard";
	constexpr auto state_command = "s2x_hp_state";
	constexpr auto ack_command = "s2x_hp_ack";
	constexpr auto recover_command = "s2x_hp_recover";
	constexpr auto leave_command = "s2x_hp_leave";

	enum class phase : std::uint64_t
	{
		idle,
		preparing,
		paused,
		resuming,
		released,
		blocked,
		cancelled,
		arming,
		recovering,
		removed,
	};

	inline bool is_frozen(const phase state)
	{
		return state == phase::arming || state == phase::paused || state == phase::resuming ||
			state == phase::recovering || state == phase::blocked;
	}

	struct message
	{
		std::uint64_t session{};
		std::uint64_t generation{};
		std::uint64_t token{};
		std::uint64_t probe{};
		phase state{};
		std::uint64_t value{};
		std::uint64_t match{};
		std::uint64_t round{};
		std::uint64_t wake{};
	};

	// One token in an engine reliable command, or an exact OOB payload. No
	// whitespace/NUL tolerance: the framing belongs to the transport, not us.
	inline std::string encode(const message& msg)
	{
		const std::array<std::uint64_t, 10> fields{version, msg.session, msg.generation, msg.token, msg.probe,
			static_cast<std::uint64_t>(msg.state), msg.value, msg.match, msg.round, msg.wake};
		std::string result;
		for (const auto field : fields)
		{
			if (!result.empty())
			{
				result += ':';
			}

			std::array<char, 16> text{};
			const auto converted = std::to_chars(text.data(), text.data() + text.size(), field, 16);
			result.append(text.data(), converted.ptr);
		}

		return result;
	}

	inline std::optional<message> decode(std::string_view text)
	{
		if (text.empty() || text.size() > 169)
		{
			return {};
		}

		std::array<std::uint64_t, 10> fields{};
		for (std::size_t i = 0; i < fields.size(); ++i)
		{
			const auto end = text.find(':');
			if ((end == text.npos) != (i + 1 == fields.size()))
			{
				return {};
			}

			const auto field = text.substr(0, end);
			if (field.empty() || field.size() > 16)
			{
				return {};
			}

			const auto converted = std::from_chars(field.data(), field.data() + field.size(), fields[i], 16);
			if (converted.ec != std::errc{} || converted.ptr != field.data() + field.size())
			{
				return {};
			}

			if (end != text.npos)
			{
				text.remove_prefix(end + 1);
			}
		}

		if (fields[0] != version || !fields[1] || !fields[2] || !fields[3] || fields[5] < 1 ||
			fields[5] > static_cast<std::uint64_t>(phase::removed) || !fields[8])
		{
			return {};
		}

		return message{fields[1], fields[2], fields[3], fields[4], static_cast<phase>(fields[5]), fields[6],
			fields[7], fields[8], fields[9]};
	}

	struct member
	{
		std::uint64_t token{};
		std::uint64_t wake{};
		std::uint64_t last_probe{};
		std::uint64_t heard_at{};
		bool acknowledged{};
		bool ready{};
	};

	// Engine-independent quorum logic. The adapter validates the remaining
	// engine identities and each packet's source endpoint before passing it here.
	class host
	{
	public:
		phase state{phase::idle};
		std::uint64_t session{};
		std::uint64_t generation{};
		std::uint64_t round{};
		std::uint64_t started_at{};
		std::uint64_t match{};
		std::uint8_t roster{};
		std::uint8_t local{};
		std::array<member, capacity> members{};

		bool start(const std::uint64_t session_id, const std::uint64_t generation_id, const std::uint8_t mask,
			const std::uint8_t local_mask, const std::array<std::uint64_t, capacity>& tokens, const std::uint64_t now)
		{
			if (state != phase::idle || !session_id || !generation_id || mask > 15 || !local_mask ||
				(local_mask & (local_mask - 1)) || (mask & local_mask) != local_mask || mask == local_mask)
			{
				return false;
			}

			for (std::size_t i = 0; i < capacity; ++i)
			{
				if ((mask & (1u << i)) && !tokens[i])
				{
					return false;
				}
			}

			*this = {};
			session = session_id;
			generation = generation_id;
			roster = mask;
			local = local_mask;
			started_at = now;
			state = phase::preparing;
			round = 1;
			for (std::size_t i = 0; i < capacity; ++i)
			{
				members[i].token = tokens[i];
				members[i].heard_at = now;
			}

			return true;
		}

		void issue_probe(const std::uint64_t now)
		{
			++probe_;
			probes_[probe_ % probes_.size()] = {probe_, now, state};
		}

		message packet(const std::size_t slot) const
		{
			return {session, generation, members[slot].token, probe_, state,
				static_cast<std::uint64_t>(roster << 4) | ready_mask(), match, round, members[slot].wake};
		}

		bool is_guest(const std::size_t slot) const
		{
			return slot < capacity && (roster & ~local & (1u << slot));
		}

		bool acknowledge(const std::size_t slot, const message& msg, const std::uint64_t now)
		{
			check_presence(now, true);
			if (!matches_guest(slot, msg) || msg.state != state || msg.round != round || msg.value > 1 ||
				!msg.probe || msg.probe > probe_ ||
				((state == phase::preparing || state == phase::arming || state == phase::recovering) && msg.value))
			{
				return false;
			}

			auto& peer = members[slot];
			const auto& sent = probes_[msg.probe % probes_.size()];
			if (msg.wake != peer.wake || msg.probe <= peer.last_probe || sent.id != msg.probe ||
				sent.state != state || now < sent.at || now - sent.at > presence_ms)
			{
				return false;
			}

			// Clients answer each increasing probe once with their current readiness.
			// Rejecting older replies also prevents an old ready vote undoing unready.
			peer.last_probe = msg.probe;
			peer.heard_at = now;
			peer.acknowledged = true;
			peer.ready = msg.value != 0;
			return true;
		}

		bool set_local_ready(const bool ready)
		{
			if (state != phase::paused)
			{
				return false;
			}

			for (std::size_t i = 0; i < capacity; ++i)
			{
				if (local & (1u << i))
				{
					members[i].ready = ready;
				}
			}

			return true;
		}

		std::uint8_t ready_mask() const
		{
			std::uint8_t mask{};
			for (std::size_t i = 0; i < capacity; ++i)
			{
				if ((roster & (1u << i)) && members[i].ready)
				{
					mask |= 1u << i;
				}
			}

			return mask;
		}

		bool all_acknowledged() const
		{
			for (std::size_t i = 0; i < capacity; ++i)
			{
				if (is_guest(i) && !members[i].acknowledged)
				{
					return false;
				}
			}

			return true;
		}

		bool release_complete(const std::uint8_t connected) const
		{
			if (state != phase::released)
			{
				return false;
			}

			// The engine may disconnect a guest after release but before its reply.
			// Keep the original wire roster for peers still waiting for release.
			for (std::size_t i = 0; i < capacity; ++i)
			{
				if (is_guest(i) && (connected & (1u << i)) && !members[i].acknowledged)
				{
					return false;
				}
			}

			return true;
		}

		bool recover()
		{
			if (!is_frozen(state) || state == phase::blocked)
			{
				return false;
			}

			++round;
			probes_ = {};

			for (auto& peer : members)
			{
				peer.ready = false;
			}

			transition(phase::recovering);
			return true;
		}

		bool request_recovery(const std::size_t slot, const message& msg)
		{
			if (!matches_guest(slot, msg) || !is_frozen(state) || state == phase::blocked ||
				msg.state != phase::recovering || msg.probe || msg.value || !msg.round ||
				msg.round > round || msg.wake <= members[slot].wake)
			{
				return false;
			}

			members[slot].wake = msg.wake;
			return recover();
		}

		bool can_remove(const std::size_t slot) const
		{
			return is_guest(slot) && is_frozen(state) && state != phase::blocked;
		}

		bool present(const std::size_t slot, const std::uint64_t now) const
		{
			return slot < capacity && now >= members[slot].heard_at && now - members[slot].heard_at <= presence_ms;
		}

		bool confirms_leave(const std::size_t slot, const message& msg) const
		{
			// Leaving is terminal for this token, even if a recovery round overtook
			// the notice. It cannot remove a replacement or a later pause generation.
			return can_remove(slot) && matches_guest(slot, msg) && msg.state == phase::removed &&
				msg.round && msg.round <= round && !msg.probe && !msg.value && !msg.wake;
		}

		bool remove(const std::size_t slot)
		{
			if (!can_remove(slot))
			{
				return false;
			}

			roster &= static_cast<std::uint8_t>(~(1u << slot));
			members[slot] = {};
			return recover();
		}

		bool all_present(const std::uint64_t now) const
		{
			for (std::size_t i = 0; i < capacity; ++i)
			{
				if (is_guest(i) && !present(i, now))
				{
					return false;
				}
			}

			return true;
		}

		// Run before consuming queued packets too: an old ready reply must not
		// refresh presence after an interruption and conceal the missing player.
		void check_presence(const std::uint64_t now, const bool roster_matches)
		{
			if (!is_frozen(state) || state == phase::blocked)
			{
				return;
			}

			if (!roster_matches)
			{
				transition(phase::blocked);
			}
			else if (state != phase::recovering && !all_present(now))
			{
				recover();
			}
		}

		phase advance(const std::uint64_t now, const bool roster_matches)
		{
			check_presence(now, roster_matches);
			if (state == phase::preparing)
			{
				if (!roster_matches || now < started_at || now - started_at > prepare_ms)
				{
					transition(phase::cancelled);
				}
				else if (all_present(now) && all_acknowledged())
				{
					transition(phase::arming);
				}
			}
			else if ((state == phase::arming || state == phase::recovering) &&
				all_present(now) && all_acknowledged())
			{
				transition(phase::paused);
			}
			else if (state == phase::paused && ready_mask() == roster && all_acknowledged())
			{
				transition(phase::resuming);
			}
			else if (state == phase::resuming && all_acknowledged())
			{
				transition(ready_mask() == roster ? phase::released : phase::paused);
			}

			return state;
		}

	private:
		struct probe
		{
			std::uint64_t id{};
			std::uint64_t at{};
			phase state{};
		};
		std::array<probe, 32> probes_{};
		std::uint64_t probe_{};

		bool matches_guest(const std::size_t slot, const message& msg) const
		{
			return is_guest(slot) && msg.session == session && msg.generation == generation &&
				msg.match == match && msg.token == members[slot].token;
		}

		void transition(const phase next)
		{
			state = next;
			for (auto& peer : members)
			{
				peer.acknowledged = false;
			}
		}
	};

	class client
	{
	public:
		message current{};
		std::uint64_t heard_at{};
		bool ready{};

		bool begin(const message& msg, const std::uint64_t now)
		{
			if (msg.state != phase::preparing || msg.probe || msg.value > 255 || !msg.session || !msg.generation ||
				!msg.token || msg.round != 1 || msg.wake)
			{
				return false;
			}

			if (current.generation == msg.generation && current.session == msg.session)
			{
				return false;
			}

			if (frozen())
			{
				return false;
			}

			*this = {};
			current = msg;
			heard_at = now;
			return true;
		}

		bool receive(const message& msg, const std::uint64_t now)
		{
			check_presence(now);
			if (!matches_pause(msg) || msg.probe <= current.probe ||
				msg.value > 255 || msg.round < current.round || msg.wake != wake_)
			{
				return false;
			}

			const auto previous = current.state;
			const auto next = msg.state;
			const bool new_round = msg.round > current.round;
			const auto mask = msg.value >> 4;
			const auto previous_mask = current.value >> 4;
			if (!mask || (mask & previous_mask) != mask || (!new_round && mask != previous_mask) ||
				((msg.value & 15) & ~mask))
			{
				return false;
			}

			if ((new_round && next != phase::recovering && next != phase::blocked) ||
				(recovery_pending_ && !new_round))
			{
				return false;
			}

			bool valid = false;
			if (new_round && (is_frozen(previous) || previous == phase::preparing) && previous != phase::blocked)
			{
				valid = true;
			}
			else if (!recovery_pending_ && next == previous)
			{
				valid = true;
			}
			else
			{
				switch (previous)
				{
				case phase::preparing:
					valid = next == phase::arming || next == phase::cancelled;
					break;
				case phase::arming:
				case phase::recovering:
					valid = next == phase::paused || next == phase::blocked;
					break;
				case phase::paused:
					valid = next == phase::resuming || next == phase::blocked;
					break;
				case phase::resuming:
					valid = next == phase::paused || next == phase::blocked || (next == phase::released && ready);
					break;
				default:
					break;
				}
			}

			if (!valid)
			{
				return false;
			}

			if (new_round)
			{
				ready = false;
				recovery_pending_ = false;
			}

			current = msg;
			heard_at = now;
			return true;
		}

		message leave_notice() const
		{
			auto msg = current;
			msg.state = phase::removed;
			msg.probe = msg.value = msg.wake = 0;
			return msg;
		}

		bool dismiss(const message& msg)
		{
			if (!matches_pause(msg) || msg.state != phase::removed || !msg.round || msg.probe || msg.value || msg.wake)
			{
				return false;
			}

			current = msg;
			ready = recovery_pending_ = false;
			return true;
		}

		bool recover()
		{
			if (!frozen() || current.state == phase::blocked)
			{
				return false;
			}

			++wake_;
			ready = false;
			recovery_pending_ = true;
			current.state = phase::recovering;
			return true;
		}

		void check_presence(const std::uint64_t now)
		{
			// A confirmed ready vote stays committed until the host decides. It
			// may already have released and be retransmitting a lost release.
			// Actual local power transitions still call recover() unconditionally.
			if (current.state == phase::resuming && ready)
			{
				return;
			}

			if (!recovery_pending_ && (now < heard_at || now - heard_at > presence_ms))
			{
				recover();
			}
		}

		bool recovery_pending() const
		{
			return recovery_pending_;
		}

		message recovery_request() const
		{
			auto msg = current;
			msg.state = phase::recovering;
			msg.probe = msg.value = 0;
			msg.wake = wake_;
			return msg;
		}

		bool set_ready(const bool value)
		{
			// Entering resuming commits the current readiness until the host either
			// releases the pause or returns everyone to paused for another vote.
			if (current.state != phase::paused || recovery_pending_)
			{
				return false;
			}

			ready = value;
			return true;
		}

		bool frozen() const
		{
			return is_frozen(current.state);
		}

		message acknowledgement() const
		{
			auto msg = current;
			msg.value = ready ? 1 : 0;
			return msg;
		}

	private:
		std::uint64_t wake_{};
		bool recovery_pending_{};

		bool matches_pause(const message& msg) const
		{
			return current.generation && msg.session == current.session && msg.generation == current.generation &&
				msg.match == current.match && msg.token == current.token;
		}
	};
}
