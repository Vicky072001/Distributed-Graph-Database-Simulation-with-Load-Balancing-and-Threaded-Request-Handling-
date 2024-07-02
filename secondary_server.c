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
#include <unistd.h>

#define PERMS 0644
#define N 30
struct my_msgbuf
{
    long mtype;
    char mtext[100];
};

struct thread_args
{
    int msqid;
    char mtext[100];
};

struct shmseg
{
    int n;
    int adj[N][N];
};

int *get_shmp(int seq_no)
{
    int *shmp;
    key_t key = ftok("load_balancer.c", seq_no);
    int shmid = shmget(key, sizeof(int), PERMS);
    if (shmid == -1)
    {
        perror("shmget in secondary server thread");
        exit(1);
    }
    shmp = shmat(shmid, NULL, 0);
    if (shmp == (void *)-1)
    {
        perror("shmat in secondary server thread");
        exit(1);
    }
    return shmp;
}

void print_shmseg(int *shmp)
{
    printf("Shared memory contents:-\n");
    printf("%d\n", *shmp);
}

struct shmseg graph[100];
int l[100], r[100];
int queue[100][N], vis[100][N];
pthread_mutex_t qlock[100];

struct relax_args
{
    int seq_no;
    int s;
};

void *relax_neighbors(void *arg)
{
    struct relax_args *rargs = (struct relax_args *)arg;
    int seq_no = rargs->seq_no;
    int s = rargs->s;

    for (int i = 0; i < graph[seq_no].n; ++i)
    {
        // Since the graph is a tree, there are no race conditions in checking vis[i]
        if (graph[seq_no].adj[s][i] != 0 && vis[seq_no][i] == 0)
        {
            vis[seq_no][i] = 1;
            pthread_mutex_lock(&qlock[seq_no]);
            queue[seq_no][r[seq_no]] = i;
            r[seq_no]++;
            pthread_mutex_unlock(&qlock[seq_no]);
        }
    }
}

int *parallel_bfs(int seq_no, int src)
{
    int *vertex_list = malloc(sizeof(int) * (graph[seq_no].n + 1));
    vertex_list[0] = graph[seq_no].n;
    int ptr = 1;
    if (pthread_mutex_init(&qlock[seq_no], NULL) != 0)
    {
        perror("pthread_mutex_init in parallel_bfs");
        exit(1);
    }
    l[seq_no] = 0, r[seq_no] = 0;
    memset(vis[seq_no], 0, graph[seq_no].n);

    vis[seq_no][src] = 1;
    queue[seq_no][r[seq_no]] = src;
    r[seq_no]++;
    while (l[seq_no] < r[seq_no])
    {
        int curl = l[seq_no], curr = r[seq_no];

        pthread_t threads[curr - curl];
        struct relax_args *args = malloc((curr - curl) * sizeof(struct relax_args));

        for (int i = curl; i < curr; ++i)
        {
            int s = queue[seq_no][i];
            vertex_list[ptr] = s;
            ptr++;
            args[i - curl].seq_no = seq_no;
            args[i - curl].s = s;
            pthread_create(&threads[i - curl], NULL, relax_neighbors, (void *)&args[i - curl]);
        }
        for (int i = 0; i < curr - curl; ++i)
        {
            pthread_join(threads[i], NULL);
        }
        free(args);
        l[seq_no] = curr;
    }

    if (pthread_mutex_destroy(&qlock[seq_no]) != 0)
    {
        perror("pthread_mutex_destroy in parallel_bfs");
        exit(1);
    }
    return vertex_list;
}

struct dfs_args
{
    int seq_no;
    int s;
    int p;
};

void *dfs(void *args)
{
    struct dfs_args *dargs = (struct dfs_args *)args;
    int seq_no = dargs->seq_no;
    int s = dargs->s;
    int p = dargs->p;
    // printf("s: %d, p: %d\n", s, p);
    vis[seq_no][s] = 1;
    struct dfs_args *cargs = malloc(graph[seq_no].n * sizeof(struct dfs_args));
    pthread_t threads[graph[seq_no].n];
    int ccnt = 0;
    for (int i = 0; i < graph[seq_no].n; ++i)
    {
        if (i != p && graph[seq_no].adj[s][i] == 1)
        {
            cargs[ccnt].seq_no = seq_no;
            cargs[ccnt].p = s;
            cargs[ccnt].s = i;
            pthread_create(&threads[ccnt], NULL, dfs, (void *)&cargs[ccnt]);
            ccnt++;
        }
    }
    if (ccnt == 0)
    {
        // printf("r: %d, s: %d\n", r[seq_no], s);
        pthread_mutex_lock(&qlock[seq_no]);
        queue[seq_no][r[seq_no]] = s;
        r[seq_no]++;
        pthread_mutex_unlock(&qlock[seq_no]);
    }
    for (int i = 0; i < ccnt; ++i)
    {
        pthread_join(threads[i], NULL);
    }
}

