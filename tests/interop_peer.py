#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 LingmoOS Team
# SPDX-License-Identifier: GPL-3.0-or-later
"""
A LocalSend v2 peer written from the official reference implementation
(localsend/localsend packages/core, v2.rs and tests/v2_server.rs,
v2_tls_pinning.rs), independent of the Qt code, to check lingmo-share
against the wire format of the official apps:

  * RSA-2048 self-signed "CN=LocalSend User" certificate, fingerprint =
    uppercase hex SHA-256 of the DER;
  * as a client: mTLS (presents its certificate), pins the server certificate
    to the fingerprint, sends deviceType null / version "2.2" like the Rust
    tests, uploads with a chunked (streamed) body, cancels without sessionId;
  * as a server: client certificate MANDATORY (like the official app without
    the web pages), checks the prepare-upload JSON and the upload bytes.

usage: interop_peer.py client <port> <server-fingerprint> <workdir>
       interop_peer.py server <port> <lingmo-port> <lingmo-fp> <lingmo-cert.pem> <workdir>
"""
import hashlib
import http.client
import json
import os
import ssl
import subprocess
import sys
import threading
import time
import uuid
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer


def make_identity(workdir):
    cert = os.path.join(workdir, "peer-cert.pem")
    key = os.path.join(workdir, "peer-key.pem")
    if not os.path.exists(cert):
        subprocess.run(["openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-keyout", key, "-out", cert,
                        "-subj", "/CN=LocalSend User", "-days", "3650"], check=True, capture_output=True)
    der = ssl.PEM_cert_to_DER_cert(open(cert).read())
    return cert, key, hashlib.sha256(der).hexdigest().upper()


def info(fp, port):
    # As in tests/v2_server.rs sender_info(): deviceType null, version 2.2
    return {"alias": "Reference Peer", "version": "2.2", "deviceModel": "Rust", "deviceType": None,
            "fingerprint": fp, "port": port, "protocol": "https", "download": False}


class Pinned:
    def __init__(self, cert, key, expected):
        self.ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
        self.ctx.check_hostname = False
        self.ctx.verify_mode = ssl.CERT_NONE   # self-signed: pinned below, during the handshake
        self.ctx.load_cert_chain(cert, key)    # mTLS
        self.expected = expected

    def conn(self, port):
        c = http.client.HTTPSConnection("127.0.0.1", port, context=self.ctx, timeout=30)
        c.connect()
        got = hashlib.sha256(c.sock.getpeercert(binary_form=True)).hexdigest().upper()
        if self.expected and got != self.expected:
            c.close()
            raise SystemExit(f"FAIL pin: server fingerprint {got} != {self.expected}")
        return c


def check(cond, what):
    print(("ok   " if cond else "FAIL ") + what, flush=True)
    if not cond:
        sys.exit(1)


