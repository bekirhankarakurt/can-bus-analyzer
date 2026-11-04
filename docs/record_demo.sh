#!/usr/bin/env bash
# Re-records docs/demo.gif from a real run. Needs a built tree in ./build,
# asciinema (pip install asciinema) and agg (https://github.com/asciinema/agg).
set -euo pipefail
cd "$(dirname "$0")/.."
asciinema rec --overwrite --cols 132 --rows 30 -c "bash docs/demo.sh" docs/demo.cast
agg --fps-cap 8 --speed 1.2 --font-size 15 docs/demo.cast docs/demo.gif
