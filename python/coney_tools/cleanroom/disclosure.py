"""`coney-tools cleanroom disclosure-check`: public text describes the process in neutral words.

Every pull request body and commit message is checked before it is pushed (playbook section 17; AGENTS.md, "Commits
and pull requests"). Public text says what Coney does; it never reports an incident, puts a date on a problem, or
says what was removed. The rules below are the one copy of what counts as such wording. They aim at wording about
something that happened, not at the process words the clean-room page uses for what Coney does in general (the
breach watch, the contamination response, an incident record), nor at ordinary engineering words ("remove an unused
option", "fix a memory leak").

Each rule reads a whole paragraph as one text, so wording split across lines is caught, as a reader sees it:
entities, escapes, invisible characters, markup and look-alike letters handled as page-check does. Findings
name the rule and the line, never the matched text. Every pattern is bounded.
"""

import bisect
import re
import subprocess
from dataclasses import dataclass

from coney_tools.cleanroom.page_check import clean, rendered

MAX_TEXT = 1_000_000  # bytes; larger text is refused unread
I = re.IGNORECASE

# Process nouns that make an incident word a name for part of the process ("the breach watch"), not a report.
PROCESS = (
    r"(?:watch(?:es)?|responses?|notifications?|records?|tool(?:s|ing)?|checks?|drills?|tests?|procedures?|process"
    r"|plans?|playbooks?|handling|scans?|scanners?|guides?|forms?|templates?|ledgers?|fields?|steps?)"
)
# A memory, handle or resource leak is an engineering word, not an incident.
NOUN = (
    r"(?:breach(?:es)?|(?<!memory\s)(?<!handle\s)(?<!resource\s)leak(?:s|age)?|incidents?|contamination|exposures?"
    r"|compromises?|violations?)"
)
NOT_PROCESS = rf"(?!-?\s{{0,3}}{PROCESS}\b)"

INCIDENT = [
    # "the leak was found", "a breach occurred"
    re.compile(
        rf"\b{NOUN}\b{NOT_PROCESS}\s{{1,3}}(?:(?:was|were)|(?:has|had|have)\s{{1,3}}been)\s{{1,3}}"
        r"(?:found|discovered|detected|reported|confirmed|caused|introduced|fixed|resolved|contained|cleaned)\b",
        I,
    ),
    re.compile(rf"\b{NOUN}\b{NOT_PROCESS}\s{{1,3}}(?:occurred|happened|took\s{{1,3}}place)\b", I),
    # "the pages were contaminated", "it got leaked"
    re.compile(
        r"\b(?:was|were|been|got|became)\s{1,3}(?:\w{1,30}\s{1,3})?(?:leaked|breached|contaminated|compromised|tainted)\b",
        I,
    ),
    # "since the leak", "after our breach"; "after the breach is contained" describes the process
    re.compile(
        r"\b(?:after|since|following|before|during|due\s{1,3}to|because\s{1,3}of|in\s{1,3}response\s{1,3}to"
        r"|caused\s{1,3}by|fallout\s{1,3}(?:of|from))\s{1,3}"
        r"(?:the|that|this|our|last|recent|earlier|previous|past|yesterday's)\s{1,3}(?:\w{1,30}\s{1,3}){0,2}?"
        rf"{NOUN}\b{NOT_PROCESS}(?!\s{{1,3}}(?:is|are|gets|get|has|have)\b)",
        I,
    ),
    re.compile(rf"\bclean[- ]?room\s{{1,3}}(?:breach|violation|failure|leak|incident)s?\b{NOT_PROCESS}", I),
    re.compile(r"\b(?:violated|broke|broken|breached|compromised)\s{1,3}(?:the\s{1,3}|our\s{1,3})?clean[- ]?room\b", I),
    re.compile(r"\b(?:tainted|post-?mortems?)\b", I),
]

