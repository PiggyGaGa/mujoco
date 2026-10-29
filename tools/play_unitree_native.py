"""Run mjlab's play CLI with the legacy Unitree G1 task registered."""

import argparse
from pathlib import Path
import sys

from unitree_g1_compat import load_task


def main():
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument("--task-source", type=Path, required=True)
    args, remaining = parser.parse_known_args()
    load_task(args.task_source)
    from mjlab.scripts.play import main as play_main

    sys.argv = ["play", *remaining]
    play_main()


if __name__ == "__main__":
    main()
