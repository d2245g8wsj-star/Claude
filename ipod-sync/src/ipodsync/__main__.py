"""Start de app: ``python -m ipodsync`` of ``ipod-sync``."""

import sys


def main() -> None:
    try:
        import tkinter  # noqa: F401
    except ImportError:
        sys.exit(
            "Tkinter ontbreekt. Op de Mac met Homebrew: brew install python-tk\n"
            "(of gebruik de Python-installer van python.org, daar zit het al in)."
        )
    from .gui.app import main as run

    run()


if __name__ == "__main__":
    main()