def client(port, server_fp, workdir):
    cert, key, fp = make_identity(workdir)
    pinned = Pinned(cert, key, server_fp)

    # register (two-way discovery)
    c = pinned.conn(port)
    c.request("POST", "/api/localsend/v2/register", json.dumps(info(fp, 53999)), {"Content-Type": "application/json"})
    r = c.getresponse()
    body = json.loads(r.read() or b"{}")
    check(r.status == 200 and body.get("deviceModel") == "Lingmo" and "alias" in body, "register answered %s" % body)
    check(body.get("version", "").startswith("2."), "register reports protocol 2.x")

    # legacy info route answers too
    c.request("GET", "/api/localsend/v2/info")
    r = c.getresponse()
    check(r.status == 200 and json.loads(r.read()).get("fingerprint") == server_fp, "info fingerprint = certificate hash")

    # prepare-upload, trusted beforehand: accepted without asking
    data = os.urandom(300 * 1024 + 7)
    files = {
        "file-a": {"id": "file-a", "fileName": "reference/a.bin", "size": len(data),
                   "fileType": "application/octet-stream", "sha256": hashlib.sha256(data).hexdigest(),
                   "preview": None, "metadata": {"modified": "2020-08-15T10:20:30.500Z", "accessed": None}},
    }
    c.request("POST", "/api/localsend/v2/prepare-upload", json.dumps({"info": info(fp, 53999), "files": files}),
              {"Content-Type": "application/json"})
    r = c.getresponse()
    body = json.loads(r.read() or b"{}")
    check(r.status == 200 and "sessionId" in body and "file-a" in body.get("files", {}), "prepare-upload accepted")
    session, token = body["sessionId"], body["files"]["file-a"]

    # wrong token: 403
    c.request("POST", f"/api/localsend/v2/upload?sessionId={session}&fileId=file-a&token=nope", b"x",
              {"Content-Type": "application/octet-stream"})
    r = c.getresponse(); r.read()
    check(r.status == 403, "upload with a wrong token is refused (403)")

    # missing parameters: 400
    c.request("POST", f"/api/localsend/v2/upload?sessionId={session}", b"")
    r = c.getresponse(); r.read()
    check(r.status == 400, "upload without fileId/token is refused (400)")

    # streamed (chunked) upload, like reqwest::Body::wrap_stream
    def chunks():
        for i in range(0, len(data), 1024):
            yield data[i:i + 1024]
    c2 = pinned.conn(port)
    c2.request("POST", f"/api/localsend/v2/upload?sessionId={session}&fileId=file-a&token={token}", chunks(),
               {"Content-Type": "application/octet-stream"}, encode_chunked=True)
    r = c2.getresponse(); r.read()
    check(r.status == 200, "chunked upload accepted")
    target = os.path.join(workdir, "Downloads", "reference", "a.bin")
    for _ in range(50):
        if os.path.exists(target):
            break
        time.sleep(0.1)
    check(os.path.exists(target) and open(target, "rb").read() == data, "file saved intact in the receive folder")
    check(abs(os.path.getmtime(target) - 1597486830.5) < 1, "sender's modification time kept")

    # sha256 mismatch: 422
    c.request("POST", "/api/localsend/v2/prepare-upload", json.dumps({"info": info(fp, 53999), "files": {
        "f": {"id": "f", "fileName": "bad.bin", "size": 4, "fileType": "application/octet-stream", "sha256": "00" * 32}}}),
        {"Content-Type": "application/json"})
    r = c.getresponse(); body = json.loads(r.read() or b"{}")
    check(r.status == 200, "second session accepted")
    c.request("POST", f"/api/localsend/v2/upload?sessionId={body['sessionId']}&fileId=f&token={body['files']['f']}", b"abcd")
    r = c.getresponse(); r.read()
    check(r.status == 422, "checksum mismatch answered 422")
    check(not os.path.exists(os.path.join(workdir, "Downloads", "bad.bin")), "corrupt file not kept")

    # A path that tries to leave the folder
    c.request("POST", "/api/localsend/v2/prepare-upload", json.dumps({"info": info(fp, 53999), "files": {
        "t": {"id": "t", "fileName": "../../../../tmp/escaped.txt", "size": 2, "fileType": "text/plain"}}}),
        {"Content-Type": "application/json"})
    r = c.getresponse(); body = json.loads(r.read() or b"{}")
    c.request("POST", f"/api/localsend/v2/upload?sessionId={body['sessionId']}&fileId=t&token={body['files']['t']}", b"hi")
    r = c.getresponse(); r.read()
    check(r.status == 200 and os.path.exists(os.path.join(workdir, "Downloads", "tmp", "escaped.txt")),
          "'../../' names stay inside the receive folder")
    print("client: all checks passed", flush=True)


def client_untrusted_cancel(port, server_fp, workdir):
    # A new identity the receiver doesn't trust: it is asked about, and the
    # sender cancels without knowing the session id (released app behaviour)
    sub = os.path.join(workdir, "other")
    os.makedirs(sub, exist_ok=True)
    cert, key, fp = make_identity(sub)
    pinned = Pinned(cert, key, server_fp)
    result = {}

    def prepare():
        c = pinned.conn(port)
        c.request("POST", "/api/localsend/v2/prepare-upload", json.dumps({"info": info(fp, 53999), "files": {
            "x": {"id": "x", "fileName": "x.txt", "size": 1, "fileType": "text/plain"}}}),
            {"Content-Type": "application/json"})
        result["status"] = c.getresponse().status

    t = threading.Thread(target=prepare)
    t.start()
    time.sleep(1.0)
    check("status" not in result, "untrusted sender waits for the user's answer")
    c = pinned.conn(port)
    c.request("POST", "/api/localsend/v2/cancel")
    c.getresponse().read()
    t.join(10)
    check(result.get("status") == 403, "session-less cancel withdraws the waiting request (403)")
    print("client-cancel: all checks passed", flush=True)


