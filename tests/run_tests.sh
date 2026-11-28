#!/usr/bin/env bash
# Builds everything and runs the unit tests and the 5-client integration test.
set -e
cd "$(dirname "$0")/.."
make all
make test
