/*
 * ============================================================
 * Project : Hospital Patient Triage & Bed Allocator
 * File    : admissions.c
 * Group   : Group 08
 * Members : TALHA (24F-0793) , KHIZAR (24F0812)
 * Date    : 2026-05-01
 * Purpose : Central admissions manager.
 *           Threads, IPC, priority scheduling, memory allocation.
 * Compile : gcc -Wall -Wextra -g -o admissions admissions.c -lpthread
 * ============================================================
 */

#include "hospital.h"

/* ── Global shared state ── */

static int            shm_id = -1;
static WardSharedMem *ward   = NULL;

/* Thread handles */
static pthread_t receptionist_tid;
static pthread_t scheduler_tid;
static pthread_t nurse_tids[3];

/* Nurse thread gets  index via this array */
static int   nurse_args[3]  = {0, 1, 2};
static char *nurse_types[3] = {"ICU", "ISOLATION", "GENERAL"};

/* Mutex + condition for the bed bitmap */
static pthread_mutex_t bed_mutex   = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  bed_freed   = PTHREAD_COND_INITIALIZER;

/* Mutex + condition for the priority queue */
static pthread_mutex_t queue_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  queue_ready = PTHREAD_COND_INITIALIZER;

/* Mutex for patient history array */
static pthread_mutex_t history_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Mutex for patient ID counter */
static pthread_mutex_t id_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Named semaphores */
static sem_t *sem_icu    = NULL;
static sem_t *sem_iso    = NULL;
static sem_t *sem_q_full = NULL;

/* Priority queue (sorted, 0 = highest priority) */
static PatientRecord wait_queue[MAX_QUEUE_SIZE];
static int           queue_size = 0;

/* Patient history for scheduling simulation */
static PatientRecord history[MAX_PATIENTS];
static int           history_count = 0;

/* Allocation strategy (changed by strategy flag) */
static int alloc_strategy = STRATEGY_BEST;

/* Set to 0 by signal handler to stop all threads */
static volatile int running = 1;

/* Auto-incrementing patient ID */
static int patient_id_counter = 1;

/* ── Forward declarations ── */
void  setup_ward_partitions(void);
int   find_best_fit(const char *type, int needed);
int   find_first_fit(const char *type, int needed);
int   find_worst_fit(const char *type, int needed);
int   allocate_bed(PatientRecord *rec);
void  free_bed(int patient_id);
void  coalesce_free_partitions(void);
void  print_ward_map(void);
void  compute_fragmentation(void);
void  log_fragmentation(const char *event);
void  report_paging(PatientRecord *rec);
void  enqueue_patient(PatientRecord *rec);
int   dequeue_patient(PatientRecord *out);
void  handle_discharge(DischargeMsg *msg);
int   get_next_patient_id(void);
void  assign_bed_type(PatientRecord *rec);
pid_t fork_patient_simulator(PatientRecord *rec);
void *receptionist_thread(void *arg);
void *scheduler_thread(void *arg);
void *nurse_thread(void *arg);
void  simulate_scheduling(void);
void  sigchld_handler(int sig);
void  sigterm_handler(int sig);

/* ── Utility ── */

int get_next_patient_id(void) {
    pthread_mutex_lock(&id_mutex);
    int id = patient_id_counter++;
    pthread_mutex_unlock(&id_mutex);
    return id;
}

/*
 * assign_bed_type
 * Decides bed type and care units from triage priority.
 * Priority 1-2  → ICU        (3 care units)
 * Priority 3 or infectious → Isolation (2 care units)
 * Priority 4-5  → General    (1 care unit)
 */
void assign_bed_type(PatientRecord *rec) {
    if (rec->priority <= 2) {
        strcpy(rec->bed_type, "ICU");
        rec->care_units = ICU_CARE_UNITS;
    } else if (rec->priority == 3 || rec->infectious) {
        strcpy(rec->bed_type, "ISOLATION");
        rec->care_units = ISOLATION_CARE_UNITS;
    } else {
        strcpy(rec->bed_type, "GENERAL");
        rec->care_units = GENERAL_CARE_UNITS;
    }
}

/* ── Ward setup ── */

/*
 * setup_ward_partitions
 * Builds the initial partition table in shared memory.
 */
