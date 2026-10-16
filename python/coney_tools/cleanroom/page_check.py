"""`coney-tools cleanroom page-check`: the scripted check of a research page against the page rules.

A page may carry facts (addresses, sizes, layouts, field meanings, constants, ids, formats, names) and observed
behaviour written as outcomes. This check rejects what a script can recognise of everything else: code, assembly,
instruction words, decompiler names, step lists and branch or loop shapes tied to the original's code, banned
phrases, and images without a capture record. It is the first filter only; two research reviewers judge every
section that passes. The same check runs in the analyst zone, in the sweep's export, at the public-side import and
in CI, so there is one copy of the rules.

Every rule looks at what a reader sees: HTML entities decoded, Markdown escapes, invisible characters, comments,
tags and link syntax removed, look-alike letters folded, curly quotes straightened; and at whole paragraphs as well
as single lines, so text split across lines is caught too. Every pattern is bounded, so the time a page takes grows
in step with its size.

Findings name the rule and the line, never the matched text, and a failing section's heading and hash are withheld:
the check runs on the public side too, where a failing page's text must not be shown.
"""

import hashlib
import html
import os
import re
import tomllib
import unicodedata
from dataclasses import dataclass, field
from pathlib import Path

MAX_PAGE = 2_000_000  # bytes; larger pages are refused unread

# --- what a reader sees -----------------------------------------------------------------------------------------

# Invisible characters that are not in Unicode's "format" class (which is removed as a whole).
INVISIBLE = re.compile("[\u034f\u115f\u1160\u17b4\u17b5\u180b-\u180f\u3164\ufe00-\ufe0f\uffa0\U000e0100-\U000e01ef]")
QUOTES = str.maketrans(
    {
        "\u2018": "'",
        "\u2019": "'",
        "\u201a": "'",
        "\u201b": "'",
        "\u2032": "'",
        "\u02bc": "'",
        "\u201c": '"',
        "\u201d": '"',
        "\u201e": '"',
        "\u2033": '"',
    }
)
# Cyrillic, Greek and other letters that look like Latin ones (Unicode TR39 confusables, the common part).
CONFUSABLE = str.maketrans(
    "\u0430\u0435\u043e\u0440\u0441\u0443\u0445\u0456\u0458\u0455\u0501\u04bb\u0410\u0412\u0415\u041a\u041c\u041d"
    "\u041e\u0420\u0421\u0422\u0425\u0406\u0408\u0405\u0391\u0392\u0395\u0396\u0397\u0399\u039a\u039c\u039d\u039f"
    "\u03a1\u03a4\u03a5\u03a7\u03bf\u03bd\u03b1\u03b9\u03ba\u03c1\u0261\u0251\u1d00\u0131",
    "aeopcyxijsdhABEKMHOPCTXIJSABEZHIKMNOPTYXovaikpgaAi",
)
WORD = re.compile(r"\w{2,200}")
MICRO_UNIT = re.compile(r"μ[A-Za-z]{1,3}")  # "5 µs": NFKC turns the micro sign into Greek mu


def mixed_script(text):
    """A word that holds ASCII letters, digits or underscores together with a letter that is not Latin: a look-alike
    from another script, also when the whole name is spelled in it (e.g. Lisu letters followed by _00123456)."""
    for m in WORD.finditer(text):
        w = m.group(0)
        if w.isascii() or MICRO_UNIT.fullmatch(w):
            continue
        if not any(c.isascii() and (c.isalnum() or c == "_") for c in w):
            continue
        if any(c.isalpha() and not c.isascii() and not unicodedata.name(c, "").startswith("LATIN") for c in w):
            return True
    return False


MD_ESCAPE = re.compile(r"\\([!-/:-@\[-`{-~])")
TAG = re.compile(r"</?[A-Za-z!][^<>\"']{0,500}(?:(?:\"[^\"]{0,500}\"|'[^']{0,500}')[^<>\"']{0,500}){0,20}>")
LINK = re.compile(r"!?\[([^\[\]]{0,500})\](?:\([^()]{0,500}\)|\[[^\[\]]{0,200}\])")
FOOTNOTE = re.compile(r"\[\^[^\[\]\s]{1,40}\]")
EMPHASIS_US = re.compile(r"(?<![^\W_])_+(?=\S)|(?<=\S)_+(?![^\W_])")


def strip_comments(text):
    """Remove HTML comments in one pass (an unclosed '<!--' is left as text, like any stray '<')."""
    out, i = [], 0
    while (j := text.find("<!--", i)) != -1:
        k = text.find("-->", j + 4)
        if k == -1:
            break
        out.append(text[i:j])
        i = k + 3
    out.append(text[i:])
    return "".join(out)


def clean(text):
    """Text as a reader sees it: entities decoded, escapes and invisible characters removed, look-alikes folded."""
    for _ in range(3):  # entities can be encoded more than once
        decoded = html.unescape(text)
        if decoded == text:
            break
        text = decoded
    text = unicodedata.normalize("NFKC", text)
    text = "".join(c for c in text if unicodedata.category(c) != "Cf")
    text = INVISIBLE.sub("", text).translate(QUOTES).translate(CONFUSABLE)
    return MD_ESCAPE.sub(r"\1", text)


def rendered(text):
    """Cleaned text with markup dropped: comments, tags, link syntax (down to its text), emphasis, code ticks."""
    text = TAG.sub("", strip_comments(text))
    text = LINK.sub(r"\1", FOOTNOTE.sub("", text))
    text = re.sub(r"[*`]|~~", "", text)
    text = re.sub(r"[ \t]*\|[ \t]*", " ", text)  # table cells read as one line
    return EMPHASIS_US.sub("", text)


def raw_letters(text):
    """The text with entities decoded and invisible characters removed, but look-alikes NOT folded."""
    text = unicodedata.normalize("NFKC", html.unescape(html.unescape(text)))
    return INVISIBLE.sub("", "".join(c for c in text if unicodedata.category(c) != "Cf"))


