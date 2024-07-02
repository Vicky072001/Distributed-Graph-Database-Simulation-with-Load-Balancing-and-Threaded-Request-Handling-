#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>

#define PERMS 0644
struct my_msgbuf
{
    long mtype;
    char mtext[200];
};

int main(void)
{
    struct my_msgbuf buf;
    int msqid;
    int len;
    key_t key;

    if ((key = ftok("load_balancer.c", 100)) == -1)
    {
        perror("ftok in cleanup");
        exit(1);
    }

    if ((msqid = msgget(key, PERMS)) == -1)
    {
        perror("msgget in cleanup");
        exit(1);
    }

    while (1)
    {
        printf("Do you want to terminate the server? (Y/N)\n");
        char response[10];
        scanf("%s", response);

        if ((strcmp(response, "Y") == 0 || strcmp(response, "y") == 0))
        {
            buf.mtype = 1;
            sprintf(buf.mtext, "%s", "terminate");
            len = strlen(buf.mtext);
            if (msgsnd(msqid, &buf, len + 1, 0) == -1)
            {
                perror("msgsnd in cleanup");
                exit(1);
            }
            break;
        }
    }

    return 0;
}