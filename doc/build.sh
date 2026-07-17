#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"

FONTS=vendor/dual-typst/assets/fonts
PORT="${PORT:-8000}"

# Render cetz figures to SVG assets first (version-proof, no html.frame needed).
typst compile --root . --font-path "$FONTS" figures/lightbar.typ assets/lightbar.svg

# HTML, emitted as index.html so GitHub Pages serves it at the repo root.
# Videos under assets/ are referenced relatively and served from this dir.
compile() {
  typst compile --root . --font-path "$FONTS" \
    --features html --input target=html \
    main.typ index.html
}

if [[ "${1:-}" == "watch" ]]; then
  # typst's own HTML server (ports 3000-3005) serves only the document, not
  # sibling files, so anything under assets/ 404s there. Disable it with
  # --no-serve and serve the whole doc dir ourselves so index.html + assets/ load.
  typst watch --no-serve --root . --font-path "$FONTS" \
    --features html --input target=html \
    main.typ index.html &
  WATCH_PID=$!
  trap 'kill $WATCH_PID 2>/dev/null' EXIT
  echo "Serving http://localhost:$PORT"
  python3 -m http.server "$PORT"
else
  compile
  echo "Built: $(pwd)/index.html"
fi
