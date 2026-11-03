"""The settings page as the clock embeds it (spec 036): web/index.html minified.

    python scripts/minify_page.py [web/index.html]   # prints the sizes
    python scripts/minify_page.py --out <file>       # writes the minified page (tests/web)

Used by scripts/build_options.py (firmware) and emulator/gen_headers.py (emulator), so both
embed the same page. web/index.html stays the readable source, and the page's own tests
(tests/web) read it.

- The script: terser, at the pinned version, compressing and renaming local names only;
  top-level names stay, as the markup's onclick handlers call them.
- The style: comments, and the spaces around { } : ; , out.
- The markup: indentation, blank lines and HTML comments out. Line breaks stay, as they
  are the spaces between inline elements.

terser comes from PATH (the emulator's image installs it) or through npx. Node is required:
a build without it stops instead of embedding a bigger page than the size guard measured.
"""
import re
import shutil
import subprocess
import sys

TERSER = "terser@5.36.0"


def terser(js):
    exe = shutil.which("terser")
    cmd = [exe] if exe else [shutil.which("npx") or shutil.which("npx.cmd") or "npx", "--yes", TERSER]
    if not exe and not (shutil.which("npx") or shutil.which("npx.cmd")):
        sys.exit("minify_page: Node is needed to build the settings page (npx " + TERSER + "); see CLAUDE.md")
    r = subprocess.run(cmd + ["--compress", "--mangle", "--ecma", "2020"], input=js.encode("utf-8"),
                       capture_output=True, timeout=300)
    if r.returncode or not r.stdout:
        sys.exit("minify_page: terser failed: " + r.stderr.decode("utf-8", "replace")[-2000:])
    return r.stdout.decode("utf-8").strip()


def css(text):
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    text = re.sub(r"\s+", " ", text)
    text = re.sub(r"\s*([{};,>])\s*", r"\1", text)   # not ':', which selectors need a space before
    text = re.sub(r":\s+", ":", text)
    return text.replace(";}", "}").strip()


def markup(text):
    text = re.sub(r"<!--.*?-->", "", text, flags=re.S)
    text = re.sub(r"[ \t]*\r?\n[ \t]*", "\n", text)
    return re.sub(r"\n+", "\n", text)


def minify(html):
    html = html.replace("\r\n", "\n")
    parts, pos = [], 0
    for m in re.finditer(r"<(script|style)>(.*?)</\1>", html, flags=re.S):
        parts.append(markup(html[pos:m.start()]))
        tag, body = m.group(1), m.group(2)
        parts.append("<%s>%s</%s>" % (tag, terser(body) if tag == "script" else css(body), tag))
        pos = m.end()
    parts.append(markup(html[pos:]))
    return "".join(parts).strip() + "\n"


def zopfli_module():
    try:
        import zopfli.gzip
        import zopfli.zlib
        return zopfli
    except ImportError:
        sys.exit("minify_page: the zopfli package is needed (pip install zopfli==0.4.3) in the Python "
                 "that runs the build; see CLAUDE.md")


def gzip_page(data):
    """The page gzipped as the clock serves it: zopfli, smaller than zlib's level 9 (spec 036)."""
    return zopfli_module().gzip.compress(data, numiterations=15)


def deflate(data):
    """Raw deflate by zopfli, for the update image (spec 036): standard deflate, which the
    bootloader's uzlib unpacks like zlib's (tests/eboot_inflate.c checks every build)."""
    return zopfli_module().zlib.compress(data, numiterations=15)[2:-4]   # no zlib header or adler32


if __name__ == "__main__":
    import gzip
    import pathlib
    args = sys.argv[1:]
    target = args[args.index("--out") + 1] if "--out" in args else None
    args = [a for a in args if a != "--out" and a != target]
    src = pathlib.Path(args[0] if args else pathlib.Path(__file__).parents[1] / "web" / "index.html")
    raw = src.read_bytes()
    out = minify(raw.decode("utf-8")).encode("utf-8")
    if target:
        pathlib.Path(target).write_bytes(out)
        sys.exit(0)
    print("%s: %d -> %d bytes; gzipped %d -> %d, with zopfli %d" % (src.name, len(raw), len(out),
          len(gzip.compress(raw, 9)), len(gzip.compress(out, 9)), len(gzip_page(out))))
