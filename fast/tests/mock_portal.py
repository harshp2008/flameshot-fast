#!/usr/bin/env python3
"""Mock org.freedesktop.portal.Desktop for the ScreenCast tests.

Runs on a PRIVATE session bus only (DBUS_SESSION_BUS_ADDRESS). Behaviour:

  MOCK_MODE   ok | cancel | hang | nostream | mismatch | badver
  MOCK_PW     real | silent | dead | error      (what OpenPipeWireRemote gives)
  MOCK_STREAMS  "node:x,y,w,h;node:x,y,w,h"     streams reported by Start
  MOCK_PW_SOCKET  path of the private PipeWire socket (MOCK_PW=real)
  MOCK_LOG    file that receives one line per call
  MOCK_SHOT   PNG written for Screenshot() (stock-path fallback tests)
"""
import os
import socket
import struct
import sys
import zlib

import gi
gi.require_version("Gio", "2.0")
from gi.repository import Gio, GLib  # noqa: E402

MODE = os.environ.get("MOCK_MODE", "ok")
PW = os.environ.get("MOCK_PW", "silent")
STREAMS = os.environ.get("MOCK_STREAMS", "40:0,0,800,600")
LOG = os.environ["MOCK_LOG"]
SHOT = os.environ.get("MOCK_SHOT", "")

KEEP = []  # sockets that must stay open for the process lifetime


def log(msg):
    with open(LOG, "a") as f:
        f.write(msg + "\n")


def write_png(path, w=64, h=48):
    raw = b"".join(b"\x00" + bytes([200, 30, 30]) * w for _ in range(h))

    def chunk(t, d):
        c = struct.pack(">I", len(d)) + t + d
        return c + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)

    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(png)


XML = """
<node>
 <interface name='org.freedesktop.portal.ScreenCast'>
  <method name='CreateSession'><arg type='a{sv}' direction='in'/><arg type='o' direction='out'/></method>
  <method name='SelectSources'><arg type='o' direction='in'/><arg type='a{sv}' direction='in'/><arg type='o' direction='out'/></method>
  <method name='Start'><arg type='o' direction='in'/><arg type='s' direction='in'/><arg type='a{sv}' direction='in'/><arg type='o' direction='out'/></method>
  <method name='OpenPipeWireRemote'><arg type='o' direction='in'/><arg type='a{sv}' direction='in'/><arg type='h' direction='out'/></method>
  <property name='version' type='u' access='read'/>
  <property name='AvailableSourceTypes' type='u' access='read'/>
 </interface>
 <interface name='org.freedesktop.portal.Screenshot'>
  <method name='Screenshot'><arg type='s' direction='in'/><arg type='a{sv}' direction='in'/><arg type='o' direction='out'/></method>
  <property name='version' type='u' access='read'/>
 </interface>
</node>"""
SESSION_XML = """
<node><interface name='org.freedesktop.portal.Session'>
 <method name='Close'/></interface></node>"""

conn = None
session_count = 0


def request_path(sender, options):
    token = options.get("handle_token", "t")
    return "/org/freedesktop/portal/desktop/request/%s/%s" % (
        sender.replace(":", "").replace(".", "_"), token)


def respond(path, code, results, delay=15):
    def emit():
        conn.emit_signal(None, path, "org.freedesktop.portal.Request", "Response",
                         GLib.Variant("(ua{sv})", (code, results)))
        return False
    GLib.timeout_add(delay, emit)


def streams_variant():
    out = []
    if MODE == "nostream":
        return GLib.Variant("a(ua{sv})", [])
    for spec in STREAMS.split(";"):
        node, geo = spec.split(":")
        x, y, w, h = [int(v) for v in geo.split(",")]
        if MODE == "mismatch":
            x += 5000
        out.append((int(node), {"position": GLib.Variant("(ii)", (x, y)),
                                "size": GLib.Variant("(ii)", (w, h)),
                                "source_type": GLib.Variant("u", 1)}))
    return GLib.Variant("a(ua{sv})", out)