# --- text rules -------------------------------------------------------------------------------------------------

MIPS_REG = (
    r"(?:\$(?:[0-9]{1,2}|zero|at|v[01]|a[0-3]|t[0-9]|s[0-8]|k[01]|gp|sp|fp|ra|f[0-9]{1,2}|vf[0-9]{1,2}"
    r"(?:\.[xyzw]{1,4})?|vi[0-9]{1,2})|(?:zero|at|v[01]|a[0-3]|t[0-9]|s[0-8]|k[01]|gp|sp|fp|ra|f[0-9]{1,2}"
    r"|vf[0-9]{1,2}(?:\.[xyzw]{1,4})?|vi[0-9]{1,2}|acc(?:\.[xyzw]{1,4})?)\b)"
)
MIPS_OPS = (
    r"lui|addiu?|addu|subu?|daddi?u?|dsubu?|lw|sw|lb|lbu|lh|lhu|ld|sd|sb|sh|lw[lr]|sw[lr]|ld[lr]|sd[lr]|"
    r"[ls][wd]c1|lq|sq|[ls]qc2|ll|sc|sll|srl|sra|sllv|srlv|srav|dsll(?:32)?|dsrl(?:32)?|dsra(?:32)?|dsllv|"
    r"dsrlv|dsrav|andi?|ori?|xori?|nor|sltu?|sltiu?|multu?1?|divu?1?|maddu?1?|mfhi1?|mflo1?|mthi1?|mtlo1?|"
    r"movn|movz|beql?|bnel?|blezl?|bgtzl?|bltzl?|bgezl?|bltzall?|bgezall?|beqzl?|bnezl?|bc[01][tf]l?|li|la|"
    r"move|neg|negu|not|[mc][ft]c[0-2]|qm[ft]c2|"
    r"p(?:add|sub|ext|pac|cpy|mul|mad|max|min|abs|ceq|cgt|sll|srl|sra|and|or|xor|nor|rot|exe|int|div|mf|mt|lz)"
    r"[a-z0-9]{0,8}|"
    r"v(?:add|sub|mul|madd|msub|max|mini|ftoi\d+|itof\d+|move|mr32|sqrt|rsqrt|div|iadd|iaddi|isub|iand|ior|lqi|"
    r"sqi|lqd|sqd|ilwr|iswr|clip|opmula|opmsub|callms|callmsr)[a-z]{0,8}|"
    r"(?:add|sub|mul|div|mov|neg|abs|sqrt|rsqrt|madd|msub|max|min|adda|suba|mula|c\.(?:eq|lt|le|f))\.s|"
    r"cvt\.[sw]\.[sw]"
)
CODE_IDENT = r"(?:[A-Za-z]\w{0,60}_\w{1,60}|[a-z][a-z0-9]{0,30}[A-Z]\w{0,60}|\.?L\w{1,40})"  # snake_, camelCase, .Lx
MIPS_OPERAND = (
    r"(?:" + MIPS_REG + r"|-?(?:0x[0-9a-f]{1,8}|\d{1,10})\b|[-\w]{0,40}\(\s*" + MIPS_REG + r"\s*\)|"
    r"%\w{1,10}\(|" + CODE_IDENT + r")"
)
BRANCH_OPS = r"beql?|bnel?|blezl?|bgtzl?|bltzl?|bgezl?|beqzl?|bnezl?|bc[01][tf]l?"
MIPS = re.compile(
    r"(?<![\w.$])(?:" + MIPS_OPS + r")(?:\.[a-z]{1,4})?\s{1,8}" + MIPS_REG + r"\s{0,4},\s{0,4}" + MIPS_OPERAND + r"|"
    r"(?<![\w.$])(?:" + BRANCH_OPS + r")\s{1,8}" + MIPS_REG + r"\s{0,4},(?:[^,\n]{0,40},)?\s{0,4}[A-Za-z_.]\w{0,60}|"
    r"(?<![\w.$])(?:jr|jalr|mfhi|mflo|mthi|mtlo|mfsa|mtsa)\s{1,8}" + MIPS_REG + r"(?!\s*\w)|"
    r"(?<![\w.$])(?:jal|j|bal|b)\s{1,8}(?:0x)?[0-9a-f]{6,8}\b|"
    r"(?<![\w.$])jal\s{1,8}[A-Za-z_]\w{0,60}|"
    r"^\s{0,8}(?:syscall|eret|sync(?:\.[lp])?|nop|deret)\s*$",
    re.IGNORECASE | re.MULTILINE,
)

HEX8_LETTER = r"(?=[0-9]{0,7}[a-f])[0-9a-f]{8}"
INSTRUCTION_WORDS = re.compile(
    r"\b0x[0-9a-f]{6,8}\b[^\n]{0,40}?(?<![\w])" + HEX8_LETTER + r"\b|"  # address, then a raw word
    r"(?<![\w])[0-9a-f]{6,8}:\s{1,8}(?:0x)?[0-9a-f]{8}\b|"  # dump line: "addr: word"
    r"\bpatch\s{0,4}=\s{0,4}[01]\s{0,4},\s{0,4}EE\s{0,4},|"  # PCSX2 patch line
    r"(?:^|\s)\.(?:word|long|dword|half|byte)\s{1,8}(?:0x)?[0-9a-f]{1,16}",
    re.IGNORECASE,
)
HEX_RUN = re.compile(r"(?<![\w])(?:0x)?[0-9a-f]{8}\b(?:[\s,;:]{1,8}(?:0x)?[0-9a-f]{8}\b){2,64}", re.IGNORECASE)
BYTE_RUN = re.compile(r"(?<![\w])[0-9a-f]{2}\b(?:[\s,]{1,4}[0-9a-f]{2}\b){7,256}", re.IGNORECASE)


