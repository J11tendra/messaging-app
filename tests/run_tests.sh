#!/usr/bin/env bash
set -e

echo "=== Running Multi-User Messaging Test Suite ==="

# Build everything
make clean
make all

echo ""
echo "=== Step 1: Running Database Unit Tests ==="
./bin/test_db

echo ""
echo "=== All Tests Completed Successfully ==="
