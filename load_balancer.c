#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <string.h>

#define PERMS 0644
struct my_msgbuf
{
    long mtype;
    char mtext[100];
};

int main(int argc, char **argv)
{
    if (argc != 4)
    {
        perror("Usage: ./lb <mtype of ps> <mtype of ss1> <mtype of ss2>");
        exit(1);
    }
    int ps_mtype = atoi(argv[1]);
    int ss1_mtype = atoi(argv[2]);
    int ss2_mtype = atoi(argv[3]);

    struct my_msgbuf buf;
    int msqid;
    key_t key;
    int len;

    if ((key = ftok("load_balancer.c", 100)) == -1)
    {
        perror("ftok in load balancer");
        exit(1);
    }

    if ((msqid = msgget(key, PERMS | IPC_CREAT)) == -1)
    {
        perror("msgget in load balancer");
        exit(1);
    }
    printf("Load balancer: ready to receive messages\n");

    while (1)
    {
        if (msgrcv(msqid, &buf, sizeof(buf.mtext), -100, 0) == -1)
        {
            perror("msgrcv in load balancer");
            exit(1);
        }
        if (strcmp(buf.mtext, "terminate") == 0)
        {
            break;
        }

        int seq_no, op_no;
        char filename[10];
        sscanf(buf.mtext, "%d %d %s", &seq_no, &op_no, filename);
        printf("Received: %d %d %s\n", seq_no, op_no, filename);

        len = strlen(buf.mtext);
        if (op_no == 1 || op_no == 2)
            buf.mtype = ps_mtype;
        else if (seq_no % 2 == 0)
            buf.mtype = ss1_mtype;
        else
            buf.mtype = ss2_mtype;
        
        if (msgsnd(msqid, &buf, len + 1, 0) == -1)
        {
            perror("msgsnd in load balancer");
            exit(1);
        }
    }
    len = strlen(buf.mtext);
    buf.mtype = ps_mtype;
    if (msgsnd(msqid, &buf, len + 1, 0) == -1)
    {
        perror("msgsnd in load balancer");
        exit(1);
    }
    buf.mtype = ss1_mtype;
    if (msgsnd(msqid, &buf, len + 1, 0) == -1)
    {
        perror("msgsnd in load balancer");
        exit(1);
    }
    buf.mtype = ss2_mtype;
    if (msgsnd(msqid, &buf, len + 1, 0) == -1)
    {
        perror("msgsnd in load balancer");
        exit(1);
    }

    sleep(2);

    if (msgctl(msqid, IPC_RMID, NULL) == -1)
    {
        perror("msgctl");
        exit(1);
    }

    printf("Load balancer: exit\n");

    return 0;
}
