#!/usr/bin/env bash
# Build host unit tests, run them, and emit an HTML coverage report.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT/tests"

make coverage-html

if command -v xdg-open >/dev/null 2>&1; then
    xdg-open "$ROOT/tests/coverage_html/index.html" >/dev/null 2>&1 || true
elif command -v open >/dev/null 2>&1; then
    open "$ROOT/tests/coverage_html/index.html" || true
fi

echo "Done. Open: $ROOT/tests/coverage_html/index.html"