def server(port, lingmo_port, lingmo_fp, lingmo_cert, workdir):
    cert, key, fp = make_identity(workdir)
    received = {}
    sessions = {}

    class H(BaseHTTPRequestHandler):
        protocol_version = "HTTP/1.1"

        def log_message(self, *a):
            pass

        def body(self):
            if self.headers.get("Transfer-Encoding", "").lower() == "chunked":
                out = b""
                while True:
                    size = int(self.rfile.readline().strip(), 16)
                    if size == 0:
                        self.rfile.readline()
                        return out
                    out += self.rfile.read(size)
                    self.rfile.readline()
            return self.rfile.read(int(self.headers.get("Content-Length", 0)))

        def reply(self, code, obj=None):
            data = json.dumps(obj).encode() if obj is not None else b""
            self.send_response(code)
            self.send_header("Content-Length", str(len(data)))
            if data:
                self.send_header("Content-Type", "application/json")
            self.end_headers()
            self.wfile.write(data)

        def do_POST(self):
            client_cert = self.connection.getpeercert(binary_form=True)
            client_fp = hashlib.sha256(client_cert).hexdigest().upper() if client_cert else None
            path, _, query = self.path.partition("?")
            q = dict(p.split("=", 1) for p in query.split("&") if "=" in p)
            raw = self.body()
            if path == "/api/localsend/v2/register":
                self.reply(200, {"alias": "Reference Peer", "version": "2.2", "deviceModel": "Rust",
                                 "deviceType": "desktop", "fingerprint": fp, "download": False})
            elif path == "/api/localsend/v2/prepare-upload":
                req = json.loads(raw)
                inf = req["info"]
                ok = (client_fp == lingmo_fp and inf.get("fingerprint") == lingmo_fp
                      and inf.get("alias") and inf.get("version", "").startswith("2.")
                      and inf.get("protocol") in ("http", "https") and isinstance(inf.get("port"), int))
                for f in req["files"].values():
                    ok = ok and isinstance(f["size"], int) and f["fileName"] and f["fileType"] and f["id"]
                check(ok, "prepare-upload from lingmo-share matches the v2 schema (mTLS fingerprint %s)" % client_fp)
                sid = str(uuid.uuid4())
                sessions[sid] = {fid: str(uuid.uuid4()) for fid in req["files"]}
                sessions[sid + "names"] = {fid: f["fileName"] for fid, f in req["files"].items()}
                self.reply(200, {"sessionId": sid, "files": sessions[sid]})
            elif path == "/api/localsend/v2/upload":
                sid, fid, tok = q.get("sessionId"), q.get("fileId"), q.get("token")
                if sid not in sessions or sessions[sid].get(fid) != tok:
                    self.reply(403); return
                received[sessions[sid + "names"][fid]] = raw
                self.reply(200)
            elif path == "/api/localsend/v2/cancel":
                self.reply(200)
            else:
                self.reply(404)

    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    ctx.load_cert_chain(cert, key)
    # Client certificate mandatory, like the official app without its web pages
    ctx.verify_mode = ssl.CERT_REQUIRED
    ctx.load_verify_locations(lingmo_cert)
    httpd = ThreadingHTTPServer(("127.0.0.1", port), H)
    httpd.socket = ctx.wrap_socket(httpd.socket, server_side=True)

    threading.Thread(target=httpd.serve_forever, daemon=True).start()

    # Announce ourselves to lingmo-share like an answering peer: POST /register
    pinned = Pinned(cert, key, lingmo_fp)
    c = pinned.conn(lingmo_port)
    c.request("POST", "/api/localsend/v2/register", json.dumps(dict(info(fp, port), deviceType="mobile")),
              {"Content-Type": "application/json"})
    check(c.getresponse().status == 200, "registered with lingmo-share")
    print("server: ready " + fp, flush=True)
    # Wait for lingmo-share to send
    for _ in range(600):
        if received:
            break
        time.sleep(0.1)
    expected = open(os.path.join(workdir, "outgoing.bin"), "rb").read()
    check(received.get("outgoing.bin") == expected, "file sent by lingmo-share arrived intact")
    print("server: all checks passed", flush=True)
    httpd.shutdown()


def present(port, server_fp, workdir, alias, model, dtype, offer):
    """Shows up as a phone in lingmo-share (register) and optionally offers photos"""
    os.makedirs(workdir, exist_ok=True)
    cert, key, fp = make_identity(workdir)
    pinned = Pinned(cert, key, server_fp)
    me = dict(info(fp, 53317), alias=alias, deviceModel=model, deviceType=dtype)
    c = pinned.conn(port)
    c.request("POST", "/api/localsend/v2/register", json.dumps(me), {"Content-Type": "application/json"})
    r = c.getresponse(); r.read()
    print("register", r.status, flush=True)
    if not offer:
        return
    files = {str(i): {"id": str(i), "fileName": n, "size": s, "fileType": "image/jpeg"}
             for i, (n, s) in enumerate([("IMG_4021.HEIC", 2843211), ("IMG_4022.HEIC", 3120554), ("Praia 2026.jpg", 1832004)])}
    c.request("POST", "/api/localsend/v2/prepare-upload", json.dumps({"info": me, "files": files}),
              {"Content-Type": "application/json"})
    print("offer answered", c.getresponse().status, flush=True)


if __name__ == "__main__":
    if sys.argv[1] in ("present", "offer"):
        present(int(sys.argv[2]), sys.argv[3], sys.argv[4], sys.argv[5], sys.argv[6], sys.argv[7], sys.argv[1] == "offer")
        sys.exit(0)
    mode = sys.argv[1]
    if mode == "client":
        client(int(sys.argv[2]), sys.argv[3], sys.argv[4])
    elif mode == "client-cancel":
        client_untrusted_cancel(int(sys.argv[2]), sys.argv[3], sys.argv[4])
    elif mode == "fingerprint":
        print(make_identity(sys.argv[2])[2])
    else:
        server(int(sys.argv[2]), int(sys.argv[3]), sys.argv[4], sys.argv[5], sys.argv[6])
