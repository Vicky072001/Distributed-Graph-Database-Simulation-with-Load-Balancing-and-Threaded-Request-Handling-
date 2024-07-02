#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/shm.h>

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

void display_menu()
{
    printf("1. Add a new graph to the database\n");
    printf("2. Modify an existing graph of the database\n");
    printf("3. Perform DFS on a graph of the database\n");
    printf("4. Perform BFS on a graph of the database\n");
}

int main(void)
{
    struct my_msgbuf buf;
    int msqid;
    int len;
    key_t key;

    if ((key = ftok("load_balancer.c", 100)) == -1)
    {
        perror("ftok in client");
        exit(1);
    }

    if ((msqid = msgget(key, PERMS)) == -1)
    {
        perror("msgget in client");
        exit(1);
    }
    printf("Client: ready to send messages\n");

    // printf("Enter Client-ID: ");
    // int client_id;
    // scanf("%d", &client_id);
    int client_id = getpid();

    while (1)
    {
        display_menu();

        int seq_no;
        printf("Enter Sequence Number: ");
        scanf("%d", &seq_no);

        int op_no;
        printf("Enter Operation Number: ");
        scanf("%d", &op_no);

        char filename[10];
        printf("Enter Graph File Name: ");
        scanf("%s", filename);

        if (op_no == 1 || op_no == 2)
        {
            printf("Creating a shared memory segment\n");
            struct shmseg *shmp;
            key = ftok("load_balancer.c", seq_no);
            int shmid = shmget(key, sizeof(struct shmseg), IPC_CREAT | PERMS);
            if (shmid == -1)
            {
                perror("shmget in client");
                exit(1);
            }
            shmp = shmat(shmid, NULL, 0);
            if (shmp == (void *)-1)
            {
                perror("shmat in client");
                exit(1);
            }
            printf("Successfully created shared memory segment\n");

            printf("Enter number of nodes of the graph\n");
            printf("Enter adjacency matrix, each row on a separate line and elements of a single row separated by whitespace characters\n");

            scanf("%d", &(shmp->n));
            for (int i = 0; i < shmp->n; ++i)
            {
                for (int j = 0; j < shmp->n; ++j)
                {
                    scanf("%d", &(shmp->adj[i][j]));
                }
            }
            sprintf(buf.mtext, "%d %d %s", seq_no, op_no, filename);
            len = strlen(buf.mtext);
            buf.mtype = seq_no;
            if (msgsnd(msqid, &buf, len + 1, 0) == -1)
            {
                perror("msgsnd in client");
                exit(1);
            }

            if (msgrcv(msqid, &buf, sizeof(buf.mtext), seq_no + 100, 0) == -1)
            {
                perror("msgrcv in client");
                exit(1);
            }

            printf("Response: %s\n", buf.mtext);

            if (shmdt(shmp) == -1)
            {
                perror("shmdt in client");
                exit(1);
            }

            if (shmctl(shmid, IPC_RMID, 0) == -1)
            {
                perror("shmctl in client");
                exit(1);
            }
        }
        else
        {
            printf("Creating a shared memory segment\n");
            int *shmp;
            key = ftok("load_balancer.c", seq_no);
            int shmid = shmget(key, sizeof(int), IPC_CREAT | PERMS);
            if (shmid == -1)
            {
                perror("shmget in client");
                exit(1);
            }
            shmp = shmat(shmid, NULL, 0);
            if (shmp == (void *)-1)
            {
                perror("shmat in client");
                exit(1);
            }
            printf("Successfully created shared memory segment\n");

            printf("Enter starting vertex\n");
            
            scanf("%d", shmp);
            sprintf(buf.mtext, "%d %d %s", seq_no, op_no, filename);
            len = strlen(buf.mtext);
            buf.mtype = seq_no;
            if (msgsnd(msqid, &buf, len + 1, 0) == -1)
            {
                perror("msgsnd in client");
                exit(1);
            }

            if (msgrcv(msqid, &buf, sizeof(buf.mtext), seq_no + 100, 0) == -1)
            {
                perror("msgrcv in client");
                exit(1);
            }

            printf("Response: %s\n", buf.mtext);

            if (shmdt(shmp) == -1)
            {
                perror("shmdt in client");
                exit(1);
            }

            if (shmctl(shmid, IPC_RMID, 0) == -1)
            {
                perror("shmctl in client");
                exit(1);
            }
        }
    }

    printf("Client: exit.\n");

    return 0;
}