void setup_ward_partitions(void) {
    int p    = 0;
    int unit = 0;

    /* Mark all care units free */
    for (int i = 0; i < TOTAL_CARE_UNITS; i++)
        ward->ward_array[i] = -1;

    /* Mark all page table entries free */
    for (int i = 0; i < TOTAL_PAGES; i++)
        ward->page_table[i] = -1;

    /* 4 ICU partitions — 3 care units each */
    for (int i = 0; i < ICU_COUNT; i++, p++) {
        ward->partitions[p].partition_id = p;
        ward->partitions[p].start_unit   = unit;
        ward->partitions[p].size         = ICU_CARE_UNITS;
        ward->partitions[p].is_free      = 1;
        ward->partitions[p].patient_id   = -1;
        strcpy(ward->partitions[p].bed_type, "ICU");
        unit += ICU_CARE_UNITS;
    }

    /* 4 Isolation partitions — 2 care units each */
    for (int i = 0; i < ISOLATION_COUNT; i++, p++) {
        ward->partitions[p].partition_id = p;
        ward->partitions[p].start_unit   = unit;
        ward->partitions[p].size         = ISOLATION_CARE_UNITS;
        ward->partitions[p].is_free      = 1;
        ward->partitions[p].patient_id   = -1;
        strcpy(ward->partitions[p].bed_type, "ISOLATION");
        unit += ISOLATION_CARE_UNITS;
    }

    /* 12 General partitions — 1 care unit each */
    for (int i = 0; i < GENERAL_COUNT; i++, p++) {
        ward->partitions[p].partition_id = p;
        ward->partitions[p].start_unit   = unit;
        ward->partitions[p].size         = GENERAL_CARE_UNITS;
        ward->partitions[p].is_free      = 1;
        ward->partitions[p].patient_id   = -1;
        strcpy(ward->partitions[p].bed_type, "GENERAL");
        unit += GENERAL_CARE_UNITS;
    }

    ward->num_partitions = p;
    ward->total_served   = 0;

    printf("[INIT] Ward ready: %d ICU + %d Isolation + %d General (%d partitions)\n",
           ICU_COUNT, ISOLATION_COUNT, GENERAL_COUNT, p);
    printf("[INIT] Total care units: %d | Page size: %d | Pages: %d\n",
           TOTAL_CARE_UNITS, PAGE_SIZE, TOTAL_PAGES);
}

/* ── Memory allocation — three strategies ─ */

/*
 * find_best_fit
 * Scan all free partitions of the given type.
 * Return the one with the least wasted space (size - needed).
 * Must be called with bed_mutex held.
 */
int find_best_fit(const char *type, int needed) {
    int best_idx   = -1;
    int best_waste = 99999;
    for (int i = 0; i < ward->num_partitions; i++) {
        BedPartition *p = &ward->partitions[i];
        if (!p->is_free)                      continue;
        if (strcmp(p->bed_type, type) != 0)   continue;
        if (p->size < needed)                  continue;
        int waste = p->size - needed;
        if (waste < best_waste) {
            best_waste = waste;
            best_idx   = i;
        }
    }
    return best_idx;
}

/*
 * find_first_fit
 * Return the first free partition of the given type that is big enough.
 * Must be called with bed_mutex held.
 */
int find_first_fit(const char *type, int needed) {
    for (int i = 0; i < ward->num_partitions; i++) {
        BedPartition *p = &ward->partitions[i];
        if (!p->is_free)                      continue;
        if (strcmp(p->bed_type, type) != 0)   continue;
        if (p->size >= needed)                 return i;
    }
    return -1;
}

/*
 * find_worst_fit
 * Return the largest free partition of the given type.
 * Must be called with bed_mutex held.
 */
int find_worst_fit(const char *type, int needed) {
    int worst_idx  = -1;
    int worst_size = -1;
    for (int i = 0; i < ward->num_partitions; i++) {
        BedPartition *p = &ward->partitions[i];
        if (!p->is_free)                      continue;
        if (strcmp(p->bed_type, type) != 0)   continue;
        if (p->size < needed)                  continue;
        if (p->size > worst_size) {
            worst_size = p->size;
            worst_idx  = i;
        }
    }
    return worst_idx;
}

/*
 * allocate_bed
 * Picks a partition using the active strategy.
 * Marks it occupied and updates ward_array + page_table.
 * Returns 0 on success, -1 if no suitable bed exists.
 * Must be called with bed_mutex held.
 */
int allocate_bed(PatientRecord *rec) {
    int idx = -1;

    if (alloc_strategy == STRATEGY_FIRST)
        idx = find_first_fit(rec->bed_type, rec->care_units);
    else if (alloc_strategy == STRATEGY_WORST)
        idx = find_worst_fit(rec->bed_type, rec->care_units);
    else
        idx = find_best_fit(rec->bed_type, rec->care_units);

    if (idx < 0) return -1;

    BedPartition *bp = &ward->partitions[idx];
    bp->is_free    = 0;
    bp->patient_id = rec->patient_id;

    /* Mark care units in ward_array */
    for (int u = bp->start_unit; u < bp->start_unit + bp->size; u++)
        ward->ward_array[u] = rec->patient_id;

    /* Update page table */
    int start_page = bp->start_unit / PAGE_SIZE;
    int end_page   = (bp->start_unit + bp->size - 1) / PAGE_SIZE;
    for (int pg = start_page; pg <= end_page; pg++)
        ward->page_table[pg] = rec->patient_id;

    rec->assigned_bed = idx;

    printf("[ALLOC] Patient #%d -> partition %d (%s, %d units, start_unit=%d)\n",
           rec->patient_id, idx, rec->bed_type, bp->size, bp->start_unit);
    return 0;
}

