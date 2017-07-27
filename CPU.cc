#include <iostream>
#include <list>
#include <iterator>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <sstream>
/*
This program does the following.
1) Create handlers for two signals.
2) Create an idle process which will be executed when there is nothing
   else to do.
3) Create a send_signals process that sends a SIGALRM every so often
When run, it should produce the following output (approximately):
$ ./a.out
in CPU.cc at 247 main pid = 26428
state:    1
name:     IDLE
pid:      26430
ppid:     0
slices:   0
switches: 0
started:  0
in CPU.cc at 100 at beginning of send_signals getpid () = 26429
in CPU.cc at 216 idle getpid () = 26430
in CPU.cc at 222 going to sleep
in CPU.cc at 106 sending signal = 14
in CPU.cc at 107 to pid = 26428
in CPU.cc at 148 stopped running->pid = 26430
in CPU.cc at 155 continuing tocont->pid = 26430
in CPU.cc at 106 sending signal = 14
in CPU.cc at 107 to pid = 26428
in CPU.cc at 148 stopped running->pid = 26430
in CPU.cc at 155 continuing tocont->pid = 26430
in CPU.cc at 106 sending signal = 14
in CPU.cc at 107 to pid = 26428
in CPU.cc at 115 at end of send_signals
Terminated
---------------------------------------------------------------------------
Add the following functionality.
1) Change the NUM_SECONDS to 20.
2) Take any number of arguments for executables, and place each on new_list.
    The executable will not require arguments themselves.
3) When a SIGALRM arrives, scheduler() will be called. It calls
    choose_process which currently always returns the idle process. Do the
    following.
    a) Update the PCB for the process that was interrupted including the
        number of context switches and interrupts it had, and changing its
        state from RUNNING to READY.
    b) If there are any processes on the new_list, do the following.
        i) Take the one off the new_list and put it on the processes list.
        ii) Change its state to RUNNING, and fork() and execl() it.
    c) Modify choose_process to round robin the processes in the processes
        queue that are READY. If no process is READY in the queue, execute
        the idle process.
4) When a SIGCHLD arrives notifying that a child has exited, process_done() is
    called. process_done() currently only prints out the PID and the status.
    a) Add the printing of the information in the PCB including the number
        of times it was interrupted, the number of times it was context
        switched (this may be fewer than the interrupts if a process
        becomes the only non-idle process in the ready queue), and the total
        system time the process took.
    b) Change the state to TERMINATED.
    c) Start the idle process to use the rest of the time slice.
*/

#define NUM_SECONDS 20

// make sure the asserts work
#undef NDEBUG
#include <assert.h>

#define READ_END 0
#define WRITE_END 1

#define NUM_PIPES 10

#define P2K i
#define K2P i+1

#define WRITE(a) { const char *foo = a; write (1, foo, strlen (foo)); }

#define EBUG
#ifdef EBUG
#   define dmess(a) cout << "in " << __FILE__ << \
    " at " << __LINE__ << " " << a << endl;

