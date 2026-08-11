#!/usr/bin/env python3

# Copyright 2026, UNSW
#
# SPDX-License-Identifier: BSD-2-Clause

"""Patch a payload ELF with symbols emitted by the Microkit tool."""

from argparse import ArgumentParser
from pathlib import Path
import struct

import lief

#
# Symb header format:
#   + magic: MKTSYMB\0, u8 [8] = 8
#   + pd_name_len:      u16    = 2
#   + symbol_cnt:       u32    = 4
#   + pd_name: [pd_name_len]
#
def parse_symbol_bundle(bundle_path: Path):
    bundle_data = bundle_path.read_bytes()

    magic_len = len("MKTSYMB\0")
    header_format = "<HI"
    pd_name_len, symbol_cnt = struct.unpack_from(header_format,
                                                 bundle_data,
                                                 magic_len
                                                )
    pd_name_base = magic_len + struct.calcsize(header_format)
    pd_name_end = pd_name_base + pd_name_len
    pd_name = bundle_data[pd_name_base:pd_name_end].decode()

    # parse all symbols
    symbols = []
    symbols_base = pd_name_end
    off = symbols_base
    #
    # Symb struct:
    #   + symbol_len:    u16
    #   + setvar_flag:   u16
    #   + data_len:      u32
    #   + expected_size: u64 (for setvar)
    #   + symbol: [symbol_len]
    #   + data: [data_len]
    #
    for _ in range(symbol_cnt):
        header_format = "<HHIQ"
        symbol_len, _, data_len, _ = struct.unpack_from(header_format,
                                                        bundle_data,
                                                        off
                                                    )
        off += struct.calcsize(header_format)

        # Retrieve symbol (e.g., microkit_name) from bundle
        symbol = bundle_data[off:off + symbol_len].decode()
        off += symbol_len

        # Retrieve symbol data (e.g., "server" for microkit_name) from bundle
        data = bundle_data[off:off + data_len]
        off += data_len

        symbols.append((symbol, data))

    return pd_name, symbols


def materialise_segments(elf):
    for segment in list(elf.segments):
        if segment.type != lief.ELF.Segment.TYPE.LOAD:
            continue
        # For regions that are (partially) initialised...
        # e.g., .bss, microkit_name[x], ...
        if segment.physical_size < segment.virtual_size:
            elf.extend(segment, segment.virtual_size - segment.physical_size)


def patch_payload(input_path: Path, output_path: Path, symbols):
    elf = lief.ELF.parse(input_path)

    # Materialise zero-filled ELF segments,
    # so that patch_address() can update symbols in .bss.
    materialise_segments(elf)

    # Initialise the symbolic table of the given elf
    symbs_name_to_vaddr = {}
    for s in elf.symtab_symbols:
        # s.value are Elf64_Sym.st_value,
        # which represents vaddr for symbols.
        symbs_name_to_vaddr[s.name] = s.value

    for symbol_name, symbol_data in symbols:
        symbol_vaddr = symbs_name_to_vaddr[symbol_name]
        elf.patch_address(symbol_vaddr, list(symbol_data))
        print(f"Patch '{symbol_name}' at {symbol_vaddr:#x} with {symbol_data.hex(' ')}")

    elf.write(output_path)


def main():
    parser = ArgumentParser()
    parser.add_argument("input",   type=Path, help="payload raw (elf)")
    parser.add_argument("symbols", type=Path, help="microkit symbol bundle")
    parser.add_argument("output",  type=Path, help="payload finalised")
    args = parser.parse_args()

    pd_name, symbols = parse_symbol_bundle(args.symbols)

    print(f"Patching payload for protection domain '{pd_name}'")
    patch_payload(args.input, args.output, symbols)


if __name__ == "__main__":
    main()
