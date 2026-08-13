#!/usr/bin/env python3

# Copyright 2026, UNSW
#
# SPDX-License-Identifier: BSD-2-Clause

"""Inspect a delegation bundle emitted by the Microkit tool."""

from argparse import ArgumentParser
from pathlib import Path
import struct


DLG_MAGIC = b"CapDelg\0"

RESOURCE_CHANNEL_NOTIFY = 1
RESOURCE_CHANNEL_PPC = 2
RESOURCE_MEMORY_REGION = 3
RESOURCE_IOPORT = 4

RESOURCE_NAMES = {
    RESOURCE_CHANNEL_NOTIFY: "channel-notify",
    RESOURCE_CHANNEL_PPC: "channel-ppc",
    RESOURCE_MEMORY_REGION: "memory-region",
    RESOURCE_IOPORT: "ioport",
}


#
# Dlg header format:
#
# + magic:             CapDelg\0, u8 [8] = 8
# + delegator_cnt:                 u32    = 4
# + total_size:                    u32    = 4
#
def parse_header(bundle_data):
    header_format = "<8sII"
    header_size = struct.calcsize(header_format)

    if len(bundle_data) < header_size:
        raise ValueError("delegation bundle is smaller than its header")

    magic, delegator_cnt, total_size = struct.unpack_from(
        header_format,
        bundle_data,
        0,
    )

    if magic != DLG_MAGIC:
        raise ValueError(
            f"invalid magic {magic!r}, expected {DLG_MAGIC!r}"
        )

    if total_size != len(bundle_data):
        raise ValueError(
            f"bundle header reports {total_size} bytes, "
            f"file contains {len(bundle_data)} bytes"
        )

    return delegator_cnt, header_size


#
# Delegator header format:
#
# + record_size:       u16 = 2
# + resource_cnt:      u16 = 2
# + delegation_cap:    u32 = 4
# + pd_id:             u64 = 8
#
# Followed by resource_cnt resource records.
#
def parse_delegator(bundle_data, off):
    record_base = off
    header_format = "<HHIQ"
    header_size = struct.calcsize(header_format)

    if off + header_size > len(bundle_data):
        raise ValueError(
            f"truncated delegator header at offset {off:#x}"
        )

    record_size, resource_cnt, delegation_cap, pd_id = struct.unpack_from(
        header_format,
        bundle_data,
        off,
    )
    off += header_size

    record_end = record_base + record_size
    if record_end > len(bundle_data):
        raise ValueError(
            f"delegator record at {record_base:#x} extends past "
            f"the end of the bundle"
        )

    resources = []
    for _ in range(resource_cnt):
        resource, off = parse_resource(bundle_data, off)
        resources.append(resource)

    if off != record_end:
        raise ValueError(
            f"delegator record size mismatch at {record_base:#x}: "
            f"parsed to {off:#x}, expected {record_end:#x}"
        )

    return {
        "record_size": record_size,
        "resource_cnt": resource_cnt,
        "delegation_cap": delegation_cap,
        "pd_id": pd_id,
        "resources": resources,
    }, off


#
# Resource format:
#
# + kind:          u8  = 1
# + flags:         u8  = 1
# + slot:          u16 = 2
# + cap_count:     u8  = 1
# + arg0:          u64 = 8
# + arg1:          u64 = 8
#
def parse_resource(bundle_data, off):
    header_format = "<BBHBQQ"
    resource_size = struct.calcsize(header_format)

    if off + resource_size > len(bundle_data):
        raise ValueError(
            f"truncated resource record at offset {off:#x}"
        )

    kind, flags, slot, cap_count, arg0, arg1 = struct.unpack_from(
        header_format,
        bundle_data,
        off,
    )
    off += resource_size

    if kind not in RESOURCE_NAMES:
        raise ValueError(
            f"unknown resource kind {kind} at "
            f"offset {off - resource_size:#x}"
        )

    return {
        "kind": kind,
        "flags": flags,
        "slot": slot,
        "cap_count": cap_count,
        "arg0": arg0,
        "arg1": arg1,
    }, off


def parse_delegation_bundle(bundle_path: Path):
    bundle_data = bundle_path.read_bytes()
    delegator_cnt, off = parse_header(bundle_data)

    delegators = []
    for _ in range(delegator_cnt):
        delegator, off = parse_delegator(bundle_data, off)
        delegators.append(delegator)

    if off != len(bundle_data):
        raise ValueError(
            f"bundle contains {len(bundle_data) - off} trailing bytes "
            f"after offset {off:#x}"
        )

    return bundle_data, delegators


def print_resource(resource):
    kind = resource["kind"]
    name = RESOURCE_NAMES[kind]
    slot = resource["slot"]
    cap_count = resource["cap_count"]
    arg0 = resource["arg0"]
    arg1 = resource["arg1"]

    if cap_count == 1:
        caps = f"{slot}"
    else:
        caps = f"{slot}..{slot + cap_count - 1}"

    if kind == RESOURCE_MEMORY_REGION:
        rights = resource["flags"] & 0x7
        cached = bool(resource["flags"] & (1 << 3))

        print(
            f"    {name}: caps={caps}, "
            f"vaddr={arg0:#x}, page_size={arg1:#x}, "
            f"rights={rights:#x}, cached={cached}"
        )
    elif kind == RESOURCE_IOPORT:
        print(
            f"    {name}: cap={slot}, "
            f"addr={arg0:#x}, size={arg1:#x}"
        )
    else:
        print(f"    {name}: cap={slot}")


def print_bundle(bundle_path: Path, bundle_data, delegators):
    print(f"Delegation bundle: {bundle_path}")
    print(f"Size: {len(bundle_data)} bytes")
    print(f"Delegators: {len(delegators)}")

    for i, delegator in enumerate(delegators):
        print()
        print(
            f"[{i}] pd_id={delegator['pd_id']}, "
            f"delegation_cap={delegator['delegation_cap']}, "
            f"resources={delegator['resource_cnt']}, "
            f"record_size={delegator['record_size']}"
        )

        for resource in delegator["resources"]:
            print_resource(resource)

    print()
    print("Bundle validation passed.")


def main():
    parser = ArgumentParser()
    parser.add_argument(
        "bundle",
        type=Path,
        help="Microkit delegation bundle (.dlg)",
    )
    args = parser.parse_args()

    try:
        bundle_data, delegators = parse_delegation_bundle(args.bundle)
        print_bundle(args.bundle, bundle_data, delegators)
    except (OSError, ValueError, struct.error) as err:
        parser.error(str(err))


if __name__ == "__main__":
    main()
