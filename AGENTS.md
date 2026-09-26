# AGENTS.md

## Project overview

S2x is a Windows x64 C++ client and dedicated-server project for
Call of Duty: WWII.

Much of the project interacts with reverse-engineered engine functions,
fixed addresses, hooks, networking state, and game-specific structures.
Code that looks unusual may intentionally match the original engine's
behavior.

## Development guidelines

- Keep changes focused on the requested feature or bug.
- Avoid unrelated refactoring unless it is necessary for correctness.
- Follow existing component, hook, command, and utility patterns.
- Preserve the calling convention, function signature, register behavior,
  and original-call behavior of hooked engine functions.
- Do not guess engine behavior. Verify it using call sites, disassembly,
  runtime evidence, or an established reference implementation.
- When adding or changing engine-facing structures, preserve field widths,
  alignment, and packing. Add or update `static_assert` checks for verified
  sizes and offsets; do not invent values to satisfy assertions.
- Preserve CRLF line endings in existing source files.

## Validation

- Run `git diff --check` before completing a change.
- For code or build-system changes, build both Debug x64 and Release x64
  when the required Visual Studio components are available.
  Documentation-only changes do not require builds.
- For engine or networking behavior changes, run relevant runtime smoke
  checks when the game and runtime setup are available. Distinguish build
  success from runtime verification.
- Clearly report checks that could not be completed and why.

`generate.bat` initializes submodules and generates `build\s2x.sln` for
Visual Studio 2022. With submodules already initialized, use these commands
from the repository root in a Visual Studio developer shell:

```powershell
.\tools\premake5.exe vs2022
msbuild build\s2x.sln /m /v:minimal /p:Configuration=Debug /p:Platform=x64
msbuild build\s2x.sln /m /v:minimal /p:Configuration=Release /p:Platform=x64
```

## Code Review Rules

### State management

- Flag transient state that survives beyond its intended lifetime and can
  incorrectly affect a later operation. Check success, failure, timeout,
  disconnect, cancellation, reset, and superseding-operation paths.
- Check that hosted-party, direct-connect, reconnect, and dedicated-server
  state are cleared independently where appropriate.
- Check that cleanup preserves state intentionally retained for active
  sessions, reconnect, or retry.

### Threads and callback lifetimes

- Check that engine calls run on the appropriate thread or scheduler
  pipeline. Scheduler callbacks default to the async pipeline.
- Check that data captured by delayed callbacks remains valid until use.
  Callbacks must not apply obsolete work after cancellation, disconnect,
  shutdown, or a superseding operation.

### Engine hooks

- Flag hooks that use an incorrect signature, calling convention, return
  value, or original-function behavior.
- Flag code that accesses engine state before it is initialized or after
  it has been shut down.
- Flag hardcoded addresses or structure offsets that are added or changed
  without evidence that they match the supported game binary.

### Networking

- Check packet bounds and validate the fields required by each out-of-band
  message's contract, including field lengths.
- Before a response changes connection or party state, verify its expected
  source and applicable challenge, protocol, and current request/session
  state. Do not impose response-only checks on discovery requests.
- Check that malformed or unsolicited responses cannot alter connection
  or party state.

### Compatibility

- Flag changes that could unintentionally break stock sessions, direct-IP
  connections, reconnect behavior, hosted games, or dedicated servers.
- Check compatibility across campaign, multiplayer, and zombies where
  affected. Verify mode guards and binary/address selection; zombies uses
  the multiplayer binary.
- Check that client-only behavior is not accidentally executed by a
  dedicated server and vice versa.

### Review scope

- For diff reviews, report concrete issues introduced or exposed by the
  change. Explain the triggering conditions, affected behavior, and
  supporting code evidence. Separate unresolved engine assumptions from
  confirmed findings; runtime reproduction is not required when the code
  establishes the issue.
- Prioritize crashes, memory corruption, invalid engine state, security
  problems, protocol regressions, and incorrect lifecycle handling.
- Do not report purely stylistic or formatting preferences.
- Do not recommend large abstractions unless they solve a concrete issue
  introduced by the pull request.