def instruction_words(text):
    if INSTRUCTION_WORDS.search(text):
        return True
    for m in HEX_RUN.finditer(text):  # three or more 8-digit words; a letter rules out runs of decimal ids
        if re.search(r"[a-f]", m.group(0).lower().replace("0x", "")):
            return True
    for m in BYTE_RUN.finditer(text):  # a byte dump
        if len(re.findall(r"\b[0-9]?[a-f][0-9a-f]?\b", m.group(0).lower())) >= 2:
            return True
    return False


DECOMPILER = re.compile(
    r"\b(?:FUN|DAT|LAB|PTR|SUB|LOC|switchD|caseD|joined_r0x)_[0-9a-f]{5,16}\b|"
    r"\b(?:UNK|OFF|BYTE|WORD|DWORD|QWORD|STRUCT|ARRAY|ASCII|STR)_[0-9a-f]{6,16}\b|\b(?:switchD|caseD)_\w{1,60}",
    re.IGNORECASE,
)
DECOMPILER_CS = re.compile(
    r"\b(?:p{0,3}[uibcslfdvzx]|p{1,3})Var\d{1,4}\b|\b(?:a[uicbs]?|p[uicbs]?)Stack_\w{1,40}|\bparam_\d{1,3}\b|"
    r"\blocal_(?:res)?[0-9a-f]{1,5}\b|\bunaff_\w{1,40}|\bextraout_\w{1,40}|\bin_stack_\w{1,40}|"
    r"\bin_(?:v[01]|a[0-3]|t\d|s\d|ra|sp|gp|fp|at|hi|lo)\b|"
    r"\b(?:ZEXT|SEXT|SUB|CONCAT|CARRY|SCARRY|SBORROW|POPCOUNT|LZCOUNT)\d{2,3}\b|\bundefined[1-8]\b"
)

# Plain-text code. Each pattern is bounded; the callers run it on lines and on paragraphs.
ASSIGN = re.compile(
    r"[A-Za-z_]\w{0,60}(?:\[[^\]\n]{0,60}\]|(?:->|\.)\w{1,60}){0,6}\s{0,4}"
    r"(?:[-+*/%&|^]|<<|>>)?=\s{0,4}([^=\s;][^;\n]{0,200});"
)
CODE_TEXT = re.compile(
    r"\b(?:if|while|switch|for)\s{0,4}\([^)\n]{0,200}\)\s{0,4}\{|"  # if (...) {
    r"\bfor\s{0,4}\([^)\n]{0,200};[^)\n]{0,200};|"  # for (;;
    r"\breturn\b[^.;\n]{0,200};|"  # return ...;
    r"\{[^{}\n]{0,200};[^{}\n]{0,200}\}|"  # { ...; }
    r"\b[A-Za-z_]\w{0,60}\s{0,4}(?:[-+*/%&|^]=|<<=|>>=|:=)\s{0,4}[\w(-]|"  # x -= 1, x := y
    r"\b[A-Za-z_]\w{0,60}\s{0,4}\u2190|"  # x <- y (arrow)
    r"\b[a-z_]\w{0,60}\+\+\s{0,4}[;)]|\b[a-z_]\w{0,60}\s{0,4}--\s{0,4};|"
    r"^\s{0,12}(?:def\s+\w+\s*\(|for\s+\w+(?:\s*,\s*\w+){0,4}\s+in\s+\S|while\s+[^\n:]{1,120}[<>=!]|elif\b|"
    r"if\s+[^\n:]{1,120}(?:==|!=|<=|>=|<|>|\bnot\b|\band\b|\bor\b))[^\n]{0,200}:\s*$|"  # python-like header
    r"^\s{0,12}if\s+[^\n:]{1,120}:\s*\w{1,60}\s*=|"  # if x: y = z
    r"\bif\s{0,4}\([^)\n]{0,200}\)\s{0,4}[A-Za-z_][^;\n{]{0,200};|"  # if (...) stmt;
    r"\b[A-Za-z_]\w{0,60}\([^()\n]{1,200}\)\s{0,4};|"  # Call(args);
    r"\b[A-Za-z_]\w{0,60}\s{0,4}=\s{0,4}[A-Za-z_]\w{0,60}\([^()\n]{0,200}\)|"  # x = Call(...)
    r"\bif\b[^\n]{1,120}\bthen\b[^\n]{0,120}=[^\n]{0,120}\bend\b",
    re.MULTILINE,
)  # if ... then x = y end
# PowerShell conditions hold $variables or -eq/-ne/... operators; C and C-like conditions hold neither.
NOT_PS_CONDITION = r"(?![^)\n]{0,200}(?:\$|\s-[a-z]{2,12}\b))"
SHELL_CODE = re.compile(  # in a guide's shell block: code that is not shell (shell has its own = and ;)
    r"\b(?:if|while|switch|for)\s{0,4}\(" + NOT_PS_CONDITION + r"[^)\n]{0,200}\)\s{0,4}[{A-Za-z_]|"
    r"\bfor\s{0,4}\(" + NOT_PS_CONDITION + r"[^)\n]{0,200};|"
    r"\breturn\b[^.;\n]{0,200};|\b(?:int|void|float|char|u32|s32|f32)\s{1,4}\w{1,60}\s{0,4}[=(]|->\w|"
    r"(?<![\w$:])[A-Za-z_]\w{0,60}\s{0,4}(?:[-+*/%&|^]=|<<=|>>=)\s{0,4}\w|"
    r"(?<![\w$:])[A-Za-z_]\w{0,60}\[[^\]\n]{0,60}\]\s{0,4}=(?!=)|"
    r"(?<![\w$.:-])[A-Za-z_]\w{0,60}(?:->\w{1,60}|\.\w{1,60}){0,6}\s{1,4}=\s{1,4}\S|"  # spaced "x = y" (no $ or $env:)
    r"(?<![\w$-])[a-z_]\w{0,60}(?:\+\+|--)(?=[\s;)]|$)|"  # hp--, timer++
    r"^\s{0,12}(?:if|elif|while|for|def)\b[^\n]{0,200}:\s*$|"  # python-like block
    r"^\s{0,12}[A-Za-z_]\w{0,60}\([^()\n]{1,200}\)\s{0,4};",
    re.MULTILINE,
)  # Call(args);


