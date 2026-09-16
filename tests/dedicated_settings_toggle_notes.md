# Native toggle tracking evidence

Base: 61f88e41de238ecb099904a153d3be4237c1c410. Inspected the local MP
image section captures under `D:/S2x/build/research/sections` with Capstone
5.0.7 (addresses below are RVAs). No engine toggle implementation is added.

- Command registration at 0x6647e2 / 0x6647fc references `toggle` / `togglep`
  strings at 0xb97780 / 0xb97788. Their handlers, 0x6655e0 / 0x6655d0,
  both jump to 0x665270 in this captured image.
- 0x665287 rejects argc < 2; 0x6652a4 looks up the target and rejects a
  missing target. argc == 2 selects the type jump table at 0x66559c:
  bool, float, int, enum and protected bool/float/int (0,1,5,6,10,11,12).
  Other no-list types return false at 0x665434. Empty enums are a no-op.
- argc > 2 takes the list path at 0x66543f, including a single explicit
  value. It calls 0xaf8a0, which formats CURRENT (+0x10, decode=true).
  Comparisons and enum conversion stay native. The selected argument goes
  to Dvar_SetCommand at 0x665549. Implicit toggles also read current and
  invoke the native typed setters with external source=1.
- Dvar_SetCommand 0xb2360 calls 0xb2910 with source=1 for an existing
  target; 0xb298f calls the common setter 0xb30c0. Domain checks at
  0xb316d / 0xb31a0 and flag restrictions can reject writes. At 0xb3242,
  DVAR_LATCH (2) sends the value to 0xb2b50 with decode=false and returns
  without changing current; 0xb2d12 copies to latched (+0x20).
- Dvar_ValueToString 0xb4520 tests decode for protected types at
  0xb4564 / 0xb459f / 0xb45da. true calls protected CURRENT getters;
  false formats the supplied union. Native latched formatter 0xaf890
  likewise supplies +0x20 and false. Therefore merely replacing the union
  pointer while retaining true would still lose protected latched writes.

The deferred action records the resulting latched value for DVAR_LATCH,
otherwise current, immediately after the native command. Missing targets,
unsupported no-list types and null formatting results do not record. This
is deliberately a resulting-state policy: domain/permission rejection can
record the unchanged target (including an earlier pending latched value).
There is no native success signal available to the post-command action;
it cannot distinguish a rejected write from a successful no-op. It never
records an unaccepted list candidate. Source-copy requested-value policy
is untouched.

## Validation

Run `tests\dedicated_settings_toggle_tests.cmd` from an x64 VS 2022 Native
Tools prompt. Passed with `/std:c++20 /W4 /WX` in both `/Od /RTC1` and
`/O2 /DNDEBUG`; assertions remain active in Release. `git diff --check`
also passes. `tests\run-tests.cmd` includes this runner.

Coverage: both command spellings; score 100 -> native 250 -> ledger 250
and simulated restore; ordinary supported types; latched/protected storage
and decode selection; empty results; unsupported no-list types; one-value
lists; missing argument/target; null formatting; unchanged state after
native permission/domain rejection. Tests exercise the production helper
with supplied post-native states, not an invented toggle emulator.

Limitations: no live game/Cbuf/_act/rotation execution, no actual native
error injection, and no claim that helper tests execute plan_command or
run_action. Their production wiring was inspected; standalone tests do not
replace building the full client or testing a live game.
