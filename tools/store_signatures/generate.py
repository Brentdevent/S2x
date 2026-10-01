import argparse
import bisect
import collections
import difflib
import hashlib
import os
import pickle
import re
import struct
import sys

import pefile
from capstone import CS_ARCH_X86, CS_MODE_64, Cs
from capstone.x86 import X86_OP_IMM, X86_OP_MEM, X86_REG_RIP

TEXT, RDATA, DATA = 0, 1, 2
SECTION_IDS = {".text": TEXT, ".rdata": RDATA, ".data": DATA}

ADDRESS_PATTERNS = [
    re.compile(r"\b(0x[0-9A-Fa-f]+)_g\b"),
    re.compile(r"\bselect\(\s*(0x[0-9A-Fa-f]+)\s*,"),
    re.compile(r"\bsymbol<[^;{}]*?>\s*\w+\s*\{\s*(0x[0-9A-Fa-f]+)", re.S),
]

COMMENT = re.compile(r"//[^\n]*|/\*.*?\*/", re.S)

MAX_PATTERN = 160
BACKTRACK = 32

md = Cs(CS_ARCH_X86, CS_MODE_64)
md.detail = True


class Image:
    def __init__(self, path):
        pe = pefile.PE(path, fast_load=True)
        self.path = path
        self.base = pe.OPTIONAL_HEADER.ImageBase
        self.sections = []
        for s in pe.sections:
            name = s.Name.rstrip(b"\0").decode(errors="replace")
            if any(x[0] == name for x in self.sections):
                continue
            data = s.get_data()[: s.Misc_VirtualSize]
            data += b"\0" * (s.Misc_VirtualSize - len(data))
            self.sections.append((name, s.VirtualAddress, s.Misc_VirtualSize, data))
        self.text = self.section(".text")
        self.rdata = self.section(".rdata")
        directory = pe.OPTIONAL_HEADER.DATA_DIRECTORY[3]
        pdata = pe.get_data(directory.VirtualAddress, directory.Size)
        funcs = set()
        lo, hi = self.text[1], self.text[1] + self.text[2]
        for i in range(0, len(pdata) - 11, 12):
            begin, end, _ = struct.unpack_from("<III", pdata, i)
            if lo <= begin < hi and begin < end:
                funcs.add((begin, end))
        self.funcs = sorted(funcs)
        self.starts = [f[0] for f in self.funcs]

    def section(self, name):
        for s in self.sections:
            if s[0] == name:
                return s
        raise RuntimeError(f"{self.path}: missing {name}")

    def section_of(self, rva):
        for s in self.sections:
            if s[1] <= rva < s[1] + s[2]:
                return s[0]
        return None

    def read(self, rva, size):
        for _, va, vsize, data in self.sections:
            if va <= rva < va + vsize:
                return data[rva - va: rva - va + size]
        return b""

    def func_at(self, rva):
        i = bisect.bisect_right(self.starts, rva) - 1
        if i >= 0 and self.funcs[i][0] <= rva < self.funcs[i][1]:
            return self.funcs[i]
        return None


def disasm(image, begin, end):
    code = image.read(begin, end - begin)
    out = []
    for insn in md.disasm(code, begin):
        raw = bytearray(insn.bytes)
        mask = bytearray(b"\1" * len(raw))
        target = None
        operand = 0
        kind = None
        enc = insn.encoding
        for op in insn.operands:
            if op.type == X86_OP_MEM and op.mem.base == X86_REG_RIP and enc.disp_size:
                target = insn.address + insn.size + op.mem.disp
                operand = enc.disp_offset
                kind = "mem"
                for k in range(enc.disp_size):
                    mask[enc.disp_offset + k] = 0
        if insn.group(1) or insn.group(2) or insn.group(7):
            if insn.operands and insn.operands[0].type == X86_OP_IMM and enc.imm_size:
                target = insn.operands[0].imm
                operand = enc.imm_offset if enc.imm_size == 4 else 0
                kind = "call" if insn.mnemonic == "call" else "br"
                for k in range(enc.imm_size):
                    mask[enc.imm_offset + k] = 0
        if enc.imm_size == 8:
            for k in range(8):
                mask[enc.imm_offset + k] = 0
        out.append((insn.address, insn.size, insn.mnemonic, bytes(raw), bytes(mask), target, kind, operand))
    return out