def code_text(text):
    text = INLINE_CODE.sub(" ", text)
    if CODE_TEXT.search(text):
        return True
    for m in ASSIGN.finditer(text):  # "a = b;" is code when the value is more than a plain number or word
        value = m.group(1).strip()
        if re.search(r"[-+*/%&|^()\[\]<>]|\w\s+\w", value) and not re.fullmatch(
            r"-?[\d.]+\s{0,2}[A-Za-zµμ%/^²³\d]{0,10}", value
        ):  # 9.8 m/s
            return True
    return False


BRANCH = re.compile(r"\belse[\s,-]{1,4}if\b|\belif\b|\botherwise,?\s{1,4}if\b|\bfailing\s{1,4}that\b", re.IGNORECASE)
IDENT = r"(?:[A-Z][a-z0-9]{1,30}[A-Z]\w{0,60}|\w{1,60}::\w{1,60}|[A-Za-z]\w{0,60}_\w{1,60}|\w{1,60}\(\))"
CALL_VERB = (
    r"(?:calls|invokes|jumps\s+to|branches\s+to|tail-calls|hands\s+(?:off|control)\s+to|delegates\s+to|"
    r"dispatches\s+to|(?:passes|transfers)\s+control\s+to)"
)
HELPER = re.compile(
    r"\b(?i:it|which|this|that|the\s+(?:function|routine|handler|method|subroutine|callee|caller))\s{1,4}"
    r"(?:then\s+|first\s+|also\s+)?" + CALL_VERB + r"\s{1,4}(?:[A-Z]\w{0,60}|" + IDENT + r")|"
    r"\b"
    + IDENT
    + r"\s{1,4}(?:then\s+|first\s+|also\s+)?(?:"
    + CALL_VERB
    + r"|uses|reuses|wraps)\s{1,4}"
    + IDENT
    + r"|"
    r"\b" + IDENT + r"\s{1,4}(?:then\s+|first\s+|also\s+)?" + CALL_VERB + r"\s{1,4}[A-Za-z]\w{0,60}|"
    r"\bis\s{1,4}(?:called|invoked)\s{1,4}(?:by|from)\b|"
    r"\bwhich\s{1,4}then\s{1,4}(?:calls|invokes|jumps|branches|returns)\b|"
    r"\b(?i:it)\s{1,4}(?:then\s+|first\s+)?(?:calls|invokes)\b.{0,100}?\bwhich\s{1,4}then\b|"  # 8.1's own example
    r"\bcall\s{1,4}(?:chain|graph|tree|site)s?\b|\bcallees?\b|\bcallers?\b"
)
LOOP = re.compile(
    r"\b(?:loops?|iterates?)\s{1,4}(?:over|through|across)\b|\bloop\s{1,4}(?:body|counter|index|variable)\b|"
    r"\b(?:for|while|do-while)\s{1,4}loop\b|\bfor\s{1,4}each\s{1,4}iteration\b|\blocal\s{1,4}variables?\b|"
    r"\bstack\s{1,4}(?:variable|slot|frame)s?\b",
    re.IGNORECASE,
)
PHRASES = re.compile(
    r"\bin\s{1,4}the\s{1,4}original'?s?\s{1,4}order\b|"
    r"\bas\s{1,4}the\s{1,4}(?:original|game|binary|executable)\s{1,4}does\b|"
    r"\blike\s{1,4}the\s{1,4}(?:original|game)\s{1,4}does\b|\bkeeps?\s{1,4}the\s{1,4}original'?s?\s{1,4}arithmetic\b|"
    r"\bsame\s{1,4}order\s{1,4}as\s{1,4}the\s{1,4}(?:original|game)\b|"
    r"\b(?:mirrors?|follows?|matches|copies|reproduces)\s{1,4}the\s{1,4}(?:original(?:\s{1,4}game)?|game)'?s?\s{1,4}"
    r"(?:code|logic|order|structure|control\s{1,4}flow|implementation)\b|"
    r"\bthe\s{1,4}(?:original(?:\s{1,4}game)?|game|binary|executable)'?s?\s{1,4}"
    r"(?:code|logic|control\s{1,4}flow|source|implementation|assembly)\b|"
    r"\boriginal\s{1,4}(?:code|logic|assembly|source|implementation|instructions?)\b|"
    r"\b(?:ported|transcribed|translated|converted)\s{1,4}(?:directly\s+|straight\s+)?from\s{1,4}(?:the\s+)?"
    r"(?:original|game'?s?\s+code|binary|executable|assembly|mips|decompil\w*|disassembl\w*|ghidra|ida)\b|"
    r"\bdecompil|\bdisassembl|\bpseudo-?code\b|\bghidra\b|\bida\s{1,4}pro\b|"
    r"\bConey'?s\s{1,4}implementation\b|\bour\s{1,4}(?:implementation|code)\b|"
    r"\bthe\s{1,4}(?:old|previous|legacy)\s{1,4}Coney\b",
    re.IGNORECASE,
)