def on_session_call(c, sender, path, iface, method, params, inv):
    log("Close %s" % path)
    inv.return_value(None)


def on_call(c, sender, path, iface, method, params, inv):
    global session_count
    if iface == "org.freedesktop.portal.Screenshot":
        opts = params.unpack()[1]
        rp = request_path(sender, opts)
        log("Screenshot")
        inv.return_value(GLib.Variant("(o)", (rp,)))
        if SHOT:
            write_png(SHOT)
            respond(rp, 0, {"uri": GLib.Variant("s", "file://" + SHOT)})
        else:
            respond(rp, 2, {})
        return
    args = params.unpack()
    if method == "CreateSession":
        rp = request_path(sender, args[0])
        session_count += 1
        spath = "/org/freedesktop/portal/desktop/session/mock/s%d" % session_count
        node = Gio.DBusNodeInfo.new_for_xml(SESSION_XML)
        conn.register_object(spath, node.interfaces[0], on_session_call, None, None)
        log("CreateSession")
        inv.return_value(GLib.Variant("(o)", (rp,)))
        respond(rp, 0, {"session_handle": GLib.Variant("s", spath)})
    elif method == "SelectSources":
        rp = request_path(sender, args[1])
        log("SelectSources restore_token=%s persist_mode=%s types=%s multiple=%s cursor_mode=%s" % (
            args[1].get("restore_token", ""), args[1].get("persist_mode"),
            args[1].get("types"), args[1].get("multiple"), args[1].get("cursor_mode")))
        inv.return_value(GLib.Variant("(o)", (rp,)))
        respond(rp, 0, {})
    elif method == "Start":
        rp = request_path(sender, args[2])
        log("Start mode=%s" % MODE)
        inv.return_value(GLib.Variant("(o)", (rp,)))
        if MODE == "hang":
            return
        if MODE == "cancel":
            respond(rp, 1, {})
            return
        respond(rp, 0, {"streams": streams_variant(),
                        "restore_token": GLib.Variant("s", "tok-new")})
    elif method == "OpenPipeWireRemote":
        log("OpenPipeWireRemote pw=%s" % PW)
        if PW == "error":
            inv.return_dbus_error("org.freedesktop.portal.Error.Failed", "mock: no pipewire")
            return
        if PW == "real":
            s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            s.connect(os.environ["MOCK_PW_SOCKET"])
            mine = s
        else:
            mine, peer = socket.socketpair(socket.AF_UNIX, socket.SOCK_STREAM)
            if PW == "dead":
                peer.close()
            else:
                KEEP.append(peer)
        # the fd list takes ownership of the descriptor
        fdl = Gio.UnixFDList.new_from_array([mine.detach()])
        inv.return_value_with_unix_fd_list(GLib.Variant("(h)", (0,)), fdl)


def on_prop(c, sender, path, iface, prop):
    if prop == "version":
        return GLib.Variant("u", 3 if MODE == "badver" else 4)
    if prop == "AvailableSourceTypes":
        return GLib.Variant("u", 1)
    return None


def main():
    global conn
    addr = os.environ["DBUS_SESSION_BUS_ADDRESS"]
    conn = Gio.DBusConnection.new_for_address_sync(
        addr, Gio.DBusConnectionFlags.AUTHENTICATION_CLIENT |
        Gio.DBusConnectionFlags.MESSAGE_BUS_CONNECTION, None, None)
    info = Gio.DBusNodeInfo.new_for_xml(XML)
    for iface in info.interfaces:
        conn.register_object("/org/freedesktop/portal/desktop", iface, on_call, on_prop, None)
    loop = GLib.MainLoop()
    Gio.bus_own_name_on_connection(conn, "org.freedesktop.portal.Desktop",
                                   Gio.BusNameOwnerFlags.NONE, lambda *a: print("READY", flush=True), None)
    loop.run()


main()