/*
 * free_bed
 * Clears the partition occupied by patient_id.
 * Updates ward_array and page_table too.
 * Must be called with bed_mutex held.
 */
void free_bed(int patient_id) {
    for (int i = 0; i < ward->num_partitions; i++) {
        BedPartition *bp = &ward->partitions[i];
        if (bp->patient_id != patient_id) continue;

        printf("[FREE ] Partition %d freed (patient #%d, %s)\n",
               i, patient_id, bp->bed_type);

        for (int u = bp->start_unit; u < bp->start_unit + bp->size; u++)
            ward->ward_array[u] = -1;

        int sp2 = bp->start_unit / PAGE_SIZE;
        int ep2 = (bp->start_unit + bp->size - 1) / PAGE_SIZE;
        for (int pg = sp2; pg <= ep2; pg++) {
            if (ward->page_table[pg] == patient_id)
                ward->page_table[pg] = -1;
        }

        bp->is_free    = 1;
        bp->patient_id = -1;
        ward->total_served++;
        return;
    }
    printf("[WARN ] free_bed: patient #%d not found\n", patient_id);
}

/* ── Coalescing ─── */

/*
 * coalesce_free_partitions
 * Merges adjacent free partitions of the same bed type.
 * Runs after every free_bed() call.
 * Must be called with bed_mutex held.
 */
void coalesce_free_partitions(void) {
    int merged = 1;
    while (merged) {
        merged = 0;
        for (int i = 0; i < ward->num_partitions - 1; i++) {
            BedPartition *a = &ward->partitions[i];
            BedPartition *b = &ward->partitions[i + 1];

            if (!a->is_free || !b->is_free)                  continue;
            if (strcmp(a->bed_type, b->bed_type) != 0)        continue;
            if (a->start_unit + a->size != b->start_unit)     continue;

            printf("[COAL ] Merging partitions %d+%d (%s, %d+%d units)\n",
                   a->partition_id, b->partition_id,
                   a->bed_type, a->size, b->size);

            /* Absorb b into a */
            a->size += b->size;

            /* Shift remaining partitions left */
            for (int j = i + 1; j < ward->num_partitions - 1; j++)
                ward->partitions[j] = ward->partitions[j + 1];

            ward->num_partitions--;
            merged = 1;
            break;
        }
    }
}

/* ── Fragmentation reporting ───*/

void compute_fragmentation(void) {
    int total_free   = 0;
    int largest_free = 0;
    for (int i = 0; i < ward->num_partitions; i++) {
        if (!ward->partitions[i].is_free) continue;
        total_free += ward->partitions[i].size;
        if (ward->partitions[i].size > largest_free)
            largest_free = ward->partitions[i].size;
    }
    float pct = 0.0f;
    if (total_free > 0)
        pct = (1.0f - (float)largest_free / (float)total_free) * 100.0f;
    printf("[FRAG ] Free=%d | Largest=%d | ExtFrag=%.1f%%\n",
           total_free, largest_free, pct);
}

void log_fragmentation(const char *event) {
    FILE *fp = fopen("logs/memory_log.txt", "a");
    if (!fp) return;

    int total_free   = 0;
    int largest_free = 0;
    pthread_mutex_lock(&bed_mutex);
    for (int i = 0; i < ward->num_partitions; i++) {
        if (!ward->partitions[i].is_free) continue;
        total_free += ward->partitions[i].size;
        if (ward->partitions[i].size > largest_free)
            largest_free = ward->partitions[i].size;
    }
    pthread_mutex_unlock(&bed_mutex);

    float pct = 0.0f;
    if (total_free > 0)
        pct = (1.0f - (float)largest_free / (float)total_free) * 100.0f;

    time_t now = time(NULL);
    char ts[32];
    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", localtime(&now));
    fprintf(fp, "[%s] %-20s | Free: %2d | Largest: %2d | ExtFrag: %.1f%%\n",
            ts, event, total_free, largest_free, pct);
    fclose(fp);
}

void print_ward_map(void) {
    printf("[MAP  ] [");
    for (int i = 0; i < TOTAL_CARE_UNITS; i++) {
        if (ward->ward_array[i] == -1) printf(".");
        else                           printf("%d", ward->ward_array[i] % 10);
    }
    printf("] (%d partitions)\n", ward->num_partitions);
}

/* ── Paging simulation ─── */