# (id, what it rejects, test on a line or paragraph of cleaned text)
TEXT_RULES = [
    ("mips", "MIPS instructions or assembly", MIPS.search),
    (
        "instruction-words",
        "original instruction words (an address then a raw word, a hex or byte dump, a patch line)",
        instruction_words,
    ),
    (
        "decompiler-names",
        "decompiler or disassembler names and helpers (FUN_, param_N, puVar, auStack_, sub_, ZEXT)",
        lambda t: bool(DECOMPILER.search(t) or DECOMPILER_CS.search(t)),
    ),
    ("code-text", "code written as plain text (statements, blocks, assignments)", code_text),
    ("branch-shape", 'decision trees that mirror the original\'s branches ("else if", "failing that")', BRANCH.search),
    (
        "helper-boundary",
        'helper boundaries ("GetUp calls Recover", "is called by", callers and callees)',
        HELPER.search,
    ),
    ("loop-shape", "loop shapes or local variables", LOOP.search),
    (
        "banned-phrase",
        "wording that ties the text to the original's code, its tools, or old Coney's implementation",
        PHRASES.search,
    ),
]
STRUCT_RULES = [
    ("code-block", "code blocks: fenced (also in quotes and lists), indented, or HTML pre/code/samp/xmp"),
    ("inline-code", "code inside an inline code span (statements, assignments, calls with arguments, keywords)"),
    ("step-list", "numbered, lettered or sequenced steps in a section that names a game address"),
    ("image", "an image or embedded media without a valid capture record, or outside the page's folder"),
    ("entry-points", "an '## Entry points' table not in the form 'Address | Job | Outcomes' (see the README)"),
    ("look-alike", "a word mixing Latin letters with look-alike letters from another script"),
    ("empty", "the page holds no text"),
    ("too-large", f"the page is larger than {MAX_PAGE // 1_000_000} MB"),
    ("not-text", "the page is not UTF-8 text"),
]
RULES = [(rid, what) for rid, what, _ in TEXT_RULES] + STRUCT_RULES

# --- structure --------------------------------------------------------------------------------------------------

HEADING = re.compile(r"^ {0,3}(#{1,6})(?:[ \t]+(.*?))?[ \t]*#*[ \t]*$")
QUOTE_PREFIX = re.compile(r"^(?: {0,3}>[ \t]?){1,20}")
FENCE_ANY = re.compile(r"^\s{0,40}(`{3,}|~{3,})\s{0,4}([^\s`]{0,40})")  # cleaned text: anything fence-like
FENCE_RAW = re.compile(r"^ {0,3}(`{3,}|~{3,})[ \t]*([^\s`]{0,40})")  # a real fence, as written
LIST_ITEM = re.compile(r"^( {0,40})([-*+]|\d{1,3}[.)]|[a-zA-Z][.)])( {1,40})")
ORDERED_ITEM = re.compile(r"^\s{0,40}(?:\d{1,3}|[a-zA-Z]|[ivx]{1,4})[.)]\s")
STEP_BULLET = re.compile(
    r"^\s{0,40}[-*+]\s{1,4}(?:(?:then|next|finally|after\s+that|afterwards|first|second|third|"
    r"initially|lastly)\b|[^\n]{0,200}\bthen\b)",
    re.IGNORECASE,
)
HEX_LITERAL = re.compile(r"\b0x([0-9a-f]{5,8})\b", re.IGNORECASE)
# Game RAM (main memory from 0x00100000) and its kseg/uncached mirrors; small constants and floats are not addresses.
RAM_RANGES = [(0x00100000, 0x02000000), (0x20100000, 0x22000000), (0x30100000, 0x32000000), (0x80100000, 0x82000000)]


def names_game_address(text):
    if "@orig" in text:
        return True
    return any(lo <= int(m.group(1), 16) < hi for m in HEX_LITERAL.finditer(text) for lo, hi in RAM_RANGES)


SEQUENCE = re.compile(
    r"\b(?:first|initially)\b.{0,300}?\b(?:then|next|afterwards|after\s+that|second(?:ly)?)\b|"
    r"\bstep\s{0,4}\d{1,3}\b.{0,400}?\bstep\s{0,4}\d{1,3}\b|"
    r"\(\s{0,2}(?:1|a|i)\s{0,2}\).{0,300}?\(\s{0,2}(?:2|b|ii)\s{0,2}\)",
    re.IGNORECASE | re.DOTALL,
)
INLINE_CODE = re.compile(r"(`{1,10})(.{1,500}?)\1", re.DOTALL)
CODE_HTML = re.compile(
    r"<\s{0,4}(?:pre|code|script|samp|xmp|kbd|listing|plaintext|tt|var|textarea|template|style)\b", re.IGNORECASE
)
EMBED_HTML = re.compile(
    r"<\s{0,4}(?:picture|source|video|audio|embed|object|iframe|svg|canvas|math|input)\b|"
    r"\bsrcset\s{0,4}=|\burl\s{0,4}\(",
    re.IGNORECASE,
)
IMG_HTML = re.compile(r"<img\b[^>]{0,2000}?\bsrc\s{0,4}=\s{0,4}[\"']?([^\"'\s>]{1,2000})", re.IGNORECASE | re.DOTALL)
REF_DEF = re.compile(r"^ {0,3}\[([^\]]{1,500})\]:\s{0,8}<?([^\s>]{1,2000})>?")
SHELL_INFO = {"sh", "bash", "shell", "console", "zsh", "powershell", "pwsh", "ps1", "cmd", "bat", "batch"}
CAPTURE_KEYS = ("source", "captured", "by")
INLINE_ALLOWED = re.compile(r"^-{1,2}[\w.-]{1,60}(?:=\S{0,100})?$|^[\w.-]{1,60}=[\w./:,-]{0,100}$")
TYPE_ARG = re.compile(
    r"^(?:const\s+)?(?:[A-Z][a-z]\w{0,60}(?:::\w{1,60}){0,4}|u8|u16|u32|u64|s8|s16|s32|s64|f32|f64|"
    r"int|float|char|bool|void|short|long|unsigned(?:\s+\w+)?|size_t)\s{0,2}[*&]{0,3}\s{0,2}"
    r"(?:[a-z]\w{0,60})?$"
)
CALL = re.compile(r"\b[A-Za-z_]\w{0,60}\s{0,2}\(([^()]{0,300})\)")


def inline_is_code(span):
    s = span.strip()
    if not s or INLINE_ALLOWED.match(s):
        return False
    if re.search(r";|(?<![<>=!-])=(?![=>])|==|!=|<=|>=|\+=|-=|\*=|/=|:=|&&|\|\||\+\+|--\s*$|\u2190", s):
        return True
    if re.match(r"^(?:if|for|while|return|switch|case|goto|break|else|do)\b", s):
        return True
    for m in CALL.finditer(s):  # a call with arguments; a signature listing only types is a name
        args = [a.strip() for a in m.group(1).split(",") if a.strip()]
        if any(not TYPE_ARG.match(a) for a in args):
            return True
    return False


