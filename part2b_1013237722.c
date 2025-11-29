#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <string.h>
#include <time.h>
#include <semaphore.h>
#include <fcntl.h>

#define RUBRIC_LINES 5
#define MAX_LINE_LEN 100
#define EXAM_LINES 10

struct SharedData {
    char rubric[RUBRIC_LINES][MAX_LINE_LEN];
    char exam[EXAM_LINES][MAX_LINE_LEN];
    int currentExamIndex;
    int totalExams;
    char examFiles[50][256];
    int marked[RUBRIC_LINES];
    int questions_remaining;
    int exam_generation;
    int terminate;
};

static sem_t *sem_rubric;
static sem_t *sem_exam;
static sem_t *sem_question;

static void die(const char *msg) { perror(msg); exit(EXIT_FAILURE); }

static void random_delay(double min_sec, double max_sec) {
    double range = max_sec - min_sec;
    double delay = min_sec + ((double)rand() / RAND_MAX) * range;
    usleep((useconds_t)(delay * 1000000));
}

static void save_rubric(struct SharedData *data, const char *rubricFile) {
    FILE *f = fopen(rubricFile, "w");
    if (!f) die("Failed to write rubric file");
    for (int i = 0; i < RUBRIC_LINES; i++) fprintf(f, "%s", data->rubric[i]);
    fclose(f);
}

static void load_rubric(struct SharedData *data, const char *rubricFile) {
    FILE *f = fopen(rubricFile, "r");
    if (!f) die("Failed to open rubric file");
    for (int i = 0; i < RUBRIC_LINES; i++) {
        if (fgets(data->rubric[i], MAX_LINE_LEN, f) == NULL)
            snprintf(data->rubric[i], MAX_LINE_LEN, "%d, %c\n", i + 1, 'A' + i);
    }
    fclose(f);
}

static void load_exam(struct SharedData *data) {
    const char *fname = data->examFiles[data->currentExamIndex];
    printf("[COORD] BEFORE READ exam file '%s' (index=%d)\n", fname, data->currentExamIndex);
    FILE *f = fopen(fname, "r");
    if (!f) die("Failed to open exam file");
    for (int i = 0; i < EXAM_LINES; i++) data->exam[i][0] = '\0';
    for (int i = 0; i < EXAM_LINES; i++) if (fgets(data->exam[i], MAX_LINE_LEN, f) == NULL) break;
    fclose(f);
    for (int i = 0; i < RUBRIC_LINES; i++) data->marked[i] = 0;
    data->questions_remaining = RUBRIC_LINES;
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
        if (data->terminate) { printf("[TA %d] Termination signal received. Exiting.\n", id); break; }
        printf("[TA %d] Checking rubric...\n", id);
        for (int i = 0; i < RUBRIC_LINES; i++) {
            printf("[TA %d] BEFORE READ rubric[%d] = '%s'\n", id, i, data->rubric[i]);
            random_delay(0.5, 1.0);
            if (rand() % 5 == 0) {
                sem_wait(sem_rubric);
                char *comma = strchr(data->rubric[i], ',');
                if (comma && *(comma + 2) != '\0') {
                    char oldVal = comma[2];
                    if (comma[2] >= 'A' && comma[2] < 'Z') comma[2]++;
                    else comma[2] = 'A';
                    printf("[TA %d] BEFORE WRITE rubric[%d] change %c -> %c\n", id, i + 1, oldVal, comma[2]);
                    save_rubric(data, rubricFile);
                    printf("[TA %d] AFTER  WRITE rubric[%d] saved\n", id, i + 1);
                }
                sem_post(sem_rubric);
            }
            printf("[TA %d] AFTER  READ rubric[%d] = '%s'\n", id, i, data->rubric[i]);
        }
        for (int q = 0; q < RUBRIC_LINES; q++) {
            sem_wait(sem_question);
            printf("[TA %d] BEFORE READ marked[%d] = %d\n", id, q, data->marked[q]);
            if (data->marked[q] == 0) {
                data->marked[q] = 1;
                data->questions_remaining--;
                printf("[TA %d] AFTER  WRITE marked[%d] = 1, remaining=%d\n", id, q, data->questions_remaining);
                sem_post(sem_question);
                random_delay(1.0, 2.0);
                printf("[TA %d] Marked question %d for student %s", id, q + 1, data->exam[0]);
            } else {
                sem_post(sem_question);
            }
        }
        // attempt to load next exam when current is fully marked
        sem_wait(sem_exam);
        if (data->questions_remaining == 0) {
            if (data->terminate) {
                sem_post(sem_exam);
                printf("[TA %d] Finished marking sentinel exam. Exiting.\n", id);
                break;
            }
            // advance index and load next exam
            printf("[TA %d] BEFORE loading next exam (index=%d)\n", id, data->currentExamIndex);
            data->currentExamIndex++;
            if (data->currentExamIndex >= data->totalExams) {
                // If out of files, also terminate as a safeguard
                data->terminate = 1;
                sem_post(sem_exam);
                printf("[TA %d] No more exam files. Exiting.\n", id);
                break;
            }
            load_exam(data);
            sem_post(sem_exam);
            printf("[TA %d] AFTER  loading exam (index=%d)\n", id, data->currentExamIndex);
        } else {
            sem_post(sem_exam);
            int my_gen = data->exam_generation;
            while (!data->terminate && data->exam_generation == my_gen) usleep(10000);
        }
    }
}

int main(int argc, char *argv[]) {
    if (argc < 4) { fprintf(stderr, "Usage: %s <num_TAs> <rubric_file> <exam_files...>\n", argv[0]); return EXIT_FAILURE; }
    int numTAs = atoi(argv[1]);
    const char *rubricFile = argv[2];
    int totalExams = argc - 3;

    int shmid = shmget(IPC_PRIVATE, sizeof(struct SharedData), 0666 | IPC_CREAT);
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

    sem_rubric   = sem_open("/rubric_sem_1013237722",  O_CREAT, 0644, 1);
    sem_exam     = sem_open("/exam_sem_1013237722",    O_CREAT, 0644, 1);
    sem_question = sem_open("/question_sem_1013237722",O_CREAT, 0644, 1);
    if (sem_rubric == SEM_FAILED || sem_exam == SEM_FAILED || sem_question == SEM_FAILED) die("sem_open");

    for (int i = 0; i < numTAs; i++) {
        pid_t pid = fork();
        if (pid == 0) { TA_process(i + 1, data, rubricFile); shmdt(data); exit(0); }
    }
    for (int i = 0; i < numTAs; i++) wait(NULL);

    sem_close(sem_rubric); sem_close(sem_exam); sem_close(sem_question);
    sem_unlink("/rubric_sem_1013237722"); sem_unlink("/exam_sem_1013237722"); sem_unlink("/question_sem_1013237722");
    shmdt(data); shmctl(shmid, IPC_RMID, NULL);
    return EXIT_SUCCESS;
}
