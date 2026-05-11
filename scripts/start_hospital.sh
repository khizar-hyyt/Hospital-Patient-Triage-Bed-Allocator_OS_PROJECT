#!/bin/bash
# ============================================================
# Project : Hospital Patient Triage & Bed Allocator
# Script  : start_hospital.sh
# Group   : Group 08
# Date    : 2026-05-01
# Purpose : Launch the admissions manager in the background.
# Usage   : ./scripts/start_hospital.sh [--strategy best|first|worst]
# ============================================================

ADMISSIONS="./admissions"
PID_FILE="/tmp/hospital.pid"

echo "╔══════════════════════════════════════════╗"
echo "║  HOSPITAL PATIENT TRIAGE & BED ALLOCATOR ║"
echo "╠══════════════════════════════════════════╣"
echo "║  ICU Beds       :  4  (3 care units each)║"
echo "║  Isolation Beds :  4  (2 care units each)║"
echo "║  General Beds   : 12  (1 care unit  each)║"
echo "║  Total          : 20 beds / 32 care units║"
echo "╚══════════════════════════════════════════╝"

# Check binary exists
if [ ! -f "$ADMISSIONS" ]; then
    echo "ERROR: $ADMISSIONS not found. Run 'make' first."
    exit 1
fi

# Already running?
if [ -f "$PID_FILE" ]; then
    PID=$(cat "$PID_FILE")
    if kill -0 "$PID" 2>/dev/null; then
        echo "Hospital already running (PID=$PID)."
        exit 0
    fi
fi

# Create logs directory and clear old logs
mkdir -p logs
> logs/memory_log.txt
> logs/schedule_log.txt

echo "[START] Launching admissions manager..."

# Launch with optional strategy
if [ "$1" = "--strategy" ] && [ -n "$2" ]; then
    "$ADMISSIONS" --strategy "$2" &
else
    "$ADMISSIONS" &
fi

APID=$!
echo "$APID" > "$PID_FILE"

# Give it a second to initialise , sleep 1
sleep 1

if kill -0 "$APID" 2>/dev/null; then
    echo "[START] Admissions manager running (PID=$APID)"
    echo "[START] Hospital OPEN. Send patients with:"
    echo "        ./scripts/triage.sh <name> <age> <severity>"
else
    echo "[ERROR] Admissions manager failed to start."
    rm -f "$PID_FILE"
    exit 1
fi
