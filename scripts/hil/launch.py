"""Isolated Python entry point; only import code from this reviewed installation."""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from run import main

if __name__ == '__main__':
    raise SystemExit(main())
