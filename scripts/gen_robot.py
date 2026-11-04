"""Moni, the project's robot (spec 039): a little robot whose head is a screen. Its parts go
into the Message face's template in src/faces.cpp, and its picture is the logo.

    python scripts/gen_robot.py             # rewrites the template's robot items
    python scripts/gen_robot.py --logo      # also docs/images/robot.png, robot.gif and robot-sheet.png

Colours: k outline, w shell, s shell shade, g steel, b blue joints, e screen, h the
screen's glint, a the accent (the message's colour on the clock).
"""
import json
import sys
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
W, H = 29, 31
# k outline, w shell, s shell shade, g steel, d screen bezel, e screen, a accent (the
# message's colour: antenna, face, chest light)
PAL = {"k": "#141a26", "w": "#eef2f7", "s": "#aab6c6", "g": "#7d8ca3", "b": "#3b82f6", "e": "#0f2a40", "h": "#2c5578", "a": "#ffd60a"}

FACES = {   # 12 x 5, on the screen
    "idle":  ["............", "..aa....aa..", "..aa....aa..", "............", "....aaaa...."],
    "blink": ["............", "............", "..aa....aa..", "............", "....aaaa...."],
    "ask":   ["............", "..aa....aa..", "..aa....aa..", "............", ".....aa....."],
    "look":  ["............", "...aa....aa.", "...aa....aa.", "............", ".....aaaa..."],
    "happy": ["...a....a...", "..a.a..a.a..", "............", "..a......a..", "...aaaaaa..."],
    "dead":  [".a.a....a.a.", "..a......a..", ".a.a....a.a.", "............", "....aaaa...."],
}

def blank():
    return [["."] * W for _ in range(H)]

def put(c, x, y, s):
    x += 2   # two columns of margin on each side, for the waving arm
    for j, ch in enumerate(s):
        if ch != " " and 0 <= x + j < W and 0 <= y < H:
            c[y][x + j] = ch

def head(c, y, face, antenna="a"):
    put(c, 11, y, "aa"); put(c, 11, y + 1, "aa"); put(c, 11, y + 2, "gg")
    put(c, 4, y + 3, "kkkkkkkkkkkkkkkk")
    put(c, 3, y + 4, "kwwwwwwwwwwwwwwssk")
    put(c, 3, y + 5, "kwkkkkkkkkkkkkkksk")
    for i, line in enumerate(FACES[face]):
        put(c, 3, y + 6 + i, "kwk" + line.replace(".", "e") + "ksk")
    put(c, 6, y + 6, "hh"); put(c, 6, y + 7, "h")      # a glint on the screen
    put(c, 3, y + 11, "kwkkkkkkkkkkkkkksk")
    put(c, 3, y + 12, "kssssssssssssssssk")
    put(c, 4, y + 13, "kkkkkkkkkkkkkkkk")
    for dy in range(7, 10):          # ears
        put(c, 1, y + dy, "kb"); put(c, 21, y + dy, "bk")

def body(c, y, arms):
    put(c, 9, y, "kggggk")                      # neck
    put(c, 6, y + 1, "kkkkkkkkkkkk")
    put(c, 6, y + 2, "kwswwwwwswsk")
    put(c, 6, y + 3, "kwkkkkkkkwsk")
    put(c, 6, y + 4, "kwkeaaeekwsk")
    put(c, 6, y + 5, "kwkeaaeekwsk")
    put(c, 6, y + 6, "kwkkkkkkkwsk")
    put(c, 6, y + 7, "kwwbwawbwwsk")                 # status LEDs
    put(c, 6, y + 8, "kggggggggggk")                 # belt
    put(c, 6, y + 9, "kkkkkkkkkkkk")
    put(c, 7, y + 10, "kbbk  kbbk")                  # hips
    put(c, 7, y + 11, "kgsk  kgsk")
    put(c, 6, y + 12, "kwwsk  kwwsk")                # boots
    put(c, 6, y + 13, "kkkkk  kkkkk")
    left, right = arms
    arm(c, y, left, side=0); arm(c, y, right, side=1)

def hand(c, x, y, down):
    rows = ["kssk", "kbbk", " kk "] if down else [" kk ", "kbbk", "kssk"]
    for i, r in enumerate(rows):
        put(c, x, y + i, r)

