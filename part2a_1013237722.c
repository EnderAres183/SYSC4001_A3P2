#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <string.h>
#include <time.h>

#define RUBRIC_LINES 5
#define MAX_LINE_LEN 100
#define EXAM_LINES 10

struct SharedData {
    char rubric[RUBRIC_LINES][MAX_LINE_LEN];
    char exam[EXAM_LINES][MAX_LINE_LEN];
    int currentExamIndex;
    int totalExams;
    char examFiles[50][256];
    int exam_generation;
    int terminate;
    int marked[RUBRIC_LINES];
};

static void die(const char *msg) {
    perror(msg);
    exit(EXIT_FAILURE);
}

static void random_delay(double min_sec, double max_sec) {
    double range = max_sec - min_sec;
    double delay = min_sec + ((double)rand() / RAND_MAX) * range;
    usleep((useconds_t)(delay * 1000000));
}

static void save_rubric(struct SharedData *data, const char *rubricFile) {
    FILE *f = fopen(rubricFile, "w");
    if (!f) die("Failed to write rubric file");
    for (int i = 0; i < RUBRIC_LINES; i++) {
        fprintf(f, "%s", data->rubric[i]);
    }
    fclose(f);
}

static void load_rubric(struct SharedData *data, const char *rubricFile) {
    FILE *f = fopen(rubricFile, "r");
    if (!f) die("Failed to open rubric file");
    for (int i = 0; i < RUBRIC_LINES; i++) {
        if (fgets(data->rubric[i], MAX_LINE_LEN, f) == NULL) {
            // default line if missing
            snprintf(data->rubric[i], MAX_LINE_LEN, "%d, %c\n", i + 1, 'A' + i);
        }
    }
    fclose(f);
}

static void load_exam(struct SharedData *data) {
    if (data->currentExamIndex >= data->totalExams) return;
    const char *fname = data->examFiles[data->currentExamIndex];
    printf("[COORD] BEFORE READ exam file '%s' (index=%d)\n", fname, data->currentExamIndex);
    FILE *f = fopen(fname, "r");
    if (!f) die("Failed to open exam file");
    for (int i = 0; i < EXAM_LINES; i++) data->exam[i][0] = '\0';
    for (int i = 0; i < EXAM_LINES; i++) {
        if (fgets(data->exam[i], MAX_LINE_LEN, f) == NULL) break;
    }
    fclose(f);
    for (int i = 0; i < RUBRIC_LINES; i++) data->marked[i] = 0;
    data->exam_generation++;
    printf("[COORD] AFTER  READ loaded exam for student %s (index=%d, gen=%d)\n",
           data->exam[0], data->currentExamIndex, data->exam_generation);
    // Termination sentinel check: student number 9999
    int stu = atoi(data->exam[0]);
    if (stu == 9999) {
        data->terminate = 1;
        printf("[COORD] Sentinel exam (9999) detected. Signaling termination.\n");
    }
}

static void TA_process(int id, struct SharedData *data, const char *rubricFile) {
    srand((unsigned int)(time(NULL) ^ (id << 16)));
    while (1) {
        if (data->terminate) {
            printf("[TA %d] Termination signal received. Exiting.\n", id);
            break;
        }
        printf("[TA %d] Checking rubric...\n", id);
        for (int i = 0; i < RUBRIC_LINES; i++) {
            printf("[TA %d] BEFORE READ rubric[%d] = '%s'\n", id, i, data->rubric[i]);
            random_delay(0.5, 1.0);
            // 20%% chance to modify rubric (races allowed in Part 2a)
            if (rand() % 5 == 0) {
                char *comma = strchr(data->rubric[i], ',');
                if (comma && *(comma + 2) != '\0') {
                    char oldVal = comma[2];
                    if (comma[2] >= 'A' && comma[2] < 'Z') comma[2]++;
                    else comma[2] = 'A';
                    printf("[TA %d] BEFORE WRITE rubric[%d] change %c -> %c\n", id, i + 1, oldVal, comma[2]);
                    save_rubric(data, rubricFile);
                    printf("[TA %d] AFTER  WRITE rubric[%d] saved\n", id, i + 1);
                }
            }
            printf("[TA %d] AFTER  READ rubric[%d] = '%s'\n", id, i, data->rubric[i]);
        }
        // Try to mark questions (optimistic, races allowed)
        for (int q = 0; q < RUBRIC_LINES; q++) {
            if (data->terminate) break;
            printf("[TA %d] BEFORE READ marked[%d] = %d\n", id, q, data->marked[q]);
            if (data->marked[q] == 0) {
                data->marked[q] = 1; // racy set (Part 2a)
                printf("[TA %d] AFTER  WRITE marked[%d] = 1\n", id, q);
                random_delay(1.0, 2.0);
                printf("[TA %d] Marked question %d for student %s", id, q + 1, data->exam[0]);
            }
        }
        // coordinator loads next exam after all questions appear marked
        if (id == 1) {
            int all = 1;
            for (int i = 0; i < RUBRIC_LINES; i++) if (data->marked[i] == 0) { all = 0; break; }
            if (all && !data->terminate) {
                printf("[TA %d] BEFORE loading next exam (index=%d)\n", id, data->currentExamIndex);
                data->currentExamIndex++;
                load_exam(data);
                printf("[TA %d] AFTER  loading exam (index=%d)\n", id, data->currentExamIndex);
            } else {
                // allow others to progress; short sleep to avoid tight spin
                usleep(10000);
            }
        } else {
            int my_gen = data->exam_generation;
            while (!data->terminate && data->exam_generation == my_gen) usleep(10000);
        }
    }
}

int main(int argc, char *argv[]) {
    if (argc < 4) {
        fprintf(stderr, "Usage: %s <num_TAs> <rubric_file> <exam_files...>\n", argv[0]);
        return EXIT_FAILURE;
    }
    int numTAs = atoi(argv[1]);
    const char *rubricFile = argv[2];
    int totalExams = argc - 3;

    // create a token file for ftok
    FILE *tf = fopen("shmfile", "a"); if (tf) fclose(tf);
    key_t key = ftok("shmfile", 65);
    int shmid = shmget(key, sizeof(struct SharedData), 0666 | IPC_CREAT);
    if (shmid == -1) die("shmget");
    struct SharedData *data = (struct SharedData *) shmat(shmid, NULL, 0);
    if (data == (void *)-1) die("shmat");

    data->totalExams = totalExams;
    data->currentExamIndex = 0;
    data->exam_generation = 0;
    data->terminate = 0;
    for (int i = 0; i < totalExams; i++) {
        strncpy(data->examFiles[i], argv[i + 3], 255);
        data->examFiles[i][255] = '\0';
    }

    load_rubric(data, rubricFile);
    load_exam(data);

    for (int i = 0; i < numTAs; i++) {
        pid_t pid = fork();
        if (pid == 0) {
            TA_process(i + 1, data, rubricFile);
            shmdt(data);
            exit(0);
        }
    }
    for (int i = 0; i < numTAs; i++) wait(NULL);

    shmdt(data);
    shmctl(shmid, IPC_RMID, NULL);
    return EXIT_SUCCESS;
}
