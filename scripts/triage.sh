#!/bin/bash
# ============================================================
# Project : Hospital Patient Triage & Bed Allocator
# Script  : triage.sh
 # Group   : Group 08
 # Members : TALHA (24F-0793) , KHIZAR (24F0812)
 # Date    : 2026-05-01
# Purpose : Validate input, compute priority, write plain text
#           record to TRIAGE_FIFO. Pure bash — no Python.
# Usage   : ./scripts/triage.sh <name> <age> <severity 1-10> [infectious 0|1]
# ============================================================

TRIAGE_FIFO="/tmp/triage_fifo"

# ── Argument check ────
if [ $# -lt 3 ]; then
    echo "Usage: $0 <name> <age> <severity 1-10> [infectious: 0|1]"
    echo "Example: $0 \"Ali Raza\" 28 9 0"
    exit 1
fi

NAME="$1"
AGE="$2"
SEVERITY="$3"
INFECTIOUS="${4:-0}"

# ── Validate name ────
if [ -z "$NAME" ]; then
    echo "ERROR: Name cannot be empty."
    exit 1
fi

# ── Validate age ───
if ! [[ "$AGE" =~ ^[0-9]+$ ]]; then
    echo "ERROR: Age must be a positive integer. Got: '$AGE'"
    exit 1
fi
if [ "$AGE" -lt 1 ] || [ "$AGE" -gt 120 ]; then
    echo "ERROR: Age must be between 1 and 120. Got: $AGE"
    exit 1
fi

# ── Validate severity ──
if ! [[ "$SEVERITY" =~ ^[0-9]+$ ]]; then
    echo "ERROR: Severity must be a number. Got: '$SEVERITY'"
    exit 1
fi
if [ "$SEVERITY" -lt 1 ] || [ "$SEVERITY" -gt 10 ]; then
    echo "ERROR: Severity must be between 1 and 10. Got: $SEVERITY"
    exit 1
fi

# ── Validate infectious flag ──
if [ "$INFECTIOUS" != "0" ] && [ "$INFECTIOUS" != "1" ]; then
    echo "ERROR: Infectious must be 0 or 1. Got: '$INFECTIOUS'"
    exit 1
fi

# ── Compute triage priority from severity ──
# Severity 9-10 -> Priority 1 (Critical  -> ICU)
# Severity 7-8  -> Priority 2 (Urgent    -> ICU)
# Severity 5-6  -> Priority 3 (Moderate  -> Isolation)
# Severity 3-4  -> Priority 4 (Non-urgent-> General)
# Severity 1-2  -> Priority 5 (Minor     -> General)
if   [ "$SEVERITY" -ge 9 ]; then PRIORITY=1
elif [ "$SEVERITY" -ge 7 ]; then PRIORITY=2
elif [ "$SEVERITY" -ge 5 ]; then PRIORITY=3
elif [ "$SEVERITY" -ge 3 ]; then PRIORITY=4
else                              PRIORITY=5
fi

# ── Print triage summary
echo "┌─────────────────────────────┐"
echo "│      TRIAGE ASSESSMENT      │"
echo "├─────────────────────────────┤"
printf "│  Name       : %-14s │\n" "$NAME"
printf "│  Age        : %-14s │\n" "$AGE"
printf "│  Severity   : %-14s │\n" "$SEVERITY/10"
printf "│  Priority   : %-14s │\n" "$PRIORITY"
printf "│  Infectious : %-14s │\n" "$INFECTIOUS"
echo "└─────────────────────────────┘"

# ── Check FIFO exists 
if [ ! -p "$TRIAGE_FIFO" ]; then
    echo "ERROR: Triage FIFO not found at $TRIAGE_FIFO"
    echo "Is the hospital running? Try: make run"
    exit 1
fi

# ── Write plain text line to FIFO
# Format: NAME|AGE|SEVERITY|PRIORITY|INFECTIOUS
# admissions.c reads this with: sscanf(line, "%63[^|]|%d|%d|%d|%d", ...)
# Pure bash echo — no Python, no external tools needed.
echo "${NAME}|${AGE}|${SEVERITY}|${PRIORITY}|${INFECTIOUS}" > "$TRIAGE_FIFO"

if [ $? -eq 0 ]; then
    echo "[TRIAGE] Patient '$NAME' sent to admissions. ✓"
else
    echo "[TRIAGE] ERROR: Failed to write to FIFO."
    exit 1
fi
