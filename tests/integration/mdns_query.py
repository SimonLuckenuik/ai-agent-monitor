"""Asks the clock's mDNS responder for its name and its web service; prints the answers.

Run inside the emulator's network (the integration case does it with
`docker run --network container:<emulator>`):  python mdns_query.py <host>"""
import socket
import struct
import sys


def query(name, qtype):
    out = struct.pack(">HHHHHH", 0, 0, 1, 0, 0, 0)
    for part in name.split("."):
        out += bytes([len(part)]) + part.encode()
    return out + b"\0" + struct.pack(">HH", qtype, 1)


s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEPORT, 1)
s.bind(("", 5353))
s.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP, socket.inet_aton("224.0.0.251") + socket.inet_aton("0.0.0.0"))
s.settimeout(0.5)
answers = 0
for _ in range(6):
    for packet in (query(sys.argv[1] + ".local", 1), query("_http._tcp.local", 12)):
        s.sendto(packet, ("224.0.0.251", 5353))
    try:
        while True:
            data, _ = s.recvfrom(2048)
            if data[2] & 0x80:   # a response, not our own query looped back
                answers += 1
    except socket.timeout:
        pass
print("answers", answers)
