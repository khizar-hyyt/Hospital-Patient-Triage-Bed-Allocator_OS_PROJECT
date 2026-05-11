#!/bin/bash
# ============================================================
# Project : Hospital Patient Triage & Bed Allocator
# Script  : stop_hospital.sh
# Group   : Group 08
# Members : TALHA (24F-0793) , KHIZAR (24F0812)
# Date    : 2026-05-01
# Purpose : Stop admissions, clean up all IPC resources,
#           print final ward summary.
# Usage   : ./scripts/stop_hospital.sh
# ============================================================

PID_FILE="/tmp/hospital.pid"

echo "[STOP] Shutting down hospital..."

# ── Stop admissions process ──
if [ -f "$PID_FILE" ]; then
    PID=$(cat "$PID_FILE")
    if kill -0 "$PID" 2>/dev/null; then
        echo "[STOP] Sending SIGTERM to admissions (PID=$PID)..."
        kill -SIGTERM "$PID"
        # Wait up to 8 seconds for clean exit
        for i in $(seq 1 8); do
            sleep 1
            if ! kill -0 "$PID" 2>/dev/null; then
                echo "[STOP] Admissions stopped cleanly."
                break
            fi
        done
        # Force kill if still alive
        if kill -0 "$PID" 2>/dev/null; then
            echo "[STOP] Force killing PID $PID..."
            kill -9 "$PID"
        fi
    else
        echo "[STOP] PID $PID is not running."
    fi
    rm -f "$PID_FILE"
else
    echo "[STOP] No PID file found. Hospital may not be running."
fi

# ── Clean up shared memory ───
echo "[STOP] Cleaning shared memory..."
# ipcs -m lists shared memory; awk finds our key
SHM_ID=$(ipcs -m | awk '$1 == "0xbedf00d" {print $2}')
if [ -n "$SHM_ID" ]; then
    ipcrm -m "$SHM_ID" && echo "[STOP] Shared memory $SHM_ID removed."
else
    echo "[STOP] No shared memory found for key 0xbedf00d."
fi

# ── Remove FIFOs ────
echo "[STOP] Removing FIFOs..."
rm -f /tmp/triage_fifo /tmp/discharge_fifo

# ── Final summary ────
echo ""
echo "════════ FINAL SUMMARY ════════"
echo "── Memory Log (last 10 lines) ──"
tail -n 10 logs/memory_log.txt 2>/dev/null || echo "(no memory log)"
echo ""
echo "── Schedule Log (last 8 lines) ──"
tail -n 8  logs/schedule_log.txt 2>/dev/null || echo "(no schedule log)"
echo "════════════════════════════════"
echo "[STOP] Hospital closed."
