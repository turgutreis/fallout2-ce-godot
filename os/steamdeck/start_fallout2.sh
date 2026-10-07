#!/bin/bash
cd "$(dirname "$0")"
chmod +x ./fallout2-ce
exec ./fallout2-ce "$@"