void report_paging(PatientRecord *rec) {
    int pages     = (rec->care_units + PAGE_SIZE - 1) / PAGE_SIZE;
    int allocated = pages * PAGE_SIZE;
    int waste     = allocated - rec->care_units;

    printf("[PAGE ] Patient #%d: %d units -> %d pages -> internal frag: %d unit(s)\n",
           rec->patient_id, rec->care_units, pages, waste);

    FILE *fp = fopen("logs/memory_log.txt", "a");
    if (fp) {
        time_t now = time(NULL);
        char ts[32];
        strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", localtime(&now));
        fprintf(fp,
            "[%s] PAGING  Patient #%d (%s) | Units:%d | Pages:%d | IntFrag:%d\n",
            ts, rec->patient_id, rec->name,
            rec->care_units, pages, waste);
        fclose(fp);
    }
}

/* ── Priority queue ── */

/*
 * enqueue_patient
 * Inserts patient into the sorted wait queue (lower priority number = first).
 * Signals queue_ready so the scheduler thread wakes up.
 * Acquires queue_mutex internally.
 */
void enqueue_patient(PatientRecord *rec) {
    pthread_mutex_lock(&queue_mutex);

    if (queue_size >= MAX_QUEUE_SIZE) {
        printf("[QUEUE] Full! Dropping patient #%d\n", rec->patient_id);
        pthread_mutex_unlock(&queue_mutex);
        return;
    }

    /* Find insertion position (keep sorted ascending by priority) */
    int pos = queue_size;
    for (int i = 0; i < queue_size; i++) {
        if (rec->priority < wait_queue[i].priority) {
            pos = i;
            break;
        }
    }

    /* Shift right to make room */
    for (int i = queue_size; i > pos; i--)
        wait_queue[i] = wait_queue[i - 1];

    wait_queue[pos] = *rec;
    queue_size++;

    printf("[QUEUE] Enqueued #%d (%s) priority=%d | size=%d\n",
           rec->patient_id, rec->name, rec->priority, queue_size);

    pthread_cond_signal(&queue_ready);
    pthread_mutex_unlock(&queue_mutex);
}

/*
 * dequeue_patient
 * Removes and returns the front of the queue (highest priority).
 * Caller must hold queue_mutex.
 * Returns 1 if a patient was dequeued, 0 if empty.
 */
int dequeue_patient(PatientRecord *out) {
    if (queue_size == 0) return 0;
    *out = wait_queue[0];
    for (int i = 0; i < queue_size - 1; i++)
        wait_queue[i] = wait_queue[i + 1];
    queue_size--;
    return 1;
}

/* ── Process management, fork + exec ─── */

/*
 * fork_patient_simulator
 * Creates an anonymous pipe, forks a child, and execs patient_simulator.
 * Parent writes the PatientRecord into the pipe.
 * Child reads it after exec via the fd passed as argv[1].
 */
pid_t fork_patient_simulator(PatientRecord *rec) {
    int pipefd[2];

    if (pipe(pipefd) < 0) {
        perror("[ERROR] pipe");
        return -1;
    }

    pid_t pid = fork();
    if (pid < 0) {
        perror("[ERROR] fork");
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }

    if (pid == 0) {
        /* ── CHILD ──*/
        close(pipefd[1]);                  /* child does not write */
        char fd_str[16];
        snprintf(fd_str, sizeof(fd_str), "%d", pipefd[0]);
        execl("./patient_simulator", "patient_simulator", fd_str, NULL);
        perror("[ERROR] execl failed");
        exit(1);
    }

    /* ── PARENT ──── */
    close(pipefd[0]);                      /* parent does not read */
    write(pipefd[1], rec, sizeof(PatientRecord));
    close(pipefd[1]);

    printf("[FORK ] PID=%d spawned for patient #%d (%s)\n",
           pid, rec->patient_id, rec->name);
    return pid;
}

/* ── Discharge handler ─── */

/*
 * handle_discharge
 * Called by receptionist thread when a DischargeMsg arrives.
 * Frees the bed, coalesces, broadcasts bed_freed, posts semaphore.
 */
void handle_discharge(DischargeMsg *msg) {
    pthread_mutex_lock(&bed_mutex);

    /* Find out what type this patient had before freeing */
    char bed_type[16] = "GENERAL";
    for (int i = 0; i < ward->num_partitions; i++) {
        if (ward->partitions[i].patient_id == msg->patient_id) {
            strncpy(bed_type, ward->partitions[i].bed_type, 15);
            break;
        }
    }

    printf("[DISCH] Patient #%d discharged (%ds)\n",
           msg->patient_id, msg->duration);

    print_ward_map();
    free_bed(msg->patient_id);
    coalesce_free_partitions();
    compute_fragmentation();
    print_ward_map();

    pthread_cond_broadcast(&bed_freed);
    pthread_mutex_unlock(&bed_mutex);

    /* Release the right semaphore slot */
    if (strcmp(bed_type, "ICU") == 0)
        sem_post(sem_icu);
    else if (strcmp(bed_type, "ISOLATION") == 0)
        sem_post(sem_iso);
    else
        sem_post(sem_q_full);

    log_fragmentation("discharge");

    /* Record finish time in history */
    pthread_mutex_lock(&history_mutex);
    for (int i = 0; i < history_count; i++) {
        if (history[i].patient_id == msg->patient_id) {
            history[i].finish_time        = time(NULL);
            history[i].treatment_duration = msg->duration;
            break;
        }
    }
    pthread_mutex_unlock(&history_mutex);
}