def arm(c, y, pose, side):
    # 4 wide: outline, steel, highlight, outline. Shoulder joint at body row 2.
    sh = 2 if side == 0 else 18
    put(c, sh, y + 2, "kbbk")
    if pose == "down":
        for r in range(3, 9):     # as long as a raised arm: 6 cells and the hand
            put(c, sh, y + r, "kgsk")
        hand(c, sh, y + 9, True)
    elif pose in ("up", "tilt"):   # elbow at the shoulder, forearm up beside the head
        x = -1 if side == 0 else 21
        put(c, x, y + 1, "kbbk")
        for r in range(-4, 1):
            put(c, x, y + r, "kgsk")
        if pose == "tilt":     # the forearm leans out: its top rows and the hand, one cell
            o = -1 if side == 0 else 1
            for r in range(-4, -1):
                put(c, x, y + r, "...."); put(c, x + o, y + r, "kgsk")
            hand(c, x + o, y - 7, False)
        else:
            hand(c, x, y - 7, False)

def frame(face="idle", arms=("down", "down"), lift=0, slump=0, antenna="a"):
    c = blank()
    top = 2 - lift
    head(c, top + slump, face, antenna)
    body(c, top + 14, arms)
    return ["".join(r).rstrip(".") for r in c]

FRAMES = {
    "idle":   frame(),
    "blink":  frame("blink"),
    "waveA":  frame("ask", ("down", "up")),
    "waveB":  frame("ask", ("down", "tilt")),
    "cheer":  frame("happy", ("up", "up")),
    "hop":    frame("happy", ("up", "up"), lift=2),
    "error":  frame("dead", ("down", "down"), slump=1),
}

def rgb(h):
    return tuple(int(h[i:i + 2], 16) for i in (1, 3, 5))

def image(rows, accent, scale, bg):
    pal = dict(PAL, a=accent)
    img = Image.new("RGB", (W * scale, H * scale), rgb(bg))
    for y, row in enumerate(rows):
        for x, ch in enumerate(row):
            if ch in pal:
                img.paste(rgb(pal[ch]), (x * scale, y * scale, (x + 1) * scale, (y + 1) * scale))
    return img

# ---- Parts for the Message face: each a sprite of its own, so only what changes redraws.
def part(draw):
    c = blank(); draw(c); return c

def crop(canvases):
    cells = [(x, y) for c in canvases for y, r in enumerate(c) for x, ch in enumerate(r) if ch != "."]
    x0, y0 = min(x for x, _ in cells), min(y for _, y in cells)
    x1 = max(x for x, _ in cells)
    y1 = max(y for _, y in cells)
    frames = [["".join(r[x0:x1 + 1]).rstrip(".") for r in c[y0:y1 + 1]] for c in canvases]
    return x0, y0, frames

TOP = 2
def head_shell(c):
    head(c, TOP, "idle")
    for i in range(5):   # the screen without a face
        put(c, 3, TOP + 6 + i, "kwk" + "e" * 12 + "ksk")
    put(c, 6, TOP + 6, "hh"); put(c, 6, TOP + 7, "h")

def face_part(name):
    def d(c):
        for i, line in enumerate(FACES[name]):
            put(c, 6, TOP + 6 + i, line.replace(".", "e"))
        if name != "dead":
            put(c, 6, TOP + 6, "hh" if FACES[name][0][:2] == ".." else ""); put(c, 6, TOP + 7, "h" if FACES[name][1][0] == "." else "")
    return d

def body_part(c):
    b = blank(); body(b, TOP + 14, ("none", "none"))
    for y in range(H):
        c[y] = b[y]

def arm_part(pose, side):
    def d(c):
        arm(c, TOP + 14, pose, side)
    return d

def parts():
    out = {}
    out["head"] = crop([part(head_shell)])
    out["face"] = crop([part(face_part(n)) for n in ("ask", "happy", "dead", "blink")])
    out["body"] = crop([part(body_part)])
    out["left"] = crop([part(arm_part(p, 0)) for p in ("down", "up")])
    out["right"] = crop([part(arm_part(p, 1)) for p in ("down", "up", "tilt")])
    return out


FACES_CPP = Path(r"C:\dev\ai-agent-monitor\src\faces.cpp")
S = 3                        # screen pixels per cell
X0 = 120 - W * S // 2  # the canvas's left edge on screen
Y0 = 8                       # its top (the antenna starts at row 2)
J = lambda v: json.dumps(v, separators=(",", ":"))
PAL = {k: v for k, v in PAL.items() if k != "a"}
PAL["a"] = "{msg.color}"
SHOW = "{msg.anim}&&{msg.kind:fig}"
HOP = "({msg.shown}<6&&{msg.kind}==done&&{time.ms}>=500)*%d" % (2 * S)

