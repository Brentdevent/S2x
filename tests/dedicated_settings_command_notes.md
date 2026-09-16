Native evidence and test scope
==============================

Inspected `D:/S2x/build/research/ghidra/s2_mp64_ship_unpacked.exe` with
Capstone, using PE section mappings and RVAs, alongside
`build/followup-codex-review-2.log`. The audit checkout started at 84e4dc8.

- Cbuf scan at 0x64a320-0x64a357 increments quote count on **every** `"`,
  tests its low bit before accepting `;`, and always accepts CR/LF.
  There is no escape, line-comment, or block-comment handling in that scan.
- The exec scan at 0x64afb0-0x64afdd uses the same rules. It calls
  Cmd_TokenizeString at 0x64b023, targeting 0x64b970.
- Cmd_TokenizeString calls the token worker 0x64ba50 at 0x64b9ff.
  0x64baa6-0x64baf7 stops on `//` and skips `/*...*/` (first close wins).
  An unterminated block stops at the end of the current command string.
- 0x64bb35-0x64bb43 consumes backslash followed by quote inside a quoted
  token and emits just the quote. Other backslashes stay literal; there is
  no backslash-parity rule. 0x64bbb3-0x64bc34 copies unquoted characters,
  including interior quotes, until whitespace or a comment opener.

The review findings are confirmed. The important distinction is that native
tokenization understands comments/escapes but command splitting does not.
In particular, `// comment;set x 1` executes the second command, and block
comments do not span a Cbuf CR/LF boundary. Changing either would change
execution. Original command slices are emitted verbatim, terminated with a
newline, and only then followed by the action. Comments need no stripping
because native tokenization starts afresh on each command.

Run `tests\dedicated_settings_command_tests.cmd` in an x64 Visual Studio
Native Tools prompt. It builds and runs with `/W4 /WX`, both `/Od /RTC1`
and `/O2 /DNDEBUG`; checks remain enabled in both configurations.

The tests include the production header directly. They cover quotes, empty
values, password-like strings, literal backslashes, comments, native command
boundaries, preservation of raw text, and action reachability. The nested
exec test uses the production parser and rewrite callback with a small queue
simulation and test planner; it is not a live engine or ledger integration
test. Native fixed token-buffer limits and overlong-command truncation are
outside this syntax regression suite.
