#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#ifndef HOSPITAL_H
#define HOSPITAL_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <pthread.h>
#include <semaphore.h>

/* ── Ward bed counts ─── */
#define ICU_COUNT            4
#define ISOLATION_COUNT      4
#define GENERAL_COUNT        12
#define TOTAL_BEDS           20

/* ── Care units each bed type consumes ─── */
#define ICU_CARE_UNITS        3
#define ISOLATION_CARE_UNITS  2
#define GENERAL_CARE_UNITS    1

/* Total = (4×3)+(4×2)+(12×1) = 32 */
#define TOTAL_CARE_UNITS      32

/* ── Paging simulation ── */
#define PAGE_SIZE             2
#define TOTAL_PAGES           16   /* 32 / 2 */

/* ── System limits ──── */
#define MAX_PARTITIONS        40
#define MAX_QUEUE_SIZE        50
#define MAX_PATIENTS          100

/* ── IPC paths and keys, bedfood as shmkey ────── */
#define SHM_KEY               0xBEDF00D
#define TRIAGE_FIFO           "/tmp/triage_fifo"
#define DISCHARGE_FIFO        "/tmp/discharge_fifo"
#define SEM_ICU               "/sem_icu_limit"
#define SEM_ISO               "/sem_iso_limit"
#define SEM_QUEUE_FULL        "/sem_queue_full"

/* ── Allocation strategy codes ──── */
#define STRATEGY_BEST         0
#define STRATEGY_FIRST        1
#define STRATEGY_WORST        2

/*
 * PatientRecord
 * Core data structure for one patient.
 * - Created by receptionist thread after parsing triage FIFO text.
 * - Passed binary to patient_simulator via anonymous pipe.
 * - Stored in history[] for scheduling simulation.
 */
typedef struct {
    int    patient_id;
    char   name[64];
    int    age;
    int    severity;           /* raw 1-10 from triage.sh        */
    int    priority;           /* computed 1-5 triage priority    */
    int    care_units;         /* memory units needed             */
    int    infectious;         /* 1 = infectious flag set         */
    char   bed_type[16];       /* "ICU" / "ISOLATION" / "GENERAL" */
    int    assigned_bed;       /* partition index assigned        */
    time_t arrival_time;
    time_t start_time;
    time_t finish_time;
    int    treatment_duration;
} PatientRecord;

/*
 * BedPartition
 * One slot in the contiguous ward memory model.
 */
typedef struct {
    int  partition_id;
    int  start_unit;   /* starting index in ward_array */
    int  size;         /* number of care units         */
    int  is_free;      /* 1 = FREE, 0 = OCCUPIED       */
    int  patient_id;   /* -1 if free                   */
    char bed_type[16];
} BedPartition;

/*
 * DischargeMsg
 * Written by patient_simulator → DISCHARGE_FIFO as binary.
 * Two ints only — no alignment issues.
 */
typedef struct {
    int patient_id;
    int duration;
} DischargeMsg;

/*
 * WardSharedMem
 * The entire shared memory segment layout.
 * Created and managed by admissions; readable by children.
 */
typedef struct {
    int          ward_array[TOTAL_CARE_UNITS]; /* -1=free, else patient_id */
    BedPartition partitions[MAX_PARTITIONS];
    int          num_partitions;
    int          total_served;
    int          page_table[TOTAL_PAGES];      /* paging simulation        */
} WardSharedMem;

#endif /* HOSPITAL_H */