def item(name, frame=None, slump=False, bg=None):
    x, y, frames = parts()[name]
    it = {"type": "sprite", "x": X0 + x * S, "y": "%d%s-%s" % (Y0 + y * S, "+{msg.kind:dy}" if slump else "", HOP),
          "scale": S, "palette": PAL, "show": SHOW}
    if bg:
        it["bg"] = bg
    if len(frames) == 1:
        it["rows"] = frames[0]
    else:
        it["frame"] = frame
        it["frames"] = frames
    return J(it)

items = [
    item("body"),
    item("left", "{msg.kind}==done"),
    item("right", "{msg.kind}==done?1:({msg.shown}<30||{time.sec}%10<3)&&{msg.kind}==waiting?1+({time.ms}>=500):0"),
    item("head", slump=True),
    item("face", "{msg.kind}==done?1:{msg.kind}==error?2:{time.sec}%5==0&&{time.ms}<300?3:0", slump=True, bg=PAL["e"]),
]

def write_template():
    src = FACES_CPP.read_bytes().decode("utf-8")   # bytes: keep its CRLF line endings
    crlf = "\r\n" in src
    src = src.replace("\r\n", "\n")
    lines = src.split("\n")
    start = next(i for i, l in enumerate(lines) if l.startswith('const char MESSAGE_FACE[] PROGMEM'))
    end = next(i for i in range(start, len(lines)) if lines[i].endswith(')MF";'))
    body = [l for l in lines[start:end + 1] if not l.startswith('{"type":"sprite"')]
    # The figure goes right after the icon's path item.
    k = next(i for i, l in enumerate(body) if l.startswith('{"type":"path"'))
    body[k + 1:k + 1] = [i + "," for i in items]
    # maps: add dy (the error slump: one cell lower)
    body = [l.replace('"maps":{"fig"', '"maps":{"dy":{"error":%d,"*":0},"fig"' % S) if '"maps":{"fig"' in l else l for l in body]
    lines[start:end + 1] = body
    out = "\n".join(lines)
    FACES_CPP.write_bytes((out.replace("\n", "\r\n") if crlf else out).encode("utf-8"))
    size = sum(len(l) + 1 for l in body)
    print("template", size, "bytes;", sum(len(i) for i in items), "for the robot")


def logo():
    out = ROOT / "docs" / "images"
    big = image(FRAMES["waveA"], "#ffa826", 16, "#ffffff").convert("RGBA")
    big.putdata([(0, 0, 0, 0) if p[:3] == (255, 255, 255) else p for p in big.getdata()])
    big.save(out / "robot.png")
    names = ["idle", "waveA", "waveB", "cheer", "hop", "error"]
    sheet = Image.new("RGB", (W * 8 * len(names), H * 8), "#1b2733")
    for i, (n, acc) in enumerate(zip(names, ["#ffd60a", "#ffd60a", "#ffd60a", "#30d158", "#30d158", "#ff453a"])):
        sheet.paste(image(FRAMES[n], acc, 8, "#1b2733"), (i * W * 8, 0))
    sheet.save(out / "robot-sheet.png")
    # The README's robot: waving hello, blinking now and then (a looping GIF, transparent).
    hello = [frame("idle", ("down", "up")), frame("idle", ("down", "tilt"))]
    seq = [(frame("idle"), 600), (frame("blink"), 120), (frame("idle"), 500)] +           [(hello[i % 2], 220) for i in range(8)] + [(frame("idle"), 900), (frame("blink"), 120), (frame("idle"), 400)]
    pal = dict(PAL, a="#ffa826")
    keys = list(pal)
    imgs = []
    for rows, _ in seq:
        im = Image.new("P", (W * 10, H * 10), 0)
        im.putpalette([0, 0, 0] + [c for k in keys for c in rgb(pal[k])])
        for y, row in enumerate(rows):
            for x, ch in enumerate(row):
                if ch in pal:
                    im.paste(keys.index(ch) + 1, (x * 10, y * 10, (x + 1) * 10, (y + 1) * 10))
        imgs.append(im)
    imgs[0].save(out / "robot.gif", save_all=True, append_images=imgs[1:], duration=[d for _, d in seq],
                 loop=0, transparency=0, disposal=2, optimize=False)


if __name__ == "__main__":
    write_template()
    if "--logo" in sys.argv:
        logo()