def exact_hash(insns):
    h = hashlib.sha1()
    for ins in insns:
        raw, mask = ins[3], ins[4]
        h.update(bytes(b if m or ins[6] == "br" and len(raw) == 2 else 0 for b, m in zip(raw, mask)))
    return h.digest()


def loose_hash(insns):
    return hashlib.sha1("|".join(f"{i[2]}{i[1]}" for i in insns).encode()).digest()


def summarize(image):
    result = {}
    lo, hi = image.rdata[1], image.rdata[1] + image.rdata[2]
    for begin, end in image.funcs:
        insns = disasm(image, begin, end)
        calls = [i[5] for i in insns if i[6] == "call"]
        strings = set()
        for i in insns:
            if i[6] == "mem" and lo <= i[5] < hi:
                value = c_string(image, i[5])
                if value and len(value) >= 5:
                    strings.add(value)
        result[begin] = (end - begin, exact_hash(insns), loose_hash(insns), calls, frozenset(strings))
    return result


def cached(work, name, fn):
    path = os.path.join(work, name + ".pkl")
    if os.path.exists(path):
        with open(path, "rb") as f:
            return pickle.load(f)
    value = fn()
    with open(path, "wb") as f:
        pickle.dump(value, f)
    return value


def match_functions(s, m):
    fmap = {}
    used = set()

    def unique(key):
        hs = collections.defaultdict(list)
        hm = collections.defaultdict(list)
        for a, v in s.items():
            if a not in fmap:
                hs[v[key]].append(a)
        for a, v in m.items():
            if a not in used:
                hm[v[key]].append(a)
        tier = "exact" if key == 1 else "loose"
        for h, items in hs.items():
            other = hm.get(h)
            if len(items) == 1 and other and len(other) == 1:
                fmap[items[0]] = (other[0], tier)
                used.add(other[0])

    def propagate():
        while True:
            added = 0
            for a, (b, _) in list(fmap.items()):
                ca, cb = s[a][3], m[b][3]
                if len(ca) != len(cb):
                    continue
                for x, y in zip(ca, cb):
                    if x in s and y in m and x not in fmap and y not in used:
                        if abs(s[x][0] - m[y][0]) <= 0.35 * s[x][0] + 24:
                            tier = "exact" if s[x][1] == m[y][1] else "loose" if s[x][2] == m[y][2] else "graph"
                            fmap[x] = (y, tier)
                            used.add(y)
                            added += 1
            if not added:
                return

    def tier_of(x, y):
        return "exact" if s[x][1] == m[y][1] else "loose" if s[x][2] == m[y][2] else "graph"

    def by_strings():
        hs = collections.defaultdict(list)
        hm = collections.defaultdict(list)
        for a, v in s.items():
            if a not in fmap and v[4]:
                hs[v[4]].append(a)
        for a, v in m.items():
            if a not in used and v[4]:
                hm[v[4]].append(a)
        for key, items in hs.items():
            other = hm.get(key)
            if len(items) == 1 and other and len(other) == 1:
                fmap[items[0]] = (other[0], tier_of(items[0], other[0]))
                used.add(other[0])

    def by_single_string():
        owners_s = collections.defaultdict(set)
        owners_m = collections.defaultdict(set)
        for a, v in s.items():
            for value in v[4]:
                owners_s[value].add(a)
        for a, v in m.items():
            for value in v[4]:
                owners_m[value].add(a)
        candidates = collections.defaultdict(collections.Counter)
        for value, items in owners_s.items():
            other = owners_m.get(value)
            if len(items) == 1 and other and len(other) == 1:
                x, y = next(iter(items)), next(iter(other))
                candidates[x][y] += 1
        for x, counter in candidates.items():
            y, count = counter.most_common(1)[0]
            if x in fmap or y in used or count != sum(counter.values()):
                continue
            fmap[x] = (y, tier_of(x, y))
            used.add(y)

    unique(1)
    propagate()
    unique(2)
    propagate()
    by_strings()
    propagate()
    by_single_string()
    propagate()
    return fmap


