# Edge-case plain scalars for comparing schema resolution with go-yaml v2.
import itertools, random, sys
random.seed(int(sys.argv[1]) if len(sys.argv) > 1 else 1)
out = set()
add = out.add

words = ["", "~", "null", "Null", "NULL", "nULL", "y", "Y", "yes", "Yes", "YES", "yEs",
         "true", "True", "TRUE", "tRUE", "on", "On", "ON", "oN", "n", "N", "no", "No", "NO",
         "false", "False", "FALSE", "off", "Off", "OFF", "<<", "=", ".inf", ".Inf", ".INF",
         ".iNF", "+.inf", "+.Inf", "+.INF", "-.inf", "-.Inf", "-.INF", ".nan", ".NaN", ".NAN",
         ".Nan", "+.nan", "-.nan", "inf", "nan", "Inf", "NaN", "infinity", "+inf"]
for w in words: add(w)

def underscored(s):
    s = list(s)
    for _ in range(random.randint(1, 3)):
        s.insert(random.randint(0, len(s)), "_" * random.randint(1, 2))
    return "".join(s)

signs = ["", "+", "-"]
prefixes = ["", "0", "00", "0x", "0X", "0o", "0O", "0b", "0B", "0_x", "0_o"]
alph = ["01", "01234567", "0123456789", "0123456789abcdefABCDEF", "0123456789g8"]
for _ in range(60000):
    d = "".join(random.choice(random.choice(alph)) for _ in range(random.choice([0, 1, 2, 3, 5, 10, 16, 19, 20, 21, 22, 25, 64, 65])))
    s = random.choice(signs) + random.choice(prefixes) + d
    add(s)
    add(underscored(s))
for b in [2**63 - 1, 2**63, 2**63 + 1, 2**64 - 1, 2**64, 2**64 + 1, 10**19, 10**20]:
    for sg in signs:
        add(f"{sg}{b}"); add(f"{sg}0x{b:x}"); add(f"{sg}0o{b:o}"); add(f"{sg}0{b:o}"); add(f"{sg}0b{b:b}")
        add(f"{sg}{b-1}"); add(underscored(f"{sg}{b}"))

for _ in range(60000):
    ip = "".join(random.choice("0123456789") for _ in range(random.choice([0, 0, 1, 2, 3, 17])))
    fp = "".join(random.choice("0123456789") for _ in range(random.choice([0, 1, 2, 5])))
    dot = random.choice(["", ".", "."])
    e = random.choice(["", "", "e", "E", "e+", "e-", "E+"])
    ed = "".join(random.choice("0123456789") for _ in range(random.choice([0, 1, 2, 3])))
    s = random.choice(signs) + ip + dot + fp + (e + ed if e else "")
    add(s)
    if random.random() < 0.3: add(underscored(s))
for s in ["1e308", "1e309", "-1e309", "1e400", "1e-400", "4.9e-324", "2.4e-324", "1.7976931348623157e308",
          "1.7976931348623159e308", ".5", "5.", "+.5", ".", "+.", "e5", "1e", "1e+", ".e1", "1_000.5",
          "._5", "1._5", "1e_5", "_1.5", "1.5_", "0.1", "-0.0", "0e0", "00.5", "0x1p-2", "1__0"]:
    add(s)

def num(lo, hi, pad):
    n = random.randint(lo, hi)
    return f"{n:02d}" if pad and random.random() < 0.5 else str(n)
for _ in range(60000):
    y = random.choice(["2001", "2000", "1900", "2004", "2100", "0000", "9999", "200", "20011"])
    s = f"{y}-{num(0, 13, True)}-{num(0, 32, True)}"
    r = random.random()
    if r < 0.6:
        sep = random.choice(["T", "t", " ", "  ", "_"])
        frac = random.choice(["", "", ".5", ",5", ".123456789012", ".", ",", ".x"])
        s += f"{sep}{num(0, 25, True)}:{num(0, 61, True)}:{num(0, 61, True)}{frac}"
        s += random.choice(["", "Z", "z", "+05:00", "-05:30", "+24:00", "+25:00", "+05:60", "+05:61",
                            "+0500", "+05:00x", "Zx", " Z", "-00:00"])
    elif r < 0.7:
        s += random.choice(["x", " ", "T", "-", "0"])
    add(s)

alphabet = "0123456789+-._xXoObBeEtTzZ: yYnN~"
for _ in range(200000):
    add("".join(random.choice(alphabet) for _ in range(random.randint(1, 10))))

for s in sorted(out):
    if "\n" not in s: print(s)
