"""Runs coney-tools from a checkout without installing it: `py -3 -I python/coney-tools.py <command> ...`.
Read-only checkouts (the other zones, hooks, CI) use this; nothing is written next to the code."""

import sys
from pathlib import Path

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parent))

from coney_tools.cli import main

if __name__ == "__main__":  # scan's worker processes load this file too, and must not run the command
    sys.exit(main())
