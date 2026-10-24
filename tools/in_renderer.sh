#!/usr/bin/env bash
# Run a command in the toolkit's browser-renderer image (mpc-vst-html-art: headless Chromium, Pillow), on this repo.
#   docker build -q -t mpc-vst-html-art external/mpc-vst-plugins/tools/html_art   (once)
#   tools/in_renderer.sh python3 external/mpc-vst-plugins/tools/gen_vst.py vst/vst.json
exec docker run --rm -u "$(id -u):$(id -g)" -e HOME=/tmp -v "$PWD":/w -w /w mpc-vst-html-art "$@"