/* ── Signal handlers ─── */

/* Reap all finished child processes without blocking */
void sigchld_handler(int sig) {
    (void)sig;
    while (waitpid(-1, NULL, WNOHANG) > 0)
        ;
}

/* Graceful shutdown: set running=0 and wake blocked threads */
void sigterm_handler(int sig) {
    (void)sig;
    printf("\n[SIG  ] Shutdown signal received.\n");
    running = 0;
    pthread_cond_broadcast(&bed_freed);
    pthread_cond_broadcast(&queue_ready);
}

/* ── Receptionist thread ─── */

/*
 * receptionist_thread
 * Monitors both FIFOs using select().
 *
 * TRIAGE_FIFO  : reads plain text lines written by triage.sh
 *   Format     : "Name|age|severity|priority|infectious\n"
 *   Action     : parse with sscanf, assign ID, enqueue patient
 *
 * DISCHARGE_FIFO: reads binary DischargeMsg structs written by patient_simulator
 *   Action     : call handle_discharge()
 */
void *receptionist_thread(void *arg) {
    (void)arg;
    printf("[RECEP] Receptionist started (TID=%lu)\n",
           (unsigned long)pthread_self());

    /* Open triage FIFO — blocks until triage.sh opens the write end */
    int triage_fd = open(TRIAGE_FIFO, O_RDWR | O_NONBLOCK);
    if (triage_fd < 0) {
        perror("[ERROR] open TRIAGE_FIFO");
        return NULL;
    }

    /* Open discharge FIFO non-blocking so select() works on both */
    int discharge_fd = open(DISCHARGE_FIFO, O_RDONLY | O_NONBLOCK);
    if (discharge_fd < 0) {
        perror("[ERROR] open DISCHARGE_FIFO");
        close(triage_fd);
        return NULL;
    }

    fd_set         read_fds;
    struct timeval timeout;

    while (running) {
        FD_ZERO(&read_fds);
        FD_SET(triage_fd,    &read_fds);
        FD_SET(discharge_fd, &read_fds);
        int maxfd = (triage_fd > discharge_fd ? triage_fd : discharge_fd) + 1;

        timeout.tv_sec  = 1;
        timeout.tv_usec = 0;

        int ready = select(maxfd, &read_fds, NULL, NULL, &timeout);
        if (ready < 0) {
            if (errno == EINTR) continue;
            perror("[ERROR] select");
            break;
        }

        /* ── New patient from triage.sh ── */
        if (FD_ISSET(triage_fd, &read_fds)) {
            char line[256];
            memset(line, 0, sizeof(line));
            ssize_t n = read(triage_fd, line, sizeof(line) - 1);

            if (n > 0) {
                /* Remove trailing newline */
                if (line[n - 1] == '\n') line[n - 1] = '\0';

                /*
                 * Parse pipe-separated text from triage.sh
                 * Format: "Name|age|severity|priority|infectious"
                 * %63[^|] reads up to 63 chars stopping at '|'
                 * This correctly handles names with spaces.
                 */
                PatientRecord rec;
                memset(&rec, 0, sizeof(PatientRecord));

                int parsed = sscanf(line,
                    "%63[^|]|%d|%d|%d|%d",
                    rec.name,
                    &rec.age,
                    &rec.severity,
                    &rec.priority,
                    &rec.infectious);

                if (parsed != 5) {
                    printf("[RECEP] Bad line (%d fields): '%s'\n", parsed, line);
                    continue;
                }

                rec.patient_id   = get_next_patient_id();
                rec.arrival_time = time(NULL);
                rec.assigned_bed = -1;
                assign_bed_type(&rec);

                printf("[RECEP] #%d %s | age=%d sev=%d pri=%d %s\n",
                       rec.patient_id, rec.name,
                       rec.age, rec.severity, rec.priority, rec.bed_type);

                pthread_mutex_lock(&history_mutex);
                if (history_count < MAX_PATIENTS)
                    history[history_count++] = rec;
                pthread_mutex_unlock(&history_mutex);

                enqueue_patient(&rec);

            } else if (n == 0) {
    /* Writer closed — reopen for next patient */
    close(triage_fd);
    if (!running) break;
    sleep(1);
    if (!running) break;
    triage_fd = open(TRIAGE_FIFO, O_RDWR | O_NONBLOCK);
    if (triage_fd < 0) {
        perror("[ERROR] reopen TRIAGE_FIFO");
        break;
    }
 }
        }

        /* ── Discharge from patient_simulator ─ */
        if (FD_ISSET(discharge_fd, &read_fds)) {
            DischargeMsg msg;
            ssize_t n = read(discharge_fd, &msg, sizeof(DischargeMsg));
            if (n == (ssize_t)sizeof(DischargeMsg))
                handle_discharge(&msg);
        }
    }

    close(triage_fd);
    close(discharge_fd);
    printf("[RECEP] Receptionist exiting\n");
    return NULL;
}