def extend_with_votes(s, m, fmap, votes):
    used = {b for b, _ in fmap.values()}
    added = []
    for x, counter in votes.items():
        if x in fmap or x not in s:
            continue
        y, count = counter.most_common(1)[0]
        if y in m and y not in used and count == sum(counter.values()):
            tier = "exact" if s[x][1] == m[y][1] else "loose" if s[x][2] == m[y][2] else "graph"
            fmap[x] = (y, tier)
            used.add(y)
            added.append(x)
    return added


def align(sins, mins):
    sm = difflib.SequenceMatcher(None, [i[2] for i in sins], [i[2] for i in mins], autojunk=False)
    pairs = []
    for tag, i1, i2, j1, j2 in sm.get_opcodes():
        if tag == "equal":
            pairs.extend(zip(range(i1, i2), range(j1, j2)))
    return pairs


def vote_targets(steam, store, fmap, keys=None, votes=None):
    if votes is None:
        votes = collections.defaultdict(collections.Counter)
    for a in keys if keys is not None else list(fmap):
        b, tier = fmap[a]
        sins = disasm(steam, a, steam.funcs[bisect.bisect_left(steam.starts, a)][1])
        mins = disasm(store, b, store.funcs[bisect.bisect_left(store.starts, b)][1])
        if tier != "graph" and len(sins) == len(mins):
            pairs = zip(range(len(sins)), range(len(mins)))
        else:
            pairs = align(sins, mins)
        for i, j in pairs:
            x, y = sins[i], mins[j]
            if x[5] is not None and y[5] is not None and x[6] == y[6]:
                votes[x[5]][y[5]] += 1
    return votes


def extract_addresses(src):
    found = {}
    for root, _, files in os.walk(src):
        if os.sep + "arxan" in root:
            continue
        for name in files:
            if not name.endswith((".cpp", ".hpp")):
                continue
            path = os.path.join(root, name)
            with open(path, encoding="utf-8", errors="replace") as f:
                text = COMMENT.sub(lambda c: re.sub(r"[^\n]", " ", c.group(0)), f.read())
            for pattern in ADDRESS_PATTERNS:
                for match in pattern.finditer(text):
                    value = int(match.group(1), 16)
                    if value:
                        line = text.count("\n", 0, match.start()) + 1
                        found.setdefault(value, f"{os.path.relpath(path, src)}:{line}")
    return found


def c_string(image, rva):
    data = image.read(rva, 512)
    end = data.find(b"\0")
    if end < 4:
        return None
    value = data[:end]
    if all(32 <= c < 127 or c in (9, 10, 13) for c in value):
        return value + b"\0"
    return None


def steam_sig(a, steam, store):
    steam_region = steam.text[3]
    store_region = store.text[3]
    for back in range(0, 48):
        start = a - back
        insns = disasm(steam, start, start + MAX_PATTERN + back + 16)
        index = next((k for k, i in enumerate(insns) if i[0] == a), None)
        if index is None:
            continue
        raw, mask = bytearray(), bytearray()
        for ins in insns:
            raw += ins[3]
            mask += ins[4]
            if len(raw) > MAX_PATTERN + back:
                break
            if ins[0] < a or len(raw) < 8 or not has_anchor(mask):
                continue
            if count_matches(steam_region, raw, mask) != 1:
                continue
            regex = compile_pattern(raw, mask)
            hits = []
            for match in regex.finditer(store_region):
                hits.append(match.start())
                if len(hits) > 1:
                    break
            if len(hits) == 1:
                return store.text[1] + hits[0] + back
            break
    return None


def resolve(a, steam, store, fmap, votes, manual):
    if a in manual:
        return (manual[a], "manual") if manual[a] else (None, "excluded")
    target, how = resolve_auto(a, steam, store, fmap, votes)
    if target is None and steam.section_of(a) == ".text":
        found = steam_sig(a, steam, store)
        if found is not None:
            return found, "steamsig"
    return target, how