SLIP_WORD = re.compile(
    r"\b(?:accidental(?:ly)?|mistaken(?:ly)?|inadvertent(?:ly)?|unintentional(?:ly)?|erroneously"
    r"|by\s{1,3}(?:mistake|accident)|in\s{1,3}error)\b",
    I,
)
SLIP_VERB = re.compile(
    r"\b(?:commit|push|publish|includ|cop(?:y|ie)|past|upload|merg|leak|expos|shar|add|check(?:ed)?\s{1,3}in)\w{0,6}",
    I,
)
SLIP = [
    re.compile(r"\bslip(?:ped|s)?\s{1,3}(?:through|in|into|past|out)\b", I),
    re.compile(
        r"\bshould\s{1,3}(?:never|not)\s{1,3}have\s{1,3}been\s{1,3}"
        r"(?:committed|pushed|published|included|copied|pasted|uploaded|merged|shared|added)\b",
        I,
    ),
]

TAINTED_OBJECT = re.compile(
    r"\b(?:decompil\w{0,10}|disassembl\w{0,10}|original's\s{1,3}(?:code|files?|data|text|assets?)"
    r"|game\s{1,3}(?:code|data|files?)|ghidra\s{1,3}output|leaked|infringing|copyrighted|contaminated)",
    I,
)
REMOVAL_VERB = re.compile(
    r"\b(?:removed|deleted|dropped|stripped|reverted|purged|wiped|erased|cleaned\s{1,3}(?:out|up)"
    r"|(?:cut|took|taken)\s{1,3}out)\b",
    I,
)
REMOVAL = [
    re.compile(r"\bremnants?\b|\bleftovers?\s{1,3}(?:of|from)\b", I),
    re.compile(r"\b(?:scrub(?:bed|bing|s)?|purg(?:e|ed|es|ing)|expung\w{0,6})\b", I),
    re.compile(
        r"\b(?:rewr(?:ite|ote|itten|iting)|scrubbed|cleaned|purged)\s{1,3}(?:the\s{1,3})?"
        r"(?:(?:git|repo|repository|commit)\s{1,3})?history\b",
        I,
    ),
    re.compile(r"\bhistory\s{1,3}rewrites?\b|\bfilter-(?:repo|branch)\b|\bBFG\b|\bforce[- ]?pushed\b", I),
    re.compile(r"\btake-?downs?\b|\btaken\s{1,3}down\b|\bDMCA\b|\bcease[- ]and[- ]desist\b", I),
]

OLD_REPO = [
    re.compile(
        r"\b(?:old|previous|former|earlier|legacy|original)\s{1,3}(?:coney\s{1,3})?"
        r"(?:repos?|repositor(?:y|ies)|codebase|code\s{1,3}base|(?:git\s{1,3}|commit\s{1,3})history|forks?)\b",
        I,
    ),
    re.compile(r"\b(?:old|previous|former)\s{1,3}(?:version\s{1,3}of\s{1,3})?coney\b", I),
]

MONTH = (
    r"(?:jan(?:uary)?|feb(?:ruary)?|mar(?:ch)?|apr(?:il)?|may|june?|july?|aug(?:ust)?|sep(?:t(?:ember)?)?"
    r"|oct(?:ober)?|nov(?:ember)?|dec(?:ember)?)"
)
DAY = r"(?:monday|tuesday|wednesday|thursday|friday|saturday|sunday)"
DATE = re.compile(
    r"\b(?:19|20)\d\d-[01]\d-[0-3]\d\b"
    rf"|\b\d{{1,2}}(?:st|nd|rd|th)?\s{{1,3}}(?:of\s{{1,3}})?{MONTH}\b"
    rf"|\b{MONTH}\.?\s{{1,3}}(?:\d{{1,2}}(?:st|nd|rd|th)?\b|(?:of\s{{1,3}})?(?:19|20)\d\d\b)"
    rf"|\b(?:yesterday|last\s{{1,3}}(?:night|week|month|year|{DAY})|earlier\s{{1,3}}(?:today|this\s{{1,3}}"
    rf"(?:week|month|year))|this\s{{1,3}}morning|on\s{{1,3}}{DAY})\b",
    I,
)
PROBLEM = re.compile(
    r"\b(?:breach\w{0,3}|leak\w{0,3}|incidents?|contaminat\w{0,5}|expos(?:ed|ure)|compromis\w{0,3}|taint\w{0,3}"
    r"|violat\w{0,5}|mistakes?|mistaken(?:ly)?|accident\w{0,4}|problems?|removed|deleted|purged|scrubbed|wiped"
    r"|reverted|taken\s{1,3}down|take-?downs?|rewrote|rewritten)\b",
    I,
)
SENTENCE = re.compile(r"[^.!?;]{1,4000}(?:[.!?;]+|$)")