int *parallel_dfs(int seq_no, int src)
{
    if (pthread_mutex_init(&qlock[seq_no], NULL) != 0)
    {
        perror("pthread_mutex_init in parallel_dfs");
        exit(1);
    }
    struct dfs_args args;
    args.seq_no = seq_no;
    args.s = src;
    args.p = -1;
    pthread_t tid;
    pthread_create(&tid, NULL, dfs, (void *)&args);
    pthread_join(tid, NULL);
    if (pthread_mutex_destroy(&qlock[seq_no]) != 0)
    {
        perror("pthread_mutex_destroy in parallel_dfs");
        exit(1);
    }
    int *vertex_list = malloc(sizeof(int) * (r[seq_no] + 1));
    // printf("r: %d\n", r[seq_no]);
    vertex_list[0] = r[seq_no];
    for (int i = 0; i < r[seq_no]; ++i)
    {
        vertex_list[i + 1] = queue[seq_no][i];
        // printf("vertex_list[%d]=%d\n", i + 1, vertex_list[i + 1]);
    }
    return vertex_list;
}

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

int reader_cnt = 0;

void *bfs_dfs_graph(void *arg)
{
    struct thread_args *targs = (struct thread_args *)arg;
    int seq_no, op_no;
    char filename[10];
    sscanf(targs->mtext, "%d %d %s", &seq_no, &op_no, filename);

    int *shmp = get_shmp(seq_no);

    print_shmseg(shmp);
    (*shmp)--;

    int gno = get_gno(filename);
    sem_t *reader;
    sem_t *writer;
    char rname[10], wname[10];
    get_sem_names(rname, wname, gno);

    printf("gno: %d, rname: %s, wname: %s\n", gno, rname, wname);
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

    // Lock here
    sem_wait(reader);
    reader_cnt++;
    if (reader_cnt == 1) {
        sem_wait(writer);
    }
    sem_post(reader);
    sleep(30);
    FILE *fp = fopen(filename, "r+");
    if (fp == NULL)
    {
        perror("fopen in secondary server thread");
        exit(1);
    }
    fscanf(fp, "%d", &graph[seq_no].n);
    for (int i = 0; i < graph[seq_no].n; ++i)
    {
        for (int j = 0; j < graph[seq_no].n; ++j)
        {
            fscanf(fp, "%d", &graph[seq_no].adj[i][j]);
        }
    }
    fclose(fp);

    sem_wait(reader);
    reader_cnt--;
    if (reader_cnt == 0) {
        sem_post(writer);
    }
    sem_post(reader);
    // Release lock here

    sem_close(reader);
    sem_close(writer);

    int *vertex_list;
    if (op_no == 3)
        vertex_list = parallel_dfs(seq_no, *shmp);
    else
        vertex_list = parallel_bfs(seq_no, *shmp);

    if (shmdt(shmp) == -1)
    {
        perror("shmdt in secondary server thread");
        exit(1);
    }

    struct my_msgbuf buf;

    buf.mtext[0] = '\0';
    for (int i = 1; i <= vertex_list[0]; ++i)
    {
        // printf("vertex_list[%d]=%d\n", i, vertex_list[i]);
        char temp[5];
        sprintf(temp, "%d ", vertex_list[i] + 1);
        strcat(buf.mtext, temp);
    }
    free(vertex_list);

    int len = strlen(buf.mtext);
    buf.mtype = seq_no + 100;
    if (msgsnd(targs->msqid, &buf, len + 1, 0) == -1)
    {
        perror("msgsnd in secondary server thread");
        exit(1);
    }
}

int main(int argc, char **argv)
{
    if (argc != 2)
    {
        perror("Usage: ./ss <mtype>");
        exit(1);
    }

    struct my_msgbuf buf;
    int msqid;
    int len;
    key_t key;

    if ((key = ftok("load_balancer.c", 100)) == -1)
    {
        perror("ftok in secondary server");
        exit(1);
    }

    if ((msqid = msgget(key, PERMS)) == -1)
    {
        perror("msgget in secondary server");
        exit(1);
    }
    printf("Secondary server: ready to receive messages\n");

    int msg_type = atoi(argv[1]);
    pthread_t tids[100];
    struct thread_args *targs = malloc(100 * sizeof(struct thread_args));
    int tcnt = 0;

    while (1)
    {
        if (msgrcv(msqid, &buf, sizeof(buf.mtext), msg_type, 0) == -1)
        {
            perror("msgrcv in secondary server");
            exit(1);
        }
        if (strcmp(buf.mtext, "terminate") == 0)
        {
            break;
        }
        strcpy(targs[tcnt].mtext, buf.mtext);
        targs[tcnt].msqid = msqid;
        pthread_create(&tids[tcnt], NULL, bfs_dfs_graph, (void *)(&targs[tcnt]));
        tcnt++;
    }

    for (int i = 0; i < tcnt; ++i)
        pthread_join(tids[i], NULL);
    free(targs);

}