/* ── Scheduler thread ─── */

/*
 * scheduler_thread
 * Waits for patients in the queue, acquires capacity semaphore,
 * allocates a bed, and forks patient_simulator.
 */
void *scheduler_thread(void *arg) {
    (void)arg;
    printf("[SCHED] Scheduler started (TID=%lu)\n",
           (unsigned long)pthread_self());

    while (running) {
        PatientRecord rec;

        /* Wait until queue has a patient */
        pthread_mutex_lock(&queue_mutex);
        while (queue_size == 0 && running)
            pthread_cond_wait(&queue_ready, &queue_mutex);
        if (!running) { pthread_mutex_unlock(&queue_mutex); break; }

        int got = dequeue_patient(&rec);
        pthread_mutex_unlock(&queue_mutex);
        if (!got) continue;

        printf("[SCHED] Admitting #%d (%s) -> %s\n",
               rec.patient_id, rec.name, rec.bed_type);

        /*
         * Acquire capacity semaphore.
         * If all beds of this type are occupied, sem_wait blocks here
         * until handle_discharge() calls sem_post().
         * This is the producer-consumer / semaphore blocking demo.
         */
        if (strcmp(rec.bed_type, "ICU") == 0) {
            printf("[SCHED] Waiting on ICU semaphore for #%d...\n", rec.patient_id);
            sem_wait(sem_icu);
        } else if (strcmp(rec.bed_type, "ISOLATION") == 0) {
            printf("[SCHED] Waiting on Isolation semaphore for #%d...\n", rec.patient_id);
            sem_wait(sem_iso);
        } else {
            sem_wait(sem_q_full);
        }

        /* Allocate a partition */
        pthread_mutex_lock(&bed_mutex);
        while (allocate_bed(&rec) < 0 && running) {
            printf("[SCHED] No partition for #%d — waiting for bed_freed\n",
                   rec.patient_id);
            pthread_cond_wait(&bed_freed, &bed_mutex);
        }
        if (!running) { pthread_mutex_unlock(&bed_mutex); break; }

        rec.start_time = time(NULL);
        report_paging(&rec);
        compute_fragmentation();
        print_ward_map();
        pthread_mutex_unlock(&bed_mutex);

        log_fragmentation("admission");

        /* Update history with admission info */
        pthread_mutex_lock(&history_mutex);
        for (int i = 0; i < history_count; i++) {
            if (history[i].patient_id == rec.patient_id) {
                history[i].start_time   = rec.start_time;
                history[i].assigned_bed = rec.assigned_bed;
                strcpy(history[i].bed_type, rec.bed_type);
                history[i].care_units   = rec.care_units;
                break;
            }
        }
        pthread_mutex_unlock(&history_mutex);

        /* Spawn child process */
        fork_patient_simulator(&rec);

        usleep(100000); /* 0.1s gap to avoid fork spam */
    }

    printf("[SCHED] Scheduler exiting\n");
    return NULL;
}

/* ── Nurse threads ──── */

/*
 * nurse_thread
 * One nurse per bed type (ICU=0, ISOLATION=1, GENERAL=2).
 * Wakes on bed_freed broadcast and reports ward status.
 */
void *nurse_thread(void *arg) {
    int   idx     = *(int *)arg;
    char *my_type = nurse_types[idx];

    printf("[NURSE] %s nurse started (TID=%lu)\n",
           my_type, (unsigned long)pthread_self());

    while (running) {
        pthread_mutex_lock(&bed_mutex);
        pthread_cond_wait(&bed_freed, &bed_mutex);
        if (!running) { pthread_mutex_unlock(&bed_mutex); break; }

        int free_cnt = 0, occ_cnt = 0;
        for (int i = 0; i < ward->num_partitions; i++) {
            BedPartition *p = &ward->partitions[i];
            if (strcmp(p->bed_type, my_type) != 0) continue;
            if (p->is_free) free_cnt++;
            else            occ_cnt++;
        }
        printf("[NURSE] %s ward: %d occupied, %d free\n",
               my_type, occ_cnt, free_cnt);
        pthread_mutex_unlock(&bed_mutex);
    }

    printf("[NURSE] %s nurse exiting\n", my_type);
    return NULL;
}

