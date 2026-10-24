#!/usr/bin/env bash
# CI helper: run a command, keep its output, and surface it as annotations so a failure is readable from the
# run's summary (and through the API) without opening the raw log.
#   tools/ci_run.sh "<title>" <command...>
# On failure: every line that looks like an error, then the last 25 lines, as ::error annotations.
# Lines the command prints starting with "NOTE:" become ::notice annotations either way.
title="$1"; shift
log=$(mktemp)
set +e
"$@" 2>&1 | tee "$log"
code=${PIPESTATUS[0]}
set -e
grep -E '^NOTE:' "$log" | sed "s/^NOTE: */::notice title=$title::/" || true
if [ "$code" -ne 0 ]; then
  grep -E 'error|Error|ERROR|FAIL|undefined reference|No such file' "$log" | grep -v '^::' | head -40 \
    | while IFS= read -r line; do echo "::error title=$title::${line:0:900}"; done
  tail -25 "$log" | while IFS= read -r line; do echo "::error title=$title (tail)::${line:0:900}"; done
fi
rm -f "$log"
exit "$code"
