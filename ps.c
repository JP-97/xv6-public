#include "types.h"
#include "user.h"
#include "pstat.h"

#define stdout 1

/**
 * @brief Dump some basic info for all running processes.
 */
static void dump_process_info(void)
{
    struct pstat proc_info;

    if(getpinfo(&proc_info) < 0){
        printf(stdout, "Failed to get system process info!\n");
        exit();
    }

    // Print out info for running processes
    printf(stdout, "PID    TIME    TICKETS\n");
    for(int i=0; i < NPROC; i++)
    {
        if(!proc_info.inuse[i])
            continue;

        printf(stdout, "%d      %d       %d\n", 
            proc_info.pid[i],
            proc_info.ticks[i],
            proc_info.tickets[i]);
    }
}


int main(void)
{
    dump_process_info();
    return 0;
}