/* ── Scheduling simulation ───── */

/*
 * simulate_scheduling
 * Runs at shutdown. Simulates FCFS, Priority, and SJF on the
 * recorded patient history and writes results to schedule_log.txt.
 */
void simulate_scheduling(void) {
    FILE *fp = fopen("logs/schedule_log.txt", "w");
    if (!fp) { perror("schedule_log.txt"); return; }

    pthread_mutex_lock(&history_mutex);
    int n = history_count;
    PatientRecord snap[MAX_PATIENTS];
    memcpy(snap, history, sizeof(PatientRecord) * n);
    pthread_mutex_unlock(&history_mutex);

    if (n == 0) {
        fprintf(fp, "No patients recorded.\n");
        fclose(fp);
        return;
    }

    /* Burst = actual treatment_duration if known, else estimate */
    #define BURST(p) ((p).treatment_duration > 0 \
                      ? (p).treatment_duration   \
                      : (p).care_units * 3)

    fprintf(fp, "=== SCHEDULING SIMULATION REPORT ===\n");
    fprintf(fp, "Total patients: %d\n\n", n);

    /* ── 1. FCFS ──── */
    fprintf(fp, "--- Algorithm 1: FCFS (First-Come First-Served) ---\n");
    fprintf(fp, "%-4s %-20s %-5s %-6s %-8s %-10s\n",
            "ID","Name","Pri","Burst","Waiting","Turnaround");

    PatientRecord fcfs[MAX_PATIENTS];
    memcpy(fcfs, snap, sizeof(PatientRecord)*n);

    /* Bubble sort by arrival_time */
    for (int i = 0; i < n-1; i++)
        for (int j = i+1; j < n; j++)
            if (fcfs[j].arrival_time < fcfs[i].arrival_time) {
                PatientRecord t = fcfs[i]; fcfs[i]=fcfs[j]; fcfs[j]=t;
            }

    double tw=0, tt=0;
    time_t cur = fcfs[0].arrival_time;
    for (int i = 0; i < n; i++) {
        if (cur < fcfs[i].arrival_time) cur = fcfs[i].arrival_time;
        int w = (int)(cur - fcfs[i].arrival_time);
        int b = BURST(fcfs[i]);
        int t2 = w + b;
        tw += w; tt += t2; cur += b;
        fprintf(fp,"%-4d %-20s %-5d %-6d %-8d %-10d\n",
                fcfs[i].patient_id, fcfs[i].name,
                fcfs[i].priority, b, w, t2);
    }
    fprintf(fp,"\nFCFS => Avg Waiting: %.1fs | Avg Turnaround: %.1fs\n\n",
            tw/n, tt/n);

    /* ── 2. Priority ──── */
    fprintf(fp, "--- Algorithm 2: Priority Scheduling ---\n");
    fprintf(fp, "%-4s %-20s %-5s %-6s %-8s %-10s\n",
            "ID","Name","Pri","Burst","Waiting","Turnaround");

    PatientRecord pr[MAX_PATIENTS];
    memcpy(pr, snap, sizeof(PatientRecord)*n);
    for (int i = 0; i < n-1; i++)
        for (int j = i+1; j < n; j++)
            if (pr[j].priority < pr[i].priority) {
                PatientRecord t = pr[i]; pr[i]=pr[j]; pr[j]=t;
            }

    tw=0; tt=0; cur=pr[0].arrival_time;
    for (int i = 0; i < n; i++) {
        if (cur < pr[i].arrival_time) cur = pr[i].arrival_time;
        int w = (int)(cur - pr[i].arrival_time);
        int b = BURST(pr[i]);
        int t2 = w + b;
        tw += w; tt += t2; cur += b;
        fprintf(fp,"%-4d %-20s %-5d %-6d %-8d %-10d\n",
                pr[i].patient_id, pr[i].name,
                pr[i].priority, b, w, t2);
    }
    fprintf(fp,"\nPriority => Avg Waiting: %.1fs | Avg Turnaround: %.1fs\n\n",
            tw/n, tt/n);

    /* ── 3. SJF ──── */
    fprintf(fp, "--- Algorithm 3: SJF (Shortest Job First) ---\n");
    fprintf(fp, "%-4s %-20s %-5s %-6s %-8s %-10s\n",
            "ID","Name","Pri","Burst","Waiting","Turnaround");

    PatientRecord sjf[MAX_PATIENTS];
    memcpy(sjf, snap, sizeof(PatientRecord)*n);
    for (int i = 0; i < n-1; i++)
        for (int j = i+1; j < n; j++)
            if (BURST(sjf[j]) < BURST(sjf[i])) {
                PatientRecord t = sjf[i]; sjf[i]=sjf[j]; sjf[j]=t;
            }

    tw=0; tt=0; cur=sjf[0].arrival_time;
    for (int i = 0; i < n; i++) {
        if (cur < sjf[i].arrival_time) cur = sjf[i].arrival_time;
        int w = (int)(cur - sjf[i].arrival_time);
        int b = BURST(sjf[i]);
        int t2 = w + b;
        tw += w; tt += t2; cur += b;
        fprintf(fp,"%-4d %-20s %-5d %-6d %-8d %-10d\n",
                sjf[i].patient_id, sjf[i].name,
                sjf[i].priority, b, w, t2);
    }
    fprintf(fp,"\nSJF => Avg Waiting: %.1fs | Avg Turnaround: %.1fs\n\n",
            tw/n, tt/n);

    fprintf(fp,"=== END OF REPORT ===\n");
    fclose(fp);
    printf("[SCHED] schedule_log.txt written.\n");
}

