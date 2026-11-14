#!/usr/bin/env python3
"""
Joins the lines of each paragraph, list item and quote of a Markdown file into one
line, and prints the result.

GitHub shows a .md file in the repository as a document, where a line break inside
a paragraph is only a space. Release notes are shown like comments, where every
line break stays a break, so a CHANGELOG wrapped at 80 columns comes out as short,
ragged lines. The release workflow passes its notes through this before it
publishes them; CHANGELOG.md itself stays wrapped.

Left as they are: blank lines, headings, tables, HTML lines, rules and fenced code.
A list item or a quote takes its own continuation lines but is never merged with
the next item, and a paragraph inside a list item keeps its indentation. A line
that ends in a hard break (two spaces or a backslash) is not joined to the next.
Windows line endings become plain ones.

Usage: unwrap_markdown.py [file]    (standard input without a file, or with -)
"""

import re
import sys

FENCE = re.compile(r"^\s*(```|~~~)")
# Headings, table rows, HTML and rules: one line each, never joined to or with.
BLOCK = re.compile(r"^\s*(#{1,6}(\s|$)|\||<|([-*_])(\s*\3){2,}\s*$)")
LIST_ITEM = re.compile(r"^\s*([-*+]|\d{1,9}[.)])(\s|$)")
QUOTE = re.compile(r"^(\s*(?:>\s?)+)(.*)$")


def hard_break(line):
    return line.endswith("  ") or line.endswith("\\")


def unwrap(lines):
    out = []
    joinable = False  # whether the last line written takes a continuation
    quoted = False    # whether the last line written is part of a quote
    fence = None
    for raw in lines:
        line = raw.rstrip("\r\n")
        if fence:
            out.append(line)
            if line.strip().startswith(fence):
                fence = None
            continue
        m = FENCE.match(line)
        if m:
            fence = m.group(1)
            out.append(line)
            joinable = False
            continue
        if not line.strip():
            out.append("")
            joinable = False
            continue

        q = QUOTE.match(line)
        text = q.group(2) if q else line
        if q and not text.strip():
            # A bare ">": a blank line inside the quote.
            out.append(line.rstrip())
            joinable = False
            continue
        if BLOCK.match(text):
            out.append(line)
            joinable = False
            continue
        # A ">" after a paragraph starts a quote. A line without ">" after a quote
        # continues it, as Markdown reads it the same way.
        if joinable and not LIST_ITEM.match(text) and not (q and not quoted):
            out[-1] = out[-1].rstrip() + " " + text.lstrip()
        else:
            out.append(line)
            quoted = bool(q)
        joinable = not hard_break(line)
    return out


def main():
    if len(sys.argv) > 2:
        print(__doc__, file=sys.stderr)
        return 2
    name = sys.argv[1] if len(sys.argv) == 2 else "-"
    if name == "-":
        text = sys.stdin.read()
    else:
        with open(name, encoding="utf-8", newline="") as f:
            text = f.read()
    result = "\n".join(unwrap(text.splitlines()))
    sys.stdout.write(result + ("\n" if text.endswith(("\n", "\r")) else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main())