#   define dprint(a) cout << "in " << __FILE__ << \
    " at " << __LINE__ << " " << (#a) << " = " << a << endl;

#   define dprintt(a,b) cout << "in " << __FILE__ << \
    " at " << __LINE__ << " " << a << " " << (#b) << " = " \
    << b << endl
#else
#   define dprint(a)
#endif /* EBUG */


using namespace std;

int pipes[NUM_PIPES][2];

enum STATE { NEW, RUNNING, WAITING, READY, TERMINATED };

/*
** a signal handler for those signals delivered to this process, but
** not already handled.
*/
void grab (int signum) { dprint (signum); }

// c++decl> declare ISV as array 32 of pointer to function (int) returning
// void
void (*ISV[32])(int) = {
/*        00    01    02    03    04    05    06    07    08    09 */
/*  0 */ grab, grab, grab, grab, grab, grab, grab, grab, grab, grab,
/* 10 */ grab, grab, grab, grab, grab, grab, grab, grab, grab, grab,
/* 20 */ grab, grab, grab, grab, grab, grab, grab, grab, grab, grab,
/* 30 */ grab, grab
};

struct PCB
{
    STATE state;
    const char *name;   // name of the executable
    int pid;            // process id from fork();
    int ppid;           // parent process id
    int interrupts;     // number of times interrupted
    int switches;       // may be < interrupts
    int started;        // the time this process started
    int pipeP2K[2]; 	// pipe from process to kernel
    int pipeK2P[2];		// pipe from kernel to process
};

/*
** an overloaded output operator that prints a PCB
*/
ostream& operator << (ostream &os, struct PCB *pcb)
{
    os << "state:        " << pcb->state << endl;
    os << "name:         " << pcb->name << endl;
    os << "pid:          " << pcb->pid << endl;
    os << "ppid:         " << pcb->ppid << endl;
    os << "interrupts:   " << pcb->interrupts << endl;
    os << "switches:     " << pcb->switches << endl;
    os << "started:      " << pcb->started << endl;
    return (os);
}

/*
** an overloaded output operator that prints a list of PCBs
*/
ostream& operator << (ostream &os, list<PCB *> which)
{
    list<PCB *>::iterator PCB_iter;
    for (PCB_iter = which.begin(); PCB_iter != which.end(); PCB_iter++)
    {
        os << (*PCB_iter);
    }
    return (os);
}

PCB *running;
PCB *idle;

// http://www.cplusplus.com/reference/list/list/
list<PCB *> new_list;
list<PCB *> processes;

int sys_time;

/*
**  send signal to process pid every interval for number of times.
*/
void send_signals (int signal, int pid, int interval, int number)
{
    dprintt ("at beginning of send_signals", getpid ());

    for (int i = 1; i <= number; i++)
    {
        sleep (interval);

        dprintt ("sending", signal);
        dprintt ("to", pid);

        if (kill (pid, signal) == -1)
        {
            perror ("kill");
            return;
        }
    }
    dmess ("at end of send_signals");
}

struct sigaction *create_handler (int signum, void (*handler)(int))
{
    struct sigaction *action = new (struct sigaction);

    action->sa_handler = handler;
/*
**  SA_NOCLDSTOP
**  If  signum  is  SIGCHLD, do not receive notification when
**  child processes stop (i.e., when child processes  receive
**  one of SIGSTOP, SIGTSTP, SIGTTIN or SIGTTOU).
*/
    if (signum == SIGCHLD)
    {
        action->sa_flags = SA_NOCLDSTOP;
    }
    else
    {
        action->sa_flags = 0;
    }

    sigemptyset (&(action->sa_mask));

    assert (sigaction (signum, action, NULL) == 0);
    return (action);
}

PCB* choose_process ()
{   
    PCB* choosen_process = NULL;
    //Updating current running PCB for interrupts, switches, and state.
    int currentInterrupts = running->interrupts;
    int currentSwitches = running->switches;
    currentInterrupts++;
    currentSwitches++;

    running->interrupts = currentInterrupts;
    running->switches = currentSwitches;
    running->state = READY;

    //Check if there are any new processes
    if(new_list.size() > 0) {
        // Used http://www.cplusplus.com/reference/list/list/front/ 
        // and http://www.cplusplus.com/reference/list/list/pop_front/
        int newProcessPID;
        PCB* newProcess = new_list.front();
        const char *name = newProcess -> name;


        //Pipe code is taken from your main.cc
        // create the pipes
	    for (int i = 0; i < NUM_PIPES; i+=2)
	    {
	        // i is from process to kernel, K2P from kernel to process
	        assert (pipe (pipes[P2K]) == 0);
	        assert (pipe (pipes[K2P]) == 0);

	        // make the read end of the kernel pipe non-blocking.
	        assert (fcntl (pipes[P2K][READ_END], F_SETFL,
	            fcntl(pipes[P2K][READ_END], F_GETFL) | O_NONBLOCK) == 0);
	    }

		for (int i = 0; i < NUM_PIPES; i+=2)
	    {
	        int child;
	        if ((child = fork()) == 0)
	        {
	            close (pipes[P2K][READ_END]);
	            close (pipes[K2P][WRITE_END]);

	            // assign fildes 3 and 4 to the pipe ends in the child
	            dup2 (pipes[P2K][WRITE_END], 3);
	            dup2 (pipes[K2P][READ_END], 4);
	        }
	    }

        if((newProcessPID = fork()) == 0) {

            int excelError = execl(name, name, NULL);
            if(excelError < 0) {
                perror("Error occured during excel of new process");
            }
        }
        



    	newProcess->pipeP2K[READ_END] = pipes[P2K][READ_END];
     	newProcess->pipeP2K[WRITE_END] = pipes[P2K][WRITE_END];
    	newProcess->pipeK2P[READ_END] = pipes[K2P][READ_END];
    	newProcess->pipeK2P[WRITE_END] = pipes[K2P][WRITE_END];
        newProcess->pid = newProcessPID;
        newProcess->state = RUNNING;
        newProcess->started = sys_time;
        choosen_process = newProcess;
        processes.push_back(newProcess);
        new_list.pop_front();

    } else { //If no new processes round robin READY processes
        bool found = false;

        while(!found) {
            PCB* frontProcess = processes.front();
            STATE frontState = frontProcess->state;
            if(frontState == READY) {
                found = true;
                frontState = RUNNING;
                choosen_process = frontProcess;
            }

            processes.push_back(frontProcess);
            processes.pop_front();
        }
       
        if(choosen_process == NULL) {
            choosen_process = idle;
            idle->state = RUNNING;
        } 

    }
    choosen_process = running;

    return choosen_process;
}

void scheduler (int signum)
{
    assert (signum == SIGALRM);
    sys_time++;

    PCB* tocont = choose_process();

    dprintt ("continuing", tocont->pid);
    if (kill (tocont->pid, SIGCONT) == -1)
    {
        perror ("kill");
        return;
    }
}

void process_done (int signum)
{
    assert (signum == SIGCHLD);

    int status, cpid;

    cpid = waitpid (-1, &status, WNOHANG);

    dprintt ("in process_done", cpid);

    if  (cpid == -1)
    {
        perror ("waitpid");
    }
    else if (cpid == 0)
    {
        if (errno == EINTR) { return; }
        perror ("no children");
    }
    else
    {
        PCB* pcbOfTerminatedProcess = NULL;
        bool found = false;

        while(!found) {
            PCB* frontProcess = processes.front();
            int pid = frontProcess->pid;
            if(pid == cpid){
                found = true;
                frontProcess->state = TERMINATED;
                pcbOfTerminatedProcess = frontProcess;
            }
            processes.push_back(frontProcess);
            processes.pop_front();
        }
        

        if(pcbOfTerminatedProcess != NULL) {
            printf("terminated process information\n");
            cout <<pcbOfTerminatedProcess;
            int startTime = pcbOfTerminatedProcess->started;
            int totalTime = sys_time - startTime;
            printf("Total time process took: %d\n", totalTime);
        }
        

        dprint (WEXITSTATUS (status));
        idle->state = RUNNING;
        running = idle;
    }
}

void read_req (int signum) {
	assert(signum == SIGTRAP);
	WRITE("In read req -- polling all processes in process list\n");
	/*
    ** poll all the pipes as we don't know which process sent the trap, nor
    ** if more than one has arrived.
    */
    for (int i = 0; i < NUM_PIPES; i+=2)
    {
        char buf[1024];
        int num_read = read (pipes[P2K][READ_END], buf, 1023);
        if (num_read > 0)
        {	
        	PCB* waitingProcess;
            buf[num_read] = '\0';
            WRITE("kernel read: ");
            WRITE(buf);
            WRITE("\n");

            std::string messageIn (buf);

            std::string messageOut;

			for(int p = 0; p <processes.size(); p++) {
				PCB* process = processes.front();
				int processPid = process->pid;
				std::stringstream s;
				std::string strPid;
				s << processPid;
				strPid = s.str();

				if(messageIn.find(strPid)) {
					process->state = WAITING;
					waitingProcess = process;
					break;
				}
			}
            
            if(messageIn.find("ps")) {
            	WRITE("---- processes list\n");
            	std::string processList ("Process List:\n");
            	for(int p = 0; p <processes.size(); p++) {
            		PCB* process = processes.front();
            		const char *name = process->name;
            		std::string strName (name);
            		processList.append(strName).append("\n");
            		processes.push_back(process);
            		processes.pop_front();
            	}
            	messageOut = processList;
            } else if(messageIn.find("system time")) {
            	WRITE("---- system time\n");
            	string s;
				std::stringstream out;
				out << sys_time;
				s = out.str();
				messageOut = s;
            }

            // respond
            const char *message = messageOut.c_str();
            write (pipes[K2P][WRITE_END], message, strlen (message));
       		
       		waitingProcess->state=READY;
        }
    }
	
	WRITE("Leaving read_req\n");

}

/*./
** stop the running process and index into the ISV to call the ISR
*/
void ISR (int signum)
{
    if (kill (running->pid, SIGSTOP) == -1)
    {
        perror ("kill");
        return;
    }
    dprintt ("stopped", running->pid);

    ISV[signum](signum);
}

/*
** set up the "hardware"
*/
void boot (int pid)
{
    ISV[SIGALRM] = scheduler;       create_handler (SIGALRM, ISR);
    ISV[SIGCHLD] = process_done;    create_handler (SIGCHLD, ISR);
    ISV[SIGTRAP] = read_req;		create_handler (SIGTRAP, ISR); 

    // start up clock interrupt
    int ret;
    if ((ret = fork ()) == 0)
    {
        // signal this process once a second for three times
        send_signals (SIGALRM, pid, 1, NUM_SECONDS);

        // once that's done, really kill everything...
        kill (0, SIGTERM);
    }

    if (ret < 0)
    {
        perror ("fork");
    }
}

void create_idle ()
{
    int idlepid;

    if ((idlepid = fork ()) == 0)
    {
        dprintt ("idle", getpid ());

        // the pause might be interrupted, so we need to
        // repeat it forever.
        for (;;)
        {
            dmess ("going to sleep");
            pause ();
            if (errno == EINTR)
            {
                dmess ("waking up");
                continue;
            }
            perror ("pause");
        }
    }
    idle = new (PCB);
    idle->state = RUNNING;
    idle->name = "IDLE";
    idle->pid = idlepid;
    idle->ppid = 0;
    idle->interrupts = 0;
    idle->switches = 0;
    idle->started = sys_time;
}

int main (int argc, char **argv)
{
    int pid = getpid();
    dprintt ("main", pid);

    // Used http://www.cplusplus.com/reference/list/list/push_back/ 
    // as reference to adding to a list
    if(argc > 1) {
        
        for(int n = 1; n< argc; n++) {

            PCB* process = new (PCB);
            process-> state = NEW;
            process->name = argv[n];
            process->ppid = 0;
            process->interrupts = 0;
            process-> switches = 0;
            new_list.push_back(process);
        }
    } 

    sys_time = 0;

    boot (pid);

    // create a process to soak up cycles
    create_idle ();
    running = idle;

    cout << running;

    // we keep this process around so that the children don't die and
    // to keep the IRQs in place.
    for (;;)
    {
        pause();
        if (errno == EINTR) { continue; }
        perror ("pause");
    }
}