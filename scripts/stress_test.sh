#!/bin/bash
# ============================================================
# Project : Hospital Patient Triage & Bed Allocator
# Script  : stress_test.sh
# Group   : Group 08
# Members : TALHA (24F-0793) , KHIZAR (24F0812)
# Date    : 2026-05-01
# Purpose : Send 20 patients rapidly. Tests concurrency,
#           semaphore blocking, and queue ordering.
# Usage   : ./scripts/stress_test.sh
# ============================================================

TRIAGE="./scripts/triage.sh"

echo "[STRESS] Starting 20-patient stress test..."
echo ""

# 20 patient records: name, age, severity, infectious
NAMES=(
    "Ali Raza"        "Sara Khan"      "Usman Ahmed"    "Hina Malik"
    "Bilal Shah"      "Nida Fatima"    "Hamza Tariq"    "Sana Butt"
    "Zain Mirza"      "Ayesha Noor"    "Kamran Iqbal"   "Maria Siddiqui"
    "Faisal Mahmood"  "Rabia Anwar"    "Talha Qureshi"  "Zara Hussain"
    "Imran Sheikh"    "Lubna Abbas"    "Adeel Chaudhry" "Mehwish Rana"
)

for i in $(seq 0 19); do
    NAME="${NAMES[$i]}"
    AGE=$((18 + RANDOM % 60))
    SEVERITY=$((1 + RANDOM % 10))
    INFECTIOUS=$((RANDOM % 2))

    echo "[STRESS] [$((i+1))/20] $NAME | age=$AGE sev=$SEVERITY inf=$INFECTIOUS"
    bash "$TRIAGE" "$NAME" "$AGE" "$SEVERITY" "$INFECTIOUS" &

    # Short random delay so not all arrive at exactly the same time
    sleep "0.$((2 + RANDOM % 4))"
done

# Wait for all background triage calls to finish
wait
echo ""
echo "[STRESS] All 20 patients submitted."
echo "[STRESS] Watch the admissions window for processing output."
