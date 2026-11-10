#!/usr/bin/env python3
# Copyright 2026 Kerem Soke
# SPDX-License-Identifier: GPL-3.0-or-later
"""Seeds for the protocol fuzzers: every request of a group's interfaces, with
type-correct arguments, in the input format of fuzz-protocols.c.

  make-seeds.py OUTPUT-DIR

Object ids follow the fixed layout the fuzzer sets up (see fuzz-protocols.c).
"""

import itertools
import pathlib
import struct
import sys
import xml.etree.ElementTree as ET

ROOT = pathlib.Path(__file__).resolve().parents[2]
XML_DIRS = [ROOT / "protocol", pathlib.Path("/usr/share/wayland-protocols"), pathlib.Path("/usr/share/wayland")]
FIXED = {"wl_compositor": 3, "wl_seat": 4, "wl_output": 5, "xdg_wm_base": 6, "wl_shm": 7}
GROUPS = {
    "layer-shell": ["zwlr_layer_shell_v1"],
    "foreign-toplevel": ["zwlr_foreign_toplevel_manager_v1", "ext_foreign_toplevel_list_v1",
                         "xfce_foreign_toplevel_manager_private_v1"],
    "workspace": ["ext_workspace_manager_v1"],
    "session-lock": ["ext_session_lock_manager_v1"],
    "data-control": ["zwlr_data_control_manager_v1", "ext_data_control_manager_v1"],
    "image-copy-capture": ["ext_output_image_capture_source_manager_v1",
                           "ext_foreign_toplevel_image_capture_source_manager_v1",
                           "ext_image_copy_capture_manager_v1"],
}
LIMIT = 60


def interfaces():
    found = {}
    for directory in XML_DIRS:
        for path in sorted(directory.rglob("*.xml")):
            try:
                tree = ET.parse(path)
            except ET.ParseError:
                continue
            for element in tree.getroot().iter("interface"):
                found.setdefault(element.get("name"), element)
    return found


def pad(data):
    return data + b"\0" * (-len(data) % 4)


def encode(args, ids, fresh, created):
    out = b""
    counter = itertools.count()
    for arg in args:
        kind = arg.get("type")
        n = next(counter)
        if kind == "uint":
            out += struct.pack("<I", [0, 1, 2, 3][n % 4])
        elif kind == "int":
            out += struct.pack("<i", [0, 1, -1, 64][n % 4])
        elif kind == "fixed":
            out += struct.pack("<i", 256 * (n % 3))
        elif kind == "string":
            text = b"polo\0"
            out += struct.pack("<I", len(text)) + pad(text)
        elif kind == "array":
            out += struct.pack("<I", 0)
        elif kind == "fd":
            pass
        elif kind == "object":
            out += struct.pack("<I", ids.get(arg.get("interface"), 0))
        elif kind == "new_id":
            new = next(fresh)
            if arg.get("interface"):
                created.append((arg.get("interface"), new))
            else:
                out += struct.pack("<I", 0) + b"\0" * 4
                out += struct.pack("<I", 1)
            out += struct.pack("<I", new)
    return out


def record(obj, opcode, payload):
    payload = pad(payload)[:255]
    return bytes([obj, opcode, len(payload)]) + payload


def build(group, defs):
    names = GROUPS[group]
    ids = dict(FIXED)
    for i, name in enumerate(names):
        ids[name] = 8 + i
    surface = 8 + len(names)
    ids["wl_surface"] = surface
    fresh = itertools.count(surface + 1)
    queue = [(name, ids[name]) for name in names]
    records = []
    seen = set()
    while queue:
        interface, obj = queue.pop(0)
        if (interface, obj) in seen or obj >= LIMIT or interface not in defs:
            continue
        seen.add((interface, obj))
        requests = [r for r in defs[interface].findall("request")]
        ordered = [r for r in requests if r.get("type") != "destructor"] + \
                  [r for r in requests if r.get("type") == "destructor"]
        index = {r.get("name"): i for i, r in enumerate(requests)}
        for request in ordered:
            created = []
            payload = encode(request.findall("arg"), ids, fresh, created)
            records.append(record(obj, index[request.get("name")], payload))
            for new_interface, new_id in created:
                ids.setdefault(new_interface, new_id)
                queue.append((new_interface, new_id))
    return records


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    defs = interfaces()
    out = pathlib.Path(sys.argv[1])
    for group in GROUPS:
        missing = [n for n in GROUPS[group] if n not in defs]
        if missing:
            print(f"{group}: no protocol XML for {missing}")
            return 1
        directory = out / group
        directory.mkdir(parents=True, exist_ok=True)
        records = build(group, defs)
        (directory / "all-requests").write_bytes(b"".join(records))
        for i, one in enumerate(records):
            (directory / f"request-{i:03d}").write_bytes(one)
        print(f"{group}: {len(records)} requests")
    return 0


if __name__ == "__main__":
    sys.exit(main())