@dataclass
class Finding:
    rule: str
    line: int
    message: str


@dataclass
class Section:
    id: str
    heading: str
    line: int
    lines: list = field(default_factory=list)  # (line number, raw text); the heading line included
    findings: list = field(default_factory=list)
    images: list = field(default_factory=list)  # resolved paths of the images that passed, in page order

    @property
    def ok(self):
        return not self.findings

    def normalised(self):
        """The text verdict files hash: each line's trailing spaces and tabs removed, runs of blank lines collapsed
        to one, leading and trailing blank lines dropped, LF line endings, one final LF."""
        out = []
        for _, text in self.lines:
            text = text.rstrip(" \t")
            if text or (out and out[-1]):
                out.append(text)
        while out and not out[-1]:
            out.pop()
        return "\n".join(out) + "\n"

    def sha256(self):
        return hashlib.sha256(self.normalised().encode("utf-8")).hexdigest()


def slug(heading, seen):
    s = re.sub(r"[^a-z0-9]+", "-", clean(heading).lower()).strip("-")[:80] or "section"
    n, out = 1, s
    while out in seen:
        n += 1
        out = f"{s}-{n}"
    seen.add(out)
    return out


def split_sections(lines):
    """Sections start at ATX headings: a line of one to six '#' indented at most three spaces, then a space or tab
    and text (a bare '#' line is not a heading; setext headings do not split). Lines inside a fenced block (a line
    starting, after any '>' quote markers, with at least three backticks or tildes at any indentation, until a
    closing line of the same character at least as long) never start a section. Text before the first heading is the
    section 'top'."""
    sections, seen, fence = [Section("top", "", 1)], {"top"}, None
    for no, text in enumerate(lines, 1):
        body = QUOTE_PREFIX.sub("", text)
        m = FENCE_ANY.match(body)
        if fence:
            if m and m.group(1)[0] == fence[0] and len(m.group(1)) >= len(fence):
                fence = None
        elif m:
            fence = m.group(1)
        elif (h := HEADING.match(text)) and h.group(2):
            sections.append(Section(slug(h.group(2), seen), h.group(2), no))
        sections[-1].lines.append((no, text))
    if not any(t.strip() for _, t in sections[0].lines):
        sections.pop(0)
    return sections


def image_file(page_dir, target):
    """The file a local image reference names, resolved (a query or fragment dropped). Raises OSError or
    ValueError for a path that cannot be resolved."""
    return (page_dir / target.split("#")[0].split("?")[0]).resolve()


def capture_problem(page_dir, target):
    """None when the image is a local file inside the page's folder with a valid capture record, else the reason.
    Reasons never name the file."""
    if not target:
        return "image reference has no target"
    if re.match(r"^[a-z][a-z0-9+.-]{0,20}:", target, re.IGNORECASE) or target.startswith("//"):
        return "image is not a local file"
    if page_dir is None:
        return "image cannot be checked without the page's folder"
    try:
        image = image_file(page_dir, target)
    except (OSError, ValueError):
        return "image path is not valid"
    if not image.is_relative_to(page_dir.resolve()):
        return "image is outside the page's folder"
    if not image.is_file():
        return "image file is missing"
    record = image.with_name(image.name + ".capture.toml")
    if not record.is_file():
        return "image has no capture record next to it"
    try:
        data = tomllib.loads(record.read_text(encoding="utf-8"))
    except (OSError, UnicodeDecodeError, tomllib.TOMLDecodeError):
        return "capture record is not valid TOML"
    missing = [k for k in CAPTURE_KEYS if not isinstance(data.get(k), str) or not data[k].strip()]
    if missing:
        return "capture record lacks " + ", ".join(missing)
    digest = data.get("sha256", "")
    if not isinstance(digest, str) or digest.lower() not in ("", hashlib.sha256(image.read_bytes()).hexdigest()):
        return "capture record's sha256 does not match the image"
    return None


def image_targets(text, refs):
    """Targets of every Markdown image (inline, reference, collapsed and shortcut forms) and HTML img."""
    out, i = [], 0
    while (i := text.find("![", i)) != -1:
        j, depth = i + 2, 1
        while j < len(text) and depth and j < i + 2000:
            depth += {"[": 1, "]": -1}.get(text[j], 0)
            j += 1
        alt, rest = text[i + 2 : j - 1], text[j : j + 2100]
        if rest.startswith("("):
            m = re.match(r"\(\s{0,8}(<[^>\n]{0,2000}>|[^\s)]{1,2000})", rest)
            out.append(m.group(1).strip("<>").strip() if m else "")
        elif rest.startswith("["):
            k = rest.find("]")
            label = rest[1:k] if k > 0 else ""
            out.append(refs.get((label or alt).strip().lower(), ""))
        else:
            out.append(refs.get(alt.strip().lower(), ""))
        i = j
    out += [m.group(1) for m in IMG_HTML.finditer(text)]
    return out


def indent_of(text):
    t = text.expandtabs(4)
    return len(t) - len(t.lstrip(" "))