/* ── main ───── */

int main(int argc, char *argv[]) {

    /* Parse optional --strategy flag */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--strategy") == 0 && i+1 < argc) {
            i++;
            if      (strcmp(argv[i], "first") == 0) alloc_strategy = STRATEGY_FIRST;
            else if (strcmp(argv[i], "worst") == 0) alloc_strategy = STRATEGY_WORST;
            else                                     alloc_strategy = STRATEGY_BEST;
            printf("[INIT] Strategy: %s\n", argv[i]);
        }
    }

    /* Install signal handlers */
    signal(SIGCHLD, sigchld_handler);
    signal(SIGTERM, sigterm_handler);
    signal(SIGINT,  sigterm_handler);

    /* ── Shared memory ───── */
    shm_id = shmget(SHM_KEY, sizeof(WardSharedMem), IPC_CREAT | 0666);
    if (shm_id < 0) { perror("[ERROR] shmget"); exit(1); }

    ward = (WardSharedMem *)shmat(shm_id, NULL, 0);
    if (ward == (void *)-1) { perror("[ERROR] shmat"); exit(1); }

    memset(ward, 0, sizeof(WardSharedMem));
    setup_ward_partitions();

    /* ── Named semaphores ──── */
    sem_unlink(SEM_ICU);
    sem_unlink(SEM_ISO);
    sem_unlink(SEM_QUEUE_FULL);

    sem_icu    = sem_open(SEM_ICU,        O_CREAT, 0666, ICU_COUNT);
    sem_iso    = sem_open(SEM_ISO,        O_CREAT, 0666, ISOLATION_COUNT);
    sem_q_full = sem_open(SEM_QUEUE_FULL, O_CREAT, 0666, GENERAL_COUNT);

    if (sem_icu == SEM_FAILED || sem_iso == SEM_FAILED || sem_q_full == SEM_FAILED) {
        perror("[ERROR] sem_open"); exit(1);
    }

    /* ── FIFOs ───── */
    unlink(TRIAGE_FIFO);
    unlink(DISCHARGE_FIFO);

    if (mkfifo(TRIAGE_FIFO,    0666) < 0 && errno != EEXIST)
        perror("[WARN] mkfifo triage");
    if (mkfifo(DISCHARGE_FIFO, 0666) < 0 && errno != EEXIST)
        perror("[WARN] mkfifo discharge");

    printf("[INIT] FIFOs created: %s | %s\n", TRIAGE_FIFO, DISCHARGE_FIFO);

    mkdir("logs", 0755);
    log_fragmentation("startup");

    /* ── Start threads ───── */
    pthread_create(&receptionist_tid, NULL, receptionist_thread, NULL);
    pthread_create(&scheduler_tid,    NULL, scheduler_thread,    NULL);
    for (int i = 0; i < 3; i++)
        pthread_create(&nurse_tids[i], NULL, nurse_thread, &nurse_args[i]);

    printf("\n=== HOSPITAL IS OPEN — Waiting for patients ===\n\n");

    /* Main thread just waits for a signal */
    while (running) sleep(1);

    /* ── Shutdown ────── */
    printf("[MAIN ] Joining threads...\n");
    pthread_join(receptionist_tid, NULL);
    pthread_join(scheduler_tid,    NULL);
    for (int i = 0; i < 3; i++)
        pthread_cancel(nurse_tids[i]);

    simulate_scheduling();

    shmdt(ward);
    shmctl(shm_id, IPC_RMID, NULL);

    sem_close(sem_icu);    sem_unlink(SEM_ICU);
    sem_close(sem_iso);    sem_unlink(SEM_ISO);
    sem_close(sem_q_full); sem_unlink(SEM_QUEUE_FULL);

    unlink(TRIAGE_FIFO);
    unlink(DISCHARGE_FIFO);

    printf("[MAIN ] Total patients served: %d\n", ward->total_served);
    printf("[MAIN ] Done.\n");
    return 0;
}
