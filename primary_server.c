#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <errno.h>
#include <sys/shm.h>
#include <pthread.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>
#include <semaphore.h>
#include <fcntl.h>
#include <sys/stat.h>

#define PERMS 0644
#define N 30
struct my_msgbuf
{
    long mtype;
    char mtext[100];
};

struct shmseg
{
    int n;
    int adj[N][N];
};

struct thread_args
{
    int msqid;
    char mtext[100];
};

struct shmseg *get_shmp(int seq_no)
{
    struct shmseg *shmp;
    key_t key = ftok("load_balancer.c", seq_no);
    int shmid = shmget(key, sizeof(struct shmseg), PERMS);
    if (shmid == -1)
    {
        perror("shmget in primary server thread");
        exit(1);
    }
    shmp = shmat(shmid, NULL, 0);
    if (shmp == (void *)-1)
    {
        perror("shmat in primary server thread");
        exit(1);
    }
    return shmp;
}

void print_shmseg(struct shmseg *shmp)
{
    printf("Shared memory contents:-\n");
    printf("%d\n", shmp->n);
    for (int i = 0; i < shmp->n; ++i)
    {
        for (int j = 0; j < shmp->n; ++j)
            printf("%d ", shmp->adj[i][j]);
        printf("\n");
    }
}

int gnos[100];
int gcnt = 0;

int get_gno(char *filename)
{
    assert(isdigit(filename[1]));
    int gno = filename[1] - '0';
    if (isdigit(filename[2]))
        gno = gno * 10 + (filename[2] - '0');
    return gno;
}

void get_sem_names(char *rname, char *wname, int gno)
{
    sprintf(rname, "/r%d", gno);
    sprintf(wname, "/w%d", gno);
}

void *add_modify_graph(void *arg)
{
    struct thread_args *targs = (struct thread_args *)arg;
    int seq_no, op_no;
    char filename[10];
    sscanf(targs->mtext, "%d %d %s", &seq_no, &op_no, filename);

    struct shmseg *shmp = get_shmp(seq_no);

    print_shmseg(shmp);

    int gno = get_gno(filename);
    sem_t *reader;
    sem_t *writer;
    char rname[10], wname[10];
    get_sem_names(rname, wname, gno);
    printf("gno: %d, rname: %s, wname: %s\n", gno, rname, wname);

    if (op_no == 1)
    {
        reader = sem_open(rname, O_CREAT, S_IRUSR | S_IWUSR, 1);
        if (reader == SEM_FAILED)
        {
            perror("sem_open for reader in primary server");
            exit(1);
        }
        writer = sem_open(wname, O_CREAT, S_IRUSR | S_IWUSR, 1);
        if (writer == SEM_FAILED)
        {
            perror("sem_open for writer in primary server");
            exit(1);
        }
        gnos[gcnt] = gno;
        gcnt++;
    }
    else
    {
        reader = sem_open(rname, 0);
        if (reader == SEM_FAILED)
        {
            perror("sem_open for reader in primary server");
            exit(1);
        }
        writer = sem_open(wname, 0);
        if (writer == SEM_FAILED)
        {
            perror("sem_open for writer in primary server");
            exit(1);
        }
    }

    // Lock here
    sem_wait(writer);
    FILE *fp = fopen(filename, "w+");
    if (fp == NULL)
    {
        perror("fopen in primary server thread");
        exit(1);
    }
    fprintf(fp, "%d\n", shmp->n);
    for (int i = 0; i < shmp->n; ++i)
    {
        for (int j = 0; j < shmp->n; ++j)
            fprintf(fp, "%d ", shmp->adj[i][j]);
        fprintf(fp, "\n");
    }
    fclose(fp);
    sem_post(writer);
    // Release lock here

    sem_close(reader);
    sem_close(writer);

    if (shmdt(shmp) == -1)
    {
        perror("shmdt in primary server thread");
        exit(1);
    }

    struct my_msgbuf buf;

    if (op_no == 1)
        strcpy(buf.mtext, "File successfully added");
    else
        strcpy(buf.mtext, "File successfully modified");

    int len = strlen(buf.mtext);
    buf.mtype = seq_no + 100;
    if (msgsnd(targs->msqid, &buf, len + 1, 0) == -1)
    {
        perror("msgsnd in primary server thread");
        exit(1);
    }
}

void destroy_sem()
{
    for (int i = 0; i < gcnt; ++i)
    {
        int gno = gnos[i];
        char rname[10], wname[10];
        get_sem_names(rname, wname, gno);
        sem_unlink(rname);
        sem_unlink(wname);
    }
}

int main(int argc, char **argv)
{
    if (argc != 2)
    {
        perror("Usage: ./ps <mtype>");
        exit(1);
    }

    struct my_msgbuf buf;
    int msqid;
    int len;
    key_t key;

    if ((key = ftok("load_balancer.c", 100)) == -1)
    {
        perror("ftok in primary server");
        exit(1);
    }

    if ((msqid = msgget(key, PERMS)) == -1)
    {
        perror("msgget in primary server");
        exit(1);
    }
    printf("Primary server: ready to receive messages\n");

    int msg_type = atoi(argv[1]);
    pthread_t tids[100];
    struct thread_args *targs = malloc(100 * sizeof(struct thread_args));
    int tcnt = 0;

    while (1)
    {
        if (msgrcv(msqid, &buf, sizeof(buf.mtext), msg_type, 0) == -1)
        {
            perror("msgrcv in primary server");
            exit(1);
        }
        if (strcmp(buf.mtext, "terminate") == 0)
        {
            break;
        }
        strcpy(targs[tcnt].mtext, buf.mtext);
        targs[tcnt].msqid = msqid;
        pthread_create(&tids[tcnt], NULL, add_modify_graph, (void *)(&targs[tcnt]));
        tcnt++;
    }

    for (int i = 0; i < tcnt; ++i)
        pthread_join(tids[i], NULL);

    destroy_sem();

    free(targs);
}