def resolve_auto(a, steam, store, fmap, votes):
    section = steam.section_of(a)
    if section == ".text":
        func = steam.func_at(a)
        if func and func[0] in fmap:
            b, tier = fmap[func[0]]
            if tier != "graph" or a == func[0]:
                return b + (a - func[0]), tier
            sins = disasm(steam, func[0], func[1])
            mfunc = store.func_at(b)
            mins = disasm(store, mfunc[0], mfunc[1])
            for i, j in align(sins, mins):
                if sins[i][0] == a:
                    return mins[j][0], "graph"
            return None, "unaligned"
    if a in votes:
        target, count = votes[a].most_common(1)[0]
        total = sum(votes[a].values())
        if count / total >= 0.8:
            return target, "xref"
        return None, "ambiguous"
    if section == ".rdata":
        value = c_string(steam, a)
        if value:
            _, va, _, data = store.rdata
            pos = data.find(value)
            if pos != -1 and data.find(value, pos + 1) == -1:
                return va + pos, "string"
    return None, "unmatched"


def compile_pattern(raw, mask):
    return re.compile(b"".join(re.escape(bytes([b])) if m else b"." for b, m in zip(raw, mask)), re.S)


def count_matches(region, raw, mask, limit=2):
    best, start, run_start = (0, 0), 0, None
    for i in range(len(raw) + 1):
        if i < len(raw) and mask[i]:
            if run_start is None:
                run_start = i
        elif run_start is not None:
            if i - run_start > best[0]:
                best = (i - run_start, run_start)
            run_start = None
    length, start = best
    if length < 2:
        return limit
    literal = bytes(raw[start: start + length])
    regex = compile_pattern(raw, mask)
    found = 0
    pos = region.find(literal)
    while pos != -1:
        begin = pos - start
        if begin >= 0 and regex.match(region, begin):
            found += 1
            if found >= limit:
                return found
        pos = region.find(literal, pos + 1)
    return found


def has_anchor(mask):
    return any(mask[i] and mask[i + 1] for i in range(len(mask) - 1))


def format_pattern(raw, mask):
    return " ".join(f"{b:02X}" if m else "?" for b, m in zip(raw, mask))


class Signer:
    def __init__(self, store):
        self.store = store
        self.region = store.text[3]
        self.text_va = store.text[1]
        self.xrefs = None
        self.cache = {}

    def insns_from(self, rva):
        func = self.store.func_at(rva)
        if func:
            key = func[0]
            if key not in self.cache:
                self.cache[key] = disasm(self.store, func[0], func[1] + MAX_PATTERN)
            insns = self.cache[key]
            return insns, func
        insns = disasm(self.store, rva, rva + MAX_PATTERN * 2)
        return insns, (rva, rva + MAX_PATTERN * 2)

    def grow(self, insns, index):
        raw, mask = bytearray(), bytearray()
        for ins in insns[index:]:
            raw += ins[3]
            mask += ins[4]
            if len(raw) > MAX_PATTERN:
                return None
            if len(raw) >= 6 and has_anchor(mask) and count_matches(self.region, raw, mask) == 1:
                return raw, mask
        return None

    def at(self, rva):
        insns, _ = self.insns_from(rva)
        index = next((k for k, i in enumerate(insns) if i[0] <= rva < i[0] + i[1]), None)
        if index is None:
            return None
        for back in range(0, BACKTRACK + 1):
            if index - back < 0:
                break
            found = self.grow(insns, index - back)
            if found:
                origin = insns[index - back][0]
                return found[0], found[1], rva - origin
        return None

    def build_xrefs(self):
        self.xrefs = collections.defaultdict(list)
        for begin, end in self.store.funcs:
            for ins in disasm(self.store, begin, end):
                if ins[5] is not None and ins[7]:
                    self.xrefs[ins[5]].append(ins)

    def via_xref(self, rva):
        if self.xrefs is None:
            self.build_xrefs()
        for ins in self.xrefs.get(rva, [])[:16]:
            found = self.at(ins[0])
            if found:
                raw, mask, offset = found
                return raw, mask, offset, ins[7], ins[1]
        return None

    def sign(self, rva):
        section = self.store.section_of(rva)
        if section == ".text":
            found = self.at(rva)
            if found:
                raw, mask, offset = found
                return format_pattern(raw, mask), offset, 0, 0, TEXT
        found = self.via_xref(rva)
        if found:
            raw, mask, offset, operand, length = found
            return format_pattern(raw, mask), offset, operand, length, TEXT
        if section == ".rdata":
            value = c_string(self.store, rva)
            if value and len(value) >= 6:
                _, va, _, data = self.store.rdata
                if data.find(value) == rva - va and data.find(value, rva - va + 1) == -1:
                    return " ".join(f"{b:02X}" for b in value), 0, 0, 0, RDATA
        return None


