#!/usr/bin/env bash
# Пересобирает docs/Comma-manual.pdf и картинки README: свежие скриншоты из теста → нарезка → печать HTML в PDF.
# Нужны: Python 3 с Pillow, Node.js с Playwright. На Linux тест запускается через xvfb-run.
set -euo pipefail
cd "$(dirname "$0")/../.."

TEST=./build/CommaTest_artefacts/Release/CommaTest
SHOTS=$(mktemp -d)
trap 'rm -rf "$SHOTS"' EXIT

[ -d build ] || cmake -B build -DCOMMA_BUILD_TESTS=ON
cmake --build build --target CommaTest
RUN=()
if [[ "$(uname)" == "Linux" ]]; then RUN=(xvfb-run -a); fi
"${RUN[@]}" "$TEST" "$SHOTS" | tail -1

mkdir -p docs/manual/img docs/assets
python3 docs/manual/crop.py "$SHOTS" docs/manual/img docs/assets
NODE_PATH="${NODE_PATH:-$(npm root -g)}" node docs/manual/print.js docs/manual/Comma-manual.html docs/Comma-manual.pdf
echo "docs/Comma-manual.pdf: $(python3 -c 'import os; print(os.path.getsize("docs/Comma-manual.pdf") // 1024)') KB"
