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
#include <sstream>

#define READ 0
#define WRITE 1

#define TO_KERNEL 3
#define FROM_KERNEL 4

int main (int argc, char** argv)
{
    int pid = getpid();
    int ppid = getppid();

    std::string strPid;
    std::stringstream out;
    out << pid;
    strPid = out.str();

    printf ("writing in pid %d\n", pid);
    strPid.append(" ps");
    const char *message = strPid.c_str();
    write (TO_KERNEL, message, strlen (message));

    printf ("trapping to %d in pid %d\n", ppid, pid);
    kill (ppid, SIGTRAP);

    printf ("reading in pid %d\n", pid);
    char buf[1024];
    int num_read = read (FROM_KERNEL, buf, 1023);
    buf[num_read] = '\0';
    printf ("process %d read: %s\n", pid, buf);


    sleep(20);
    pid = getpid();
    ppid = getppid();

    std::string strPid2;
    std::stringstream out2;
    out2 << pid;
    strPid2 = out2.str();

    printf ("writing in pid %d\n", pid);
    strPid2.append(" system time");
    const char *message2 = strPid2.c_str();

    write (TO_KERNEL, message2, strlen (message2));

    printf ("trapping to %d in pid %d\n", ppid, pid);
    kill (ppid, SIGTRAP);

    printf ("reading in pid %d\n", pid);
    char buf2[1024];
    int num_read2 = read (FROM_KERNEL, buf2, 1023);
    buf2[num_read2] = '\0';
    printf ("process %d read: %s\n", pid, buf2);

    exit (0);
}
