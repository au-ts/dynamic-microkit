# Copyright 2026, UNSW
# SPDX-License-Identifier: BSD-2-Clause

import sys
import argparse
import importlib
from pathlib import Path

from sdfgen import SystemDescription, Sddf


SDF = SystemDescription
PD = SDF.ProtectionDomain
MR = SDF.MemoryRegion
MAP = SDF.Map
CH = SDF.Channel

SHARED_SIZE = 0x200000


def load_boards(sddf_root: str):
    meta_dir = Path(sddf_root).resolve() / "tools" / "meta"
    sys.path.insert(0, str(meta_dir))
    board_mod = importlib.import_module("board")
    return getattr(board_mod, "BOARDS")


def generate(sdf: SDF, sdf_path: str):
    shared = MR(sdf, "shared", SHARED_SIZE)
    sdf.add_mr(shared)

    loadee = PD("loadee", priority=20, template=True, sym_emit=True)
    loadee.add_map(MAP(shared, 0x20000, perms="rwx", cached=True))

    loader = PD("loader", "loader.elf", priority=25)
    loader.add_map(MAP(shared, 0xC00000, perms="rw", cached=True))

    channel = CH(loader, loadee, a_id=1, b_id=1)

    loader.add_child_pd(loadee)

    sdf.add_pd(loader)
    sdf.add_channel(channel)

    with open(sdf_path, "w+") as f:
        f.write(sdf.render())


if __name__ == "__main__":
    board_parser = argparse.ArgumentParser(add_help=False)
    board_parser.add_argument("--sddf", required=True)
    board_args, _ = board_parser.parse_known_args()

    sddf = Sddf(board_args.sddf)
    BOARDS = load_boards(board_args.sddf)

    parser = argparse.ArgumentParser(parents=[board_parser])
    parser.add_argument("--board", required=True, choices=[b.name for b in BOARDS])
    parser.add_argument("--sdf", required=True)

    args = parser.parse_args()

    board = next(b for b in BOARDS if b.name == args.board)
    sdf = SDF(board.arch, board.paddr_top)

    generate(sdf, args.sdf)