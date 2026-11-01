"""Asks the clock's mDNS responder for its name and its web service; prints what came back.

Run inside the emulator's network (the integration case does it with
`docker run --network container:<emulator>`):  python mdns_query.py <host>

Prints one line per record type seen (`A <ip>`, `SRV <port> <target>`, `PTR <name>`,
`TXT <text>`), then `answers <n>` (the replies counted) last."""
import socket
import struct
import sys


def query(name, qtype):
    out = struct.pack(">HHHHHH", 0, 0, 1, 0, 0, 0)
    for part in name.split("."):
        out += bytes([len(part)]) + part.encode()
    return out + b"\0" + struct.pack(">HH", qtype, 1)


def read_name(data, at):
    parts, jumps, end = [], 0, None
    while True:
        n = data[at]
        if n == 0:
            return ".".join(parts), (end if end is not None else at + 1)
        if n & 0xC0 == 0xC0:
            if end is None:
                end = at + 2
            at = (n & 0x3F) << 8 | data[at + 1]
            jumps += 1
            if jumps > 8:
                raise ValueError("pointer loop")
            continue
        parts.append(data[at + 1:at + 1 + n].decode(errors="replace"))
        at += 1 + n


def records(data):
    count = sum(struct.unpack(">HHH", data[6:12]))
    at = 12
    for _ in range(struct.unpack(">H", data[4:6])[0]):   # questions, if any
        _, at = read_name(data, at)
        at += 4
    for _ in range(count):
        name, at = read_name(data, at)
        rtype, _, _, length = struct.unpack(">HHIH", data[at:at + 10])
        at += 10
        yield name, rtype, data[at:at + length], at
        at += length


s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEPORT, 1)
s.bind(("", 5353))
s.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP, socket.inet_aton("224.0.0.251") + socket.inet_aton("0.0.0.0"))
s.settimeout(0.5)
answers, seen = 0, {}
host = sys.argv[1]
for _ in range(6):
    for packet in (query(host + ".local", 1), query("_http._tcp.local", 12)):
        s.sendto(packet, ("224.0.0.251", 5353))
    try:
        while True:
            data, _ = s.recvfrom(2048)
            if not data[2] & 0x80:   # our own query looped back
                continue
            answers += 1
            for name, rtype, rdata, at in records(data):
                if rtype == 1 and name == host + ".local":
                    seen["A"] = socket.inet_ntoa(rdata)
                elif rtype == 33:
                    seen["SRV"] = f"{struct.unpack('>H', rdata[4:6])[0]} {read_name(data, at + 6)[0]}"
                elif rtype == 12:
                    seen["PTR"] = read_name(data, at)[0]
                elif rtype == 16:
                    seen["TXT"] = rdata.decode(errors="replace")
    except socket.timeout:
        pass
for k, v in seen.items():
    print(k, v)
print("answers", answers)
