"""PyInstaller entry point for edge-tts edge-tts.

Delegates to edge_tts.util.main() which handles CLI argument parsing
and TTS synthesis. This wrapper exists solely as a stable script path
for PyInstaller --onedir packaging.
"""

import sys
from edge_tts.util import main

if __name__ == "__main__":
    sys.exit(main())
