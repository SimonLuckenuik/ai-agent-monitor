"""A stand-in OpenClaw Gateway for the openclaw_node integration case (spec 041).

    python fake_gateway.py [--ws 18789] [--control 8790]

Standard library only (it runs in python:3.12-alpine beside the emulator). The node
side is checked the way the gateway checks it (openclaw/openclaw 92d5d16,
docs/gateway/protocol/handshake.md and packages/gateway-client/src/device-auth.ts):
the protocol range, the client id, mode and role, device.id = sha256(publicKey), the
challenge's nonce and ts signed back, and the v3 signature, verified here with a
pure-Python Ed25519 (RFC 8032) over the payload rebuilt byte for byte. Then the
credential: the shared token, a bootstrap token (once), or the device token issued in
hello-ok, and the pairing approval when required.

Control API (HTTP, JSON):
    GET  /state             {"connects": [...], "nodes": [deviceId...], "refusals": [...], "frames": n}
    POST /reset {options}   token, bootstrapToken, deviceToken, requirePairing; forgets everything
    POST /approve {"id"}    approves a device
    POST /revoke            the device token stops working
    POST /drop              closes every node's socket, as a gateway restart would
    POST /invoke {"command", "params", "raw"?}   to the connected node; waits for its result
"""
import argparse
import base64
import hashlib
import json
import os
import socket
import threading
import time
import uuid
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

