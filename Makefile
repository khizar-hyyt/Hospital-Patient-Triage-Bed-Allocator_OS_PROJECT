# ============================================================
# Project : Hospital Patient Triage & Bed Allocator
# File    : Makefile
#  Group   : Group 08
# Members : TALHA (24F-0793) , KHIZAR (24F0812)
# Date    : 2026-05-01
# ============================================================

CC     = gcc
CFLAGS = -Wall -Wextra -g -pthread
SRC    = src

# Default target — build both binaries
all: admissions patient_simulator
	@echo "Build complete. Run: make run"

admissions: $(SRC)/admissions.c $(SRC)/hospital.h
	$(CC) $(CFLAGS) -I$(SRC) -o admissions $(SRC)/admissions.c -lpthread
	@echo "  [OK] admissions"

patient_simulator: $(SRC)/patient_simulator.c $(SRC)/hospital.h
	$(CC) $(CFLAGS) -I$(SRC) -o patient_simulator $(SRC)/patient_simulator.c
	@echo "  [OK] patient_simulator"

# Start the hospital
run: all
	@chmod +x scripts/*.sh
	@mkdir -p logs
	@./scripts/start_hospital.sh

# Build + start + 3 quick test patients
test: all
	@chmod +x scripts/*.sh
	@mkdir -p logs
	@./scripts/start_hospital.sh
	@echo "[TEST] Waiting 2s for hospital to start..."
	@sleep 2
	@echo "[TEST] Sending 3 test patients..."
	@./scripts/triage.sh "Critical Ali"  30 10 0
	@sleep 1
	@./scripts/triage.sh "Moderate Sara" 25  6 1
	@sleep 1
	@./scripts/triage.sh "Minor Zain"    40  2 0
	@echo "[TEST] Done. Watch admissions output."

# 20-patient stress test
stress: all
	@chmod +x scripts/stress_test.sh
	@./scripts/stress_test.sh

# Stop and clean up
stop:
	@./scripts/stop_hospital.sh

# Remove compiled binaries and FIFOs
clean:
	rm -f admissions patient_simulator
	rm -f /tmp/triage_fifo /tmp/discharge_fifo
	rm -f /tmp/hospital.pid
	@echo "Cleaned."

.PHONY: all run test stress stop clean