def check_section(section, page_dir, refs, guide):
    found = {}  # (rule, line) -> message; one finding per rule and line

    def add(rule, line, message):
        found.setdefault((rule, line), message)

    fence, fence_ok = None, False
    prev_kind = "blank"  # blank / heading / text / code
    list_col = None  # content column of the open list, when inside one
    ordered, step_bullets, names_address = [], 0, False
    paragraphs, para = [], []

    def end_paragraph():
        if para:
            paragraphs.append((para[0][0], " ".join(t.strip() for _, t in para)))
            para.clear()

    for no, raw in section.lines:
        body = QUOTE_PREFIX.sub("", raw)
        text = clean(body)
        fake = FENCE_ANY.match(text)
        real = FENCE_RAW.match(body)
        if fence:
            if real and real.group(1)[0] == fence[0] and len(real.group(1)) >= len(fence):
                fence = None
                prev_kind = "code"
                continue
            if fence_ok:  # a shell block in a guide: still no assembly, dumps, names, C-like code or banned wording
                for rid, what, test in TEXT_RULES:
                    if rid != "code-text" and (test(text) or test(rendered(text))):
                        add(rid, no, what)
                if SHELL_CODE.search(text):
                    add("code-text", no, "C-like code inside a shell block")
            continue
        if real:
            end_paragraph()
            fence = real.group(1)
            # Only a top-level shell fence is allowed in a guide: one opened inside a quote or list would end with
            # that container in CommonMark, while this check would keep reading the text after it as shell.
            fence_ok = guide and real.group(2).lower() in SHELL_INFO and body == raw and list_col is None
            if not fence_ok:
                add("code-block", no, "fenced code block" + (" (guides may use shell blocks only)" if guide else ""))
            prev_kind = "code"
            continue
        if fake:  # looks like a fence (indented, escaped or encoded) but is not one: flagged, and the following
            add("code-block", no, "fence-like line")  # lines stay ordinary text, checked as such
        if not text.strip():
            end_paragraph()
            prev_kind = "blank" if prev_kind != "code" else "code-blank"
            continue
        ind = indent_of(body)
        item = LIST_ITEM.match(body.expandtabs(4))
        if item and len(item.group(3)) <= 4:
            list_col = len(item.group(1)) + len(item.group(2)) + len(item.group(3))
        elif item:  # marker followed by five or more spaces: the item's content is indented code
            add("code-block", no, "indented code block in a list item")
            list_col = len(item.group(1)) + len(item.group(2)) + 1
        else:
            if list_col is not None and prev_kind in ("blank", "code-blank") and ind < list_col:
                list_col = None
            base = list_col if list_col is not None else 0
            if ind >= base + 4 and prev_kind in ("blank", "heading", "code", "code-blank"):
                add("code-block", no, "indented code block")
                end_paragraph()
                prev_kind = "code"
                continue
        if CODE_HTML.search(text):
            add("code-block", no, "HTML pre, code, samp or similar element")
        if EMBED_HTML.search(text):
            add(
                "image",
                no,
                "embedded media, an image source set or a CSS url() (use a Markdown image with a capture record)",
            )
        if mixed_script(raw_letters(body)):
            add("look-alike", no, "a word mixes Latin letters with look-alike letters from another script")
        for rid, what, test in TEXT_RULES:
            if test(text) or test(rendered(text)):
                add(rid, no, what)
        if names_game_address(text):
            names_address = True
        if ORDERED_ITEM.match(body):
            ordered.append(no)
        if STEP_BULLET.match(body):
            step_bullets += 1
        heading = no == section.line and section.heading
        if heading or body.lstrip().startswith("|"):  # a heading or a table row is its own unit: never joined
            end_paragraph()
            paragraphs.append((no, text))
        else:
            para.append((no, text))
        prev_kind = "heading" if heading else "text"
    end_paragraph()

    # Paragraph-level checks: text split across lines, inline code across lines, images (also multi-line HTML).
    line_hits = {rule for rule, _ in found}
    for start, ptext in paragraphs:
        for rid, what, test in TEXT_RULES:
            if rid not in line_hits and (test(ptext) or test(rendered(ptext))):
                add(rid, start, what + " (across lines)")
        for mm in INLINE_CODE.finditer(ptext):
            if inline_is_code(mm.group(2)):
                add("inline-code", start, "inline code span holds a statement, assignment or call")
                break
        for target in image_targets(ptext, refs):
            problem = capture_problem(page_dir, target)
            if problem:
                add("image", start, problem)
            else:
                section.images.append(image_file(page_dir, target))
        if names_address and SEQUENCE.search(ptext):
            add(
                "step-list",
                start,
                'sequence wording ("first ... then", "step 1 ... step 2", "(1) ... (2)") in a '
                "section that names a game address",
            )
    if names_address:
        if len(ordered) >= 2:
            add(
                "step-list",
                ordered[0],
                f"numbered or lettered list of {len(ordered)} items in a section that names a "
                "game address (write facts as a table)",
            )
        if step_bullets:
            add(
                "step-list",
                section.line,
                'bullets that read as steps ("first", "then", "next") in a section that names a game address',
            )
    section.findings = sorted((Finding(r, ln, msg) for (r, ln), msg in found.items()), key=lambda f: (f.line, f.rule))


def page_images(sections, page_dir):
    """Every image a checked page references, once each, as POSIX paths relative to the page's folder, sorted.
    Only images that passed are listed: a page with a failing image fails as a whole."""
    base = Path(page_dir).resolve()
    return sorted({p.relative_to(base).as_posix() for s in sections for p in s.images})


# The entry-point table the code gate reads @orig addresses from (decided 2026-10-16): one '## Entry points' section
# per page holding a table with exactly these columns; each row an address in inline code (each address once), the job
# in plain words, and the heading of the section on the same page that states the job's outcomes. The job is page text
# like any other, so every text rule applies to it. Only these tables are checked; other tables are ordinary text.
ENTRY_HEADING = "entry points"
ENTRY_COLUMNS = ["address", "job", "outcomes"]
ENTRY_ADDRESS = re.compile(r"^`0x[0-9a-f]{8}`$")
TABLE_RULE = re.compile(r"^:?-{3,}:?$")


def table_cells(text):
    return [c.strip() for c in text.strip().removeprefix("|").removesuffix("|").split("|")]


