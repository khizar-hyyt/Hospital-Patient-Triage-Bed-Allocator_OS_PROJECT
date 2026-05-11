/*
 * ============================================================
 * Project : Hospital Patient Triage & Bed Allocator
 * File    : patient_simulator.c
 * Group   : Group 08
 * Members : TALHA (24F-0793) , KHIZAR (24F0812)
 * Date    : 2026-05-01
 * Purpose : Child process — simulates one patient's treatment.
 *           Reads PatientRecord from pipe, sleeps (treatment),
 *           then writes DischargeMsg to DISCHARGE_FIFO.
 * Compile : gcc -Wall -Wextra -g -o patient_simulator patient_simulator.c
 * ============================================================
 */

#include "hospital.h"

int main(int argc, char *argv[]) {

    if (argc < 2) {
        fprintf(stderr, "[PATIENT] Usage: patient_simulator <pipe_fd>\n");
        exit(1);
    }

    /* The read-end fd was passed by the parent as a string */
    int pipe_fd = atoi(argv[1]);
    if (pipe_fd <= 0) {
        fprintf(stderr, "[PATIENT] Invalid pipe fd: %s\n", argv[1]);
        exit(1);
    }

    /*
     * Read the PatientRecord the parent wrote into the pipe.
     * This is a binary read — both sides are C so sizes match exactly.
     */
    PatientRecord rec;
    ssize_t n = read(pipe_fd, &rec, sizeof(PatientRecord));
    close(pipe_fd);

    if (n != (ssize_t)sizeof(PatientRecord)) {
        fprintf(stderr, "[PATIENT] Short read (%zd / %zu bytes)\n",
                n, sizeof(PatientRecord));
        exit(1);
    }

    printf("[PATIENT #%d] %s arrived — type: %s, priority: %d\n",
           rec.patient_id, rec.name, rec.bed_type, rec.priority);
    fflush(stdout);

    /* Seed RNG differently for each patient */
    srand((unsigned int)(time(NULL) ^ (rec.patient_id * 7919)));

    /*
     * Treatment duration depends on bed type.
     * ICU       : 5–15 seconds
     * ISOLATION : 3–10 seconds
     * GENERAL   : 2–8  seconds
     */
    int duration;
    if (strcmp(rec.bed_type, "ICU") == 0)
        duration = 5 + rand() % 11;
    else if (strcmp(rec.bed_type, "ISOLATION") == 0)
        duration = 3 + rand() % 8;
    else
        duration = 2 + rand() % 7;

    printf("[PATIENT #%d] %s — treatment started (%d seconds)\n",
           rec.patient_id, rec.name, duration);
    fflush(stdout);

    /* Simulate treatment */
    sleep(duration);

    printf("[PATIENT #%d] %s — treatment complete\n",
           rec.patient_id, rec.name);
    fflush(stdout);

    /*
     * Notify admissions via the discharge FIFO.
     * Write a small binary DischargeMsg (2 ints).
     * admissions receptionist thread reads this.
     */
    int fifo_fd = open(DISCHARGE_FIFO, O_WRONLY);
    if (fifo_fd < 0) {
        perror("[PATIENT] open DISCHARGE_FIFO");
        exit(1);
    }

    DischargeMsg msg;
    msg.patient_id = rec.patient_id;
    msg.duration   = duration;

    write(fifo_fd, &msg, sizeof(DischargeMsg));
    close(fifo_fd);

    printf("[PATIENT #%d] %s — discharged. Goodbye!\n",
           rec.patient_id, rec.name);
    fflush(stdout);

    return 0;
}