# ---------------------------------------------------------------- Ed25519 (RFC 8032), verify only
P = 2 ** 255 - 19
L = 2 ** 252 + 27742317777372353535851937790883648493
D = -121665 * pow(121666, P - 2, P) % P
SQRT_M1 = pow(2, (P - 1) // 4, P)


def _recover_x(y, sign):
    xx = (y * y - 1) * pow(D * y * y + 1, P - 2, P) % P
    x = pow(xx, (P + 3) // 8, P)
    if (x * x - xx) % P:
        x = x * SQRT_M1 % P
    if (x * x - xx) % P:
        raise ValueError("not a point")
    if x & 1 != sign:
        x = P - x
    return x


def _add(a, b):
    x1, y1, z1, t1 = a
    x2, y2, z2, t2 = b
    A = (y1 - x1) * (y2 - x2) % P
    B = (y1 + x1) * (y2 + x2) % P
    C = t1 * 2 * D * t2 % P
    Dd = z1 * 2 * z2 % P
    E, F, G, H = B - A, Dd - C, Dd + C, B + A
    return (E * F % P, G * H % P, F * G % P, E * H % P)


def _mul(s, pt):
    q = (0, 1, 1, 0)
    while s:
        if s & 1:
            q = _add(q, pt)
        pt = _add(pt, pt)
        s >>= 1
    return q


def _decode(b):
    y = int.from_bytes(b, "little")
    sign, y = y >> 255, y & ((1 << 255) - 1)
    if y >= P:
        raise ValueError("not a point")
    x = _recover_x(y, sign)
    return (x, y, 1, x * y % P)


def _encode(pt):
    x, y, z, _ = pt
    zi = pow(z, P - 2, P)
    x, y = x * zi % P, y * zi % P
    return (y | ((x & 1) << 255)).to_bytes(32, "little")


_BY = 4 * pow(5, P - 2, P) % P
BASE = (_recover_x(_BY, 0), _BY, 1, _recover_x(_BY, 0) * _BY % P)


def ed25519_verify(pub, msg, sig):
    if len(pub) != 32 or len(sig) != 64:
        return False
    try:
        a, r = _decode(pub), _decode(sig[:32])
    except ValueError:
        return False
    s = int.from_bytes(sig[32:], "little")
    if s >= L:
        return False
    h = int.from_bytes(hashlib.sha512(sig[:32] + pub + msg).digest(), "little") % L
    return _encode(_mul(s, BASE)) == _encode(_add(r, _mul(h, a)))


# RFC 8032 test 2, so a broken verifier cannot pass every node.
assert ed25519_verify(bytes.fromhex("3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c"), b"\x72",
                      bytes.fromhex("92a009a9f0d4cab8720e820b5f642540a2b27b5416503f8fb3762223ebdb69da"
                                    "085ac1e43e15996e458f3613d0f11d8c387b2eaeb4302aeeb00d291612bb0c00"))


def b64url_decode(s):
    return base64.urlsafe_b64decode(s + "=" * (-len(s) % 4))


def norm(v):   # the gateway's normalizeDeviceMetadataForAuth: trim, A-Z to a-z
    return "".join(chr(ord(c) + 32) if "A" <= c <= "Z" else c for c in v.strip()) if isinstance(v, str) else ""


def auth_payload_v3(device_id, client_id, mode, role, scopes, signed_at, token, nonce, platform, family):
    return "|".join(["v3", device_id, client_id, mode, role, ",".join(scopes), str(signed_at), token or "",
                     nonce, norm(platform), norm(family)])


# ---------------------------------------------------------------- WebSocket (RFC 6455), server side
GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"


def frame(payload, opcode=1):
    n = len(payload)
    head = bytes([0x80 | opcode])
    if n < 126:
        head += bytes([n])
    elif n < 65536:
        head += bytes([126]) + n.to_bytes(2, "big")
    else:
        head += bytes([127]) + n.to_bytes(8, "big")
    return head + payload


class Conn:
    def __init__(self, sock):
        self.sock, self.buf, self.lock = sock, b"", threading.Lock()
        self.nonce, self.ts, self.device = uuid.uuid4().hex, int(time.time() * 1000), None

    def recv_exact(self, n):
        while len(self.buf) < n:
            chunk = self.sock.recv(65536)
            if not chunk:
                raise ConnectionError("closed")
            self.buf += chunk
        out, self.buf = self.buf[:n], self.buf[n:]
        return out

    def read_frame(self):
        b0, b1 = self.recv_exact(2)
        n = b1 & 127
        if n == 126:
            n = int.from_bytes(self.recv_exact(2), "big")
        elif n == 127:
            n = int.from_bytes(self.recv_exact(8), "big")
        if not b1 & 0x80:
            raise ConnectionError("client frame not masked")
        mask = self.recv_exact(4)
        data = bytes(c ^ mask[i & 3] for i, c in enumerate(self.recv_exact(n)))
        return b0 & 15, data

    def send(self, obj, opcode=1):
        data = obj if isinstance(obj, bytes) else json.dumps(obj).encode()
        with self.lock:
            self.sock.sendall(frame(data, opcode))


# ---------------------------------------------------------------- the gateway
class Gateway:
    def __init__(self):
        self.lock = threading.Lock()
        self.reset({})

    def reset(self, o):
        with self.lock:
            for c in getattr(self, "conns", []):
                try:
                    c.sock.close()
                except OSError:
                    pass
            self.o = {"token": "gw-shared-token", "bootstrapToken": "", "deviceToken": "dev-" + uuid.uuid4().hex,
                      "requirePairing": False, **o}
            self.approved, self.conns, self.nodes = set(), [], {}
            self.connects, self.refusals, self.frames, self.pending = [], [], 0, {}

    def state(self):
        with self.lock:
            return {"connects": self.connects, "nodes": list(self.nodes), "refusals": self.refusals,
                    "frames": self.frames, "deviceToken": self.o["deviceToken"]}

    def check(self, c, p):
        if not (p.get("minProtocol", 99) <= 4 and p.get("maxProtocol", 0) >= 3):
            return "protocol"
        cl = p.get("client") or {}
        if cl.get("id") != "node-host" or cl.get("mode") != "node" or p.get("role") != "node":
            return "client"
        d = p.get("device") or {}
        if d.get("nonce") != c.nonce or d.get("signedAt") != c.ts:
            return "nonce"
        try:
            raw = b64url_decode(d.get("publicKey", ""))
        except ValueError:
            return "publicKey"
        if len(raw) != 32 or d.get("publicKey", "").endswith("="):
            return "publicKey"
        if hashlib.sha256(raw).hexdigest() != d.get("id"):
            return "deviceId"
        auth = p.get("auth") or {}
        kind = next(iter(auth), "")
        token = "" if kind == "password" else auth.get(kind, "")
        payload = auth_payload_v3(d["id"], cl["id"], cl["mode"], p["role"], p.get("scopes") or [], d["signedAt"],
                                  token, d["nonce"], cl.get("platform"), cl.get("deviceFamily"))
        try:
            sig = b64url_decode(d.get("signature", ""))
        except ValueError:
            return "signature"
        if not ed25519_verify(raw, payload.encode(), sig):
            return "signature"
        return ""

    def refuse(self, c, rid, code, message, detail):
        with self.lock:
            self.refusals.append({"code": code, "detail": detail, "message": message})
        c.send({"type": "res", "id": rid, "ok": False, "error": {"code": code, "message": message, "details": {"code": detail}}})
        time.sleep(0.05)
        c.send(b"\x03\xe8", 8)

    def on_connect(self, c, rid, p):
        with self.lock:
            self.connects.append(p)
        bad = self.check(c, p)
        if bad:
            return self.refuse(c, rid, "INVALID_REQUEST", "device auth failed: " + bad, "DEVICE_AUTH_INVALID")
        auth, dev = p.get("auth") or {}, p["device"]["id"]
        with self.lock:
            if "deviceToken" in auth:
                ok = auth["deviceToken"] == self.o["deviceToken"] and dev in self.approved
            elif "bootstrapToken" in auth:
                ok = bool(self.o["bootstrapToken"]) and auth["bootstrapToken"] == self.o["bootstrapToken"]
                if ok:
                    self.o["bootstrapToken"] = ""   # single use; enrolment approves the device
                    self.approved.add(dev)
            elif "token" in auth:
                ok = auth["token"] == self.o["token"]
            else:
                ok = False
            pending = ok and self.o["requirePairing"] and dev not in self.approved
        if not ok:
            return self.refuse(c, rid, "UNAUTHORIZED", "auth failed", "AUTH_TOKEN_MISMATCH")
        if pending:
            return self.refuse(c, rid, "NOT_PAIRED", "pairing required", "PAIRING_REQUIRED")
        with self.lock:
            self.approved.add(dev)
            self.nodes[dev] = c
            c.device = dev
        c.send({"type": "res", "id": rid, "ok": True, "payload": {
            "type": "hello-ok", "protocol": 4, "server": {"version": "fake", "connId": uuid.uuid4().hex},
            "features": {"methods": ["node.invoke.result"], "events": ["node.invoke.request", "tick"]},
            "snapshot": {"presence": [{"text": "x" * 64} for _ in range(1500)]},   # about 100 KB, as a busy gateway
            "policy": {"maxPayload": 26214400, "maxBufferedBytes": 52428800, "tickIntervalMs": 15000},
            "auth": {"role": "node", "scopes": [], "deviceToken": self.o["deviceToken"],
                     "deviceTokens": [{"role": "operator", "deviceToken": "operator-handoff",
                                       "scopes": ["operator.read", "operator.write"]}]}}})

    def serve(self, sock):
        c = Conn(sock)
        try:
            head = b""
            while b"\r\n\r\n" not in head:
                chunk = sock.recv(4096)
                if not chunk:
                    return
                head += chunk
            req, _, rest = head.partition(b"\r\n\r\n")
            c.buf = rest
            headers = {k.strip().lower(): v.strip() for k, _, v in
                       (line.partition(":") for line in req.decode(errors="replace").split("\r\n")[1:])}
            key = headers.get("sec-websocket-key", "")
            try:
                key_ok = len(base64.b64decode(key, validate=True)) == 16
            except ValueError:
                key_ok = False
            if not key_ok or headers.get("upgrade", "").lower() != "websocket":
                sock.sendall(b"HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n")
                return
            accept = base64.b64encode(hashlib.sha1((key + GUID).encode()).digest()).decode()
            sock.sendall(("HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                          f"Sec-WebSocket-Accept: {accept}\r\n\r\n").encode())
            with self.lock:
                self.conns.append(c)
            c.send({"type": "event", "event": "connect.challenge", "payload": {"nonce": c.nonce, "ts": c.ts}})
            while True:
                op, data = c.read_frame()
                with self.lock:
                    self.frames += 1
                if op == 8:
                    c.send(data[:2], 8)
                    return
                if op == 9:
                    c.send(data, 10)
                    continue
                if op != 1:
                    continue
                msg = json.loads(data)
                if msg.get("type") != "req":
                    continue
                if msg.get("method") == "connect":
                    self.on_connect(c, msg.get("id"), msg.get("params") or {})
                elif msg.get("method") == "node.invoke.result":
                    c.send({"type": "res", "id": msg.get("id"), "ok": True, "payload": {"ok": True}})
                    r = msg.get("params") or {}
                    with self.lock:
                        slot = self.pending.pop(r.get("id"), None)
                    if slot:
                        slot["result"] = r
                        slot["done"].set()
                else:
                    c.send({"type": "res", "id": msg.get("id"), "ok": False,
                            "error": {"code": "UNKNOWN_METHOD", "message": str(msg.get("method"))}})
        except (ConnectionError, OSError, ValueError):
            pass
        finally:
            with self.lock:
                if c.device and self.nodes.get(c.device) is c:
                    del self.nodes[c.device]
                if c in self.conns:
                    self.conns.remove(c)
            try:
                sock.close()
            except OSError:
                pass

    def invoke(self, command, params, raw=None, timeout=10):
        with self.lock:
            node = next(iter(self.nodes.items()), None)
        if not node:
            return {"error": "no node connected"}
        dev, c = node
        rid = "inv_" + uuid.uuid4().hex
        slot = {"done": threading.Event()}
        with self.lock:
            self.pending[rid] = slot
        c.send({"type": "event", "event": "node.invoke.request", "payload": {
            "id": rid, "nodeId": dev, "command": command,
            "paramsJSON": raw if raw is not None else json.dumps(params or {}), "timeoutMs": timeout * 1000}})
        if not slot["done"].wait(timeout):
            return {"error": "timeout"}
        r = slot["result"]
        out = {"ok": r.get("ok"), "id": r.get("id") == rid, "nodeId": r.get("nodeId")}
        if r.get("payloadJSON") is not None:
            try:
                out["payload"] = json.loads(r["payloadJSON"])
            except ValueError:
                out["ok"], out["badPayloadJSON"] = False, r["payloadJSON"]
        if r.get("error"):
            out["error"] = r["error"]
        return out

    def drop(self):
        with self.lock:
            conns = list(self.conns)
        for c in conns:
            try:
                c.sock.shutdown(socket.SHUT_RDWR)
                c.sock.close()
            except OSError:
                pass


GW = Gateway()


class Control(BaseHTTPRequestHandler):
    def reply(self, obj, code=200):
        body = json.dumps(obj).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        self.reply(GW.state() if self.path == "/state" else {"error": "not found"}, 200 if self.path == "/state" else 404)

    def do_POST(self):
        n = int(self.headers.get("Content-Length") or 0)
        body = json.loads(self.rfile.read(n) or b"{}")
        if self.path == "/reset":
            GW.reset(body)
        elif self.path == "/approve":
            with GW.lock:
                GW.approved.add(body["id"])
        elif self.path == "/revoke":
            with GW.lock:
                GW.o["deviceToken"] = "revoked-" + uuid.uuid4().hex
        elif self.path == "/drop":
            GW.drop()
        elif self.path == "/invoke":
            return self.reply(GW.invoke(body["command"], body.get("params"), body.get("raw")))
        else:
            return self.reply({"error": "not found"}, 404)
        self.reply({"ok": True})

    def log_message(self, *a):
        pass


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ws", type=int, default=int(os.environ.get("GW_WS", 18789)))
    ap.add_argument("--control", type=int, default=int(os.environ.get("GW_CONTROL", 8790)))
    a = ap.parse_args()
    srv = socket.create_server(("0.0.0.0", a.ws), reuse_port=False)
    threading.Thread(target=ThreadingHTTPServer(("0.0.0.0", a.control), Control).serve_forever, daemon=True).start()
    print(f"fake gateway: ws on {a.ws}, control on {a.control}", flush=True)
    while True:
        sock, _ = srv.accept()
        threading.Thread(target=GW.serve, args=(sock,), daemon=True).start()


if __name__ == "__main__":
    main()
