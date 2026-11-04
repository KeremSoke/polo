# Copyright 2026 Kerem Soke
# SPDX-License-Identifier: GPL-3.0-or-later
"""D-Bus interfaces as comparable values, for the session manager ABI test."""

import xml.etree.ElementTree as ET

IGNORED = ("org.freedesktop.DBus.",)


def _args(element, direction_default):
    return tuple((a.get("name") or "", a.get("type"), a.get("direction") or direction_default)
                 for a in element.findall("arg"))


def interfaces(xml):
    """{interface: {"method NAME": args, "signal NAME": args, "property NAME": (type, access)}}"""
    root = ET.fromstring(xml)
    result = {}
    for interface in root.findall("interface"):
        name = interface.get("name")
        if name.startswith(IGNORED):
            continue
        members = {}
        for method in interface.findall("method"):
            members["method " + method.get("name")] = _args(method, "in")
        for signal in interface.findall("signal"):
            members["signal " + signal.get("name")] = _args(signal, "out")
        for prop in interface.findall("property"):
            members["property " + prop.get("name")] = (prop.get("type"), prop.get("access"))
        result[name] = members
    return result


def differences(expected, actual, label):
    """Lines describing how actual's members differ from expected's."""
    lines = []
    for member in sorted(set(expected) | set(actual)):
        if member not in actual:
            lines.append(f"{label}: missing {member}")
        elif member not in expected:
            lines.append(f"{label}: unexpected {member}")
        elif expected[member] != actual[member]:
            lines.append(f"{label}: {member} is {actual[member]}, expected {expected[member]}")
    return lines