def check_entry_points(sections):
    """Findings on every '## Entry points' section whose table is not in the agreed form. Messages name the rule and
    line, never the cell's text."""
    headings = {clean(s.heading).strip().lower() for s in sections if s.heading}
    found = [s for s in sections if clean(s.heading).strip().lower() == ENTRY_HEADING]
    for k, s in enumerate(found):

        def add(line, message, s=s):
            s.findings.append(Finding("entry-points", line, message))

        if k:
            add(s.line, "a page has one '## Entry points' section")
        if not re.match(r"^ {0,3}## ", s.lines[0][1]):
            add(s.line, "the entry-point heading is a level-2 heading ('## Entry points')")
        rows = [(no, text) for no, text in s.lines[1:] if text.strip().startswith("|")]
        if len(rows) < 3:
            add(s.line, "the section holds no 'Address | Job | Outcomes' table with at least one row")
            continue
        if [c.lower() for c in table_cells(rows[0][1])] != ENTRY_COLUMNS:
            add(rows[0][0], "the table's columns are exactly 'Address | Job | Outcomes'")
        if not all(TABLE_RULE.match(c) for c in table_cells(rows[1][1])):
            add(rows[1][0], "the table's second row is its rule row ('| --- | --- | --- |')")
        seen = set()
        for no, text in rows[2:]:
            cells = table_cells(text)
            if len(cells) != 3:
                add(no, "a row has exactly three cells")
                continue
            if not ENTRY_ADDRESS.match(cells[0]):
                add(no, "the address is 0x and 8 lower-case hex digits in inline code")
            elif cells[0] in seen:
                add(no, "an address is listed once")
            seen.add(cells[0])
            if not clean(cells[1]).strip():
                add(no, "the job is empty")
            outcome = clean(cells[2]).strip().lower()
            if outcome == ENTRY_HEADING or outcome not in headings:
                add(no, "the outcomes cell is the heading of another section of this page")
        s.findings.sort(key=lambda f: (f.line, f.rule))


def normal_job(text):
    """A job as the code gate compares it: cleaned, whitespace runs as one space, case folded."""
    return " ".join(clean(text).split()).casefold()


def entry_points(sections):
    """{address: job} from a passing page's '## Entry points' table ({} when it has none): the addresses the code
    gate lets a Coney function name, each with its job as the code gate compares it (normal_job)."""
    if not all(s.ok for s in sections):
        raise ValueError("entry points are read only from a page that passes")
    out = {}
    for s in sections:
        if clean(s.heading).strip().lower() == ENTRY_HEADING:
            for _, text in [(no, t) for no, t in s.lines[1:] if t.strip().startswith("|")][2:]:
                cells = table_cells(text)
                out[cells[0].strip("`")] = normal_job(cells[1])
    return out


def check_text(text, page_dir=None, guide=False):
    """Check one page's text. page_dir resolves image paths (None: images cannot be checked). guide=True allows
    fenced shell blocks (how-to guides); everything else is checked the same."""
    lines = text.replace("\r\n", "\n").replace("\r", "\n").removeprefix("\ufeff").split("\n")
    if lines and lines[-1] == "":
        lines.pop()
    if not any(clean(l).strip() for l in lines):
        return [Section("top", "", 1, findings=[Finding("empty", 1, "the page holds no text")])]
    refs = {m.group(1).strip().lower(): m.group(2) for l in lines if (m := REF_DEF.match(clean(l)))}
    sections = split_sections(lines)
    for s in sections:
        check_section(s, page_dir, refs, guide)
    check_entry_points(sections)
    for n, s in enumerate(sections, 1):
        if not s.ok:  # a failing section's heading is withheld: its id and heading would show page text
            s.id, s.heading = f"withheld-{n}", ""
    return sections


def check_bytes(data, page_dir=None, guide=False):
    """Check a page given as bytes: too large or not UTF-8 text is one failing section."""
    if len(data) > MAX_PAGE:
        return [Section("top", "", 1, findings=[Finding("too-large", 1, f"larger than {MAX_PAGE // 1_000_000} MB")])]
    try:
        text = data.decode("utf-8")
    except UnicodeDecodeError:
        return [Section("top", "", 1, findings=[Finding("not-text", 1, "not UTF-8 text")])]
    if "\x00" in text:
        return [Section("top", "", 1, findings=[Finding("not-text", 1, "contains NUL bytes")])]
    return check_text(text, page_dir, guide)


def check_file(path, guide=False):
    p = Path(path)
    if os.stat(p).st_size > MAX_PAGE:  # refused unread
        return [Section("top", "", 1, findings=[Finding("too-large", 1, f"larger than {MAX_PAGE // 1_000_000} MB")])]
    return check_bytes(p.read_bytes(), p.resolve().parent, guide)


GUIDES_DIR = ("docs", "guides")  # relative to the repo root


def is_guide_path(path):
    """Guide mode applies only to pages under docs/guides/ of the git checkout the page is in."""
    p = Path(path).resolve()
    for root in p.parents:
        if (root / ".git").exists():
            rel = [part.lower() for part in p.relative_to(root).parts]
            return len(rel) > len(GUIDES_DIR) and tuple(rel[: len(GUIDES_DIR)]) == GUIDES_DIR
    return False


# The rules that only the original's own material sets off, as opposed to code in general: a session that must never
# see the original refuses pasted text that breaks one of them (paste-check). Scripts, configs and logs pass.
PASTE_RULES = ("mips", "instruction-words", "decompiler-names")


def paste_findings(data):
    """Findings of PASTE_RULES in pasted text (bytes), in line order. Text that is not UTF-8, or too large, is one
    'not-text' or 'too-large' finding: it cannot be checked, so it does not pass."""
    out = []
    for s in check_bytes(data):
        out += [f for f in s.findings if f.rule in PASTE_RULES + ("not-text", "too-large")]
    return sorted(out, key=lambda f: f.line)


def checker_sha256():
    """Which version of the rules passed a page, recorded by verdict files and bundles. Hashed with LF line endings,
    so a Windows and a Linux checkout of the same commit agree."""
    return hashlib.sha256(Path(__file__).read_bytes().replace(b"\r\n", b"\n")).hexdigest()