# Each test yields the offsets in the text where its wording starts.


def any_of(patterns):
    return lambda text: (m.start() for p in patterns for m in p.finditer(text))


def in_one_sentence(first, second):
    """Where `first` starts, in each sentence that holds both patterns."""

    def test(text):
        for s in SENTENCE.finditer(text):
            if (m := first.search(s.group(0))) and second.search(s.group(0)):
                yield s.start() + m.start()

    return test


def either(*tests):
    return lambda text: (pos for t in tests for pos in t(text))


# The one copy of the rules: id, what it refuses, test.
RULES = [
    ("incident", "reports an incident: a breach, leak, exposure or contamination that happened", any_of(INCIDENT)),
    (
        "slip",
        "says something was committed, pushed, published or copied by mistake",
        either(any_of(SLIP), in_one_sentence(SLIP_WORD, SLIP_VERB)),
    ),
    (
        "removal",
        "says what was removed: remnants, scrubbing, purges, history rewrites, takedowns",
        either(any_of(REMOVAL), in_one_sentence(REMOVAL_VERB, TAINTED_OBJECT)),
    ),
    ("old-repo", "points at an old or previous repository, its history or its forks", any_of(OLD_REPO)),
    (
        "problem-date",
        "puts a date or time on a problem: a date in a sentence about one",
        in_one_sentence(DATE, PROBLEM),
    ),
]


@dataclass
class Finding:
    rule: str
    line: int
    message: str


def paragraphs(lines):
    """Runs of non-blank lines, each as [(line number, text)]."""
    run = []
    for no, text in enumerate(lines, 1):
        if text.strip():
            run.append((no, text))
        elif run:
            yield run
            run = []
    if run:
        yield run


def is_scissors(line):
    return line.startswith("#") and ">8" in line and line.strip("# ->8") == ""


def check_text(text, commit_msg=False):
    """Findings in text, at most one per rule and line, in line order. With commit_msg, git's comment lines and
    everything from the scissors line on are left out, as git leaves them out of the commit."""
    lines = text.splitlines()
    if commit_msg:
        cut = next((i for i, s in enumerate(lines) if is_scissors(s)), len(lines))
        lines = ["" if s.startswith("#") else s for s in lines[:cut]]
    found = {}
    for para in paragraphs(lines):
        numbers = [no for no, _ in para]
        cleaned = [clean(raw).strip() for _, raw in para]
        # A paragraph is read as one text, as written and as rendered, so wording split across lines is one sentence.
        for parts in (cleaned, [rendered(c).strip() for c in cleaned]):
            starts, at = [], 0
            for part in parts:
                starts.append(at)
                at += len(part) + 1
            joined = " ".join(parts)
            for rid, what, test in RULES:
                for pos in test(joined):
                    found.setdefault((numbers[bisect.bisect_right(starts, pos) - 1], rid), what)
    return [Finding(rid, no, what) for (no, rid), what in sorted(found.items())]


def check_bytes(data, commit_msg=False):
    """Text given as bytes: too large or not UTF-8 text is one finding, since it cannot be checked."""
    if len(data) > MAX_TEXT:
        return [Finding("too-large", 1, f"larger than {MAX_TEXT // 1_000_000} MB")]
    try:
        text = data.decode("utf-8-sig")
    except UnicodeDecodeError:
        return [Finding("not-text", 1, "not UTF-8 text")]
    if "\x00" in text:
        return [Finding("not-text", 1, "contains NUL bytes")]
    return check_text(text, commit_msg)


def commit_messages(rev_range, git="git"):
    """(commit id, message bytes) for every commit in a git revision range, newest first. Raises OSError,
    subprocess.CalledProcessError or subprocess.TimeoutExpired when git cannot list them."""
    out = subprocess.run(
        [git, "-c", "log.showSignature=false", "log", "-z", "--format=%H%n%B", rev_range, "--"],
        capture_output=True,
        check=True,
        timeout=120,
    ).stdout
    result = []
    for record in out.split(b"\0"):
        record = record.lstrip(b"\n")
        if record:
            sha, _, body = record.partition(b"\n")
            result.append((sha.decode("ascii"), body))
    return result
