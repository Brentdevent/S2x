# Dedicated settings regression tests

Run `tests\run-tests.cmd` on Windows with Visual Studio 2022 or Build Tools
and the Desktop development with C++ workload. No game installation is needed.
The build workflow also runs this command.

The flags tests compile the production command-line parser and string helpers.
They exercise explicit empty values, a following dash flag or another assignment,
signed values, repeated overrides, case preservation, missing values, and the
unchanged ordinary flag lookup behavior.

The value tests exercise the production boolean parser, including numeric
spellings such as `01`, signed values, decimal prefixes and overflow handling.
Boolean restoration follows the native numeric conversion rather than treating
only the literal `1` as true. See `dedicated_settings_value_notes.md` for the
reverse-engineering evidence and its limits.

The copy tests exercise the deferred `setfromdvar` action with simulated dvar
lookups. They cover late source creation, missing sources, empty values and
latched destinations. Like ordinary `set`, the ledger records the requested
value, not a claim that the engine accepted it; domain rejection remains the
engine's responsibility. These tests do not execute the native `setfromdvar` command.

Command tests cover native-style line splitting separately from token decoding,
including escaped quotes, block and line comments, and deferred-action ordering.
Raw command segments are preserved. Native command splitting counts every quote
and does not skip comments; tokenization then decodes quotes and skips comments.
See `dedicated_settings_command_notes.md` for the evidence and limits.

The config tests use the production admin-config registry. They verify that
startup and nested admin configs remain tracked, while `default_xboxlive.cfg`
never enters the registry, including extensionless and case variants. They also
cover extensionless configs inside dotted directories, explicit extensions and
similarly named ordinary admin configs. The game
also executes that file automatically on each MP rotation, so its writes cannot
be treated as persistent admin overrides. To persist a custom default, put the
explicit assignment in your own server config after executing the stock defaults.

These tests do not execute the native game's command buffer or validate hook
addresses. A runtime regression check is still useful: start with a server config
containing `exec default_xboxlive.cfg` followed by `set scr_dom_scorelimit 250`,
rotate twice, and check both the ledger and live value remain 250.

A tracked config is refused in full if adding its ledger actions exceeds the
engine's exec buffer or the pending-action limit. It does **not** run untracked;
split such a config into smaller files. This refusal is reported in the console.

Diagnostic fields (names, values and config paths) show at most 256 bytes each;
the combined live-limits line shows at most 3000 bytes. This bounds the existing
console formatter without truncating stored settings. Log tests use the actual
format constants with the same CRT formatting API and 4096-byte buffer, including
4055-byte and 1-MiB inputs. Sensitive-value redaction still happens before formatting.

Toggle tests exercise the production deferred-capture helper with simulated
dvars. Native `toggle` and `togglep` still perform the actual value cycling;
capture reads their resulting current or latched value, using the appropriate
native formatting mode. Unsupported no-list types and missing targets are skipped.
