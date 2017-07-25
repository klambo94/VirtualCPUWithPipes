#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <time.h>

#define READ 0
#define WRITE 1

#define TO_KERNEL 3
#define FROM_KERNEL 4

int main (int argc, char** argv)
{
    int pid = getpid();
    int ppid = getppid();

    printf ("writing in pid %d\n", pid);
    const char *message = "ps";
    write (TO_KERNEL, message, strlen (message));

    printf ("trapping to %d in pid %d\n", ppid, pid);
    kill (ppid, SIGTRAP);

    printf ("reading in pid %d\n", pid);
    char buf[1024];
    int num_read = read (FROM_KERNEL, buf, 1023);
    buf[num_read] = '\0';
    printf ("process %d read: %s\n", pid, buf);


    sleep(10);

    printf ("writing in pid %d\n", pid);
    const char *message = "system time";
    write (TO_KERNEL, message, strlen (message));

    printf ("trapping to %d in pid %d\n", ppid, pid);
    kill (ppid, SIGTRAP);

    printf ("reading in pid %d\n", pid);
    char buf[1024];
    int num_read = read (FROM_KERNEL, buf, 1023);
    buf[num_read] = '\0';
    printf ("process %d read: %s\n", pid, buf);
    exit (0);
}
