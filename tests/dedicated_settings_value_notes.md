# Bool fallback constraints and evidence

The BOOL paths in value_matches/apply_typed share a checked parser. Other dvar
types, flags and restore ordering are unchanged by this boolean conversion fix.

Local evidence inspected read-only: build/research/ghidra/s2_mp64_ship_unpacked.exe
in the root checkout. Addresses below are image-relative RVAs, not live addresses.
Dvar_SetCommand at B2360 calls B2910 for an existing dvar. B2910 copies the input
using a 0x400-byte buffer, then calls B3CC0 with the type. Its type-zero jump table
entry is B3CFA: calls the import at 1126CA20, tests EAX, and SETNE writes the bool.
The thunk at 65234A uses the same import; the existing local decompile
ghidra/decomp-slice6/652330.c identifies it as atoi. This establishes signed
decimal-prefix conversion followed by nonzero testing, including 01 and -1.
It does not establish safe overflow behavior or justify treating true as a keyword.

The helper accepts ASCII C whitespace, an optional sign, then decimal digits
until the first nondigit (including NUL). No digits means false. Thus true/TRUE,
false, yes and hex-prefixed 0x10 convert to false; 1junk and 1.5 convert to true.
This deliberately replaces the previous true alias with the evidenced native
semantics. Leading zeroes are decimal, not octal. Only signed 32-bit magnitudes
are accepted, using a pre-multiplication bound; no unchecked atoi or signed
accumulation is introduced. Overflow and inputs of 1024 or more bytes return
unknown: comparison reports no match and the typed bool setter does nothing,
preserving the result of the preceding engine command rather than guessing.
The engine command itself still runs and may have implementation-specific
overflow/truncation behavior; this patch does not sanitize or replace that path.
The existing generic fallback warning may still appear when the bool helper
declines a write. No logging or restore-count behavior was changed.

The parser models ASCII whitespace, not locale-specific extended whitespace.
Protected bool types are unchanged. No native game execution or full client
build is claimed by this focused test runner; the whole-branch gate is separate.
Tests include the actual production header, compare representable samples to
the host CRT atoi, cover numeric-prefix/keyword/boundary/NUL/oversize cases and
a 20,001-value signed sweep. The runner builds and executes debug (/RTC1) and
optimized NDEBUG variants with /W4 /WX; checks remain active in both modes.
