# Hospital Patient Triage & Bed Allocator
**CL2006 Operating Systems Lab | FAST-NUCES CFD | Spring 2026**
#Muhammad Talha 24F0793
#KHIZAR HAYAT 24F0812

## How to Build and Run

```bash
# 1. Build
make all

# 2. Start hospital (Terminal 1)
cd ~/hospital
make run

# 3. Send a patient (Terminal 2)
./scripts/triage.sh "Ali Raza" 28 9 0

# 4. Stress test (20 patients)
make stress

# 5. Stop hospital
make stop
```

## COMPLETE STEP BY STEP HOSPITAL FUNCTIONS (PROJECT) RUN TUTORIAL (SKIPPABLE)
Download the Zip file in Ubuntu with Firefox or copy-paste from Windows to Ubuntu



STEP 1 — Open Terminal
Press Ctrl + Alt + T

A black window opens.

STEP 2 — Install GCC and Make
Type this exactly and press Enter:

bash
sudo apt install gcc make -y


#1. Clean Start

cd ~/hospital
make clean



#2. Build Project
Compile both programs:
make all



#3. Project Structure (skippable)

VERIFY all files and folders:

find . -type f | sort
ls -la src/ scripts/ logs/
4. Start Hospital (Phase 1)

Start live log monitor to check:

cd ~/hospital
watch -n 1 'echo "=== MEMORY LOG ===" && tail -20 logs/memory_log.txt && echo "" && echo "=== SCHEDULE LOG ===" && tail -10 logs/schedule_log.txt'

Then start hospital:

cd ~/hospital
make run



#5. Verify IPC Resources (skippable)

Check shared memory, FIFOs, and semaphores:

ipcs -m
ls -la /tmp/triage_fifo /tmp/discharge_fifo
ls /dev/shm/ | grep sem


#6. Admit First Patient (Critical ICU)

Send ICU patient:

cd ~/hospital
./scripts/triage.sh "Ali Raza" 28 10 0

Show full lifecycle from reception → scheduling → allocation → simulator start.

#7. Admit Isolation Patient

Send isolation case:

./scripts/triage.sh "Sara Khan" 45 6 1


#8. Admit General Ward Patient

Send general patient:

./scripts/triage.sh "Zain Ahmed" 33 2 0


#9. Priority Queue Demo

Send low priority then critical rapidly:

./scripts/triage.sh "Minor Patient" 25 1 0
sleep 0
./scripts/triage.sh "Critical Patient" 50 10 0

Show critical admitted before minor.

#10. Semaphore Blocking Demo

Fill ICU beds:

./scripts/triage.sh "ICU Patient A" 30 10 0
sleep 0.3
./scripts/triage.sh "ICU Patient B" 35 9 0
sleep 0.3
./scripts/triage.sh "ICU Patient C" 40 10 0
sleep 0.3
./scripts/triage.sh "ICU Patient D" 45 9 0
sleep 0.3
./scripts/triage.sh "ICU Patient E" 50 10 0



#11. Nurse Thread Activity

nurse status lines updating automatically.

#12. Ward Map Evolution

Scroll terminal to check multiple MAP updates over time.

#13. Coalescing Demo

Wait for adjacent discharges and show merge event.

#14. Allocation Strategies (skippable)

Stop hospital:

make stop

Run First-Fit:

./admissions --strategy first &
sleep 1
./scripts/triage.sh "FirstFit Test" 30 8 0

Run Worst-Fit:

kill %1
./admissions --strategy worst &
sleep 1
./scripts/triage.sh "WorstFit Test" 30 8 0

Restore Best-Fit:

kill %1
make run

#15. Stress Test

Submit 20 patients:
make stress



#16. Show Memory Log 

cat logs/memory_log.txt
#17. Graceful Shutdown + Schedule Log

Shutdown:

make stop

cat logs/schedule_log.txt
#18. Final Stop Script Summary


#19. Valgrind Leak Check

Run:

cd ~/hospital
valgrind --leak-check=full --track-origins=yes ./admissions &
VPID=$!
sleep 2
./scripts/triage.sh "Valgrind Test" 30 7 0
sleep 15
kill -SIGTERM $VPID
wait $VPID


#20. Show Makefile Targets

#Demonstrate all commands:

make clean

make all

make test

make stop

===========================================

## triage.sh Usage
./scripts/triage.sh <name> <age> <severity 1-10> [infectious 0|1]

| Severity | Priority | Category  | Bed Type  |
|----------|----------|-----------|-----------|
| 9–10     | 1        | Critical  | ICU       |
| 7–8      | 2        | Urgent    | ICU       |
| 5–6      | 3        | Moderate  | Isolation |
| 3–4      | 4        | Non-urgent| General   |
| 1–2      | 5        | Minor     | General   |

## Allocation Strategy

```bash
./admissions --strategy best    # Best-Fit  (default)
./admissions --strategy first   # First-Fit
./admissions --strategy worst   # Worst-Fit
```

## Dependencies
- gcc
- Standard Linux POSIX libraries

## Logs
- `logs/memory_log.txt`  — fragmentation after each event
- `logs/schedule_log.txt`— FCFS / Priority / SJF simulation


============================================================
  OS CONCEPTS DEMONSTRATED
============================================================

  fork() + execl()     Each patient spawns a child process
  Anonymous Pipe       PatientRecord passed to child via pipe
  Named FIFO           triage.sh to admissions (triage_fifo)
                       patient_simulator to admissions (discharge_fifo)
  Shared Memory        Bed bitmap at key 0xBEDF00D
  POSIX Threads        Receptionist, Scheduler, 3 Nurse threads
  Mutex                bed_mutex protects bed bitmap
  Condition Variable   bed_freed wakes scheduler on discharge
  Semaphores           sem_icu and sem_iso enforce capacity limits
  Best-Fit Allocator   Minimum waste partition selection
  Coalescing           Adjacent free partitions merged after discharge
  CPU Scheduling Sim   FCFS, Priority, SJF with avg metrics