def emit(path, entries, store):
    lines = [
        "#include <std_include.hpp>",
        "#include \"store.hpp\"",
        "",
        "namespace game::store",
        "{",
        f"\tconst std::uint32_t signature_image_timestamp = 0x{store.timestamp:08X};",
        "",
        "\tconst std::vector<signature> signatures =",
        "\t{",
    ]
    for e in entries:
        lines.append(f"\t\t{{0x{e[0]:X}, \"{e[1]}\", {e[2]}, {e[3]}, {e[4]}, {e[5]}}},")
    lines += ["\t};", "}", ""]
    with open(path, "w", newline="\r\n") as f:
        f.write("\n".join(lines))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--steam", required=True)
    parser.add_argument("--store", required=True)
    parser.add_argument("--src", default=os.path.join(os.path.dirname(__file__), "..", "..", "src", "client"))
    parser.add_argument("--out", default=os.path.join(os.path.dirname(__file__), "..", "..", "src", "client", "game", "store_signatures.cpp"))
    parser.add_argument("--work", default=os.path.join(os.path.dirname(__file__), "work"))
    parser.add_argument("--manual", default=os.path.join(os.path.dirname(__file__), "manual.txt"))
    parser.add_argument("--report", default=None)
    args = parser.parse_args()

    os.makedirs(args.work, exist_ok=True)
    steam = Image(args.steam)
    store = Image(args.store)
    store.timestamp = pefile.PE(args.store, fast_load=True).FILE_HEADER.TimeDateStamp

    s = cached(args.work, "steam_funcs", lambda: summarize(steam))
    m = cached(args.work, "store_funcs", lambda: summarize(store))
    def build():
        fmap = match_functions(s, m)
        votes = vote_targets(steam, store, fmap)
        while True:
            added = extend_with_votes(s, m, fmap, votes)
            if not added:
                return fmap, votes
            vote_targets(steam, store, fmap, added, votes)

    fmap, votes = cached(args.work, "matches", build)
    print(f"functions steam {len(s)} store {len(m)} matched {len(fmap)} "
          + " ".join(f"{k}={v}" for k, v in sorted(collections.Counter(t for _, t in fmap.values()).items())))

    manual = {}
    if os.path.exists(args.manual):
        with open(args.manual) as f:
            for line in f:
                parts = line.split("#")[0].split()
                if len(parts) >= 2:
                    manual[int(parts[0], 16)] = int(parts[1], 16)

    addresses = extract_addresses(os.path.abspath(args.src))
    for a in manual:
        addresses.setdefault(a, "manual.txt")
    signer = Signer(store)
    entries, report = [], []
    stats = collections.Counter()
    for a in sorted(addresses):
        target, how = resolve(a, steam, store, fmap, votes, manual)
        if target is None:
            stats[how] += 1
            report.append(f"UNRESOLVED {how:10} 0x{a:X} {addresses[a]}")
            continue
        signed = signer.sign(target)
        if not signed:
            stats["unsigned"] += 1
            report.append(f"UNSIGNED   {how:10} 0x{a:X} -> 0x{target:X} {addresses[a]}")
            continue
        stats[how] += 1
        entries.append((a,) + signed)
        report.append(f"OK         {how:10} 0x{a:X} -> 0x{target:X} {addresses[a]}")

    emit(args.out, entries, store)
    summary = f"addresses {len(addresses)} signed {len(entries)} " + " ".join(f"{k}={v}" for k, v in sorted(stats.items()))
    print(summary)
    if args.report:
        with open(args.report, "w") as f:
            f.write(summary + "\n" + "\n".join(report) + "\n")


if __name__ == "__main__":
    sys.exit(main())
