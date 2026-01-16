#include "types.h"
#include "user.h"
#include "pstat.h"

#define stdout 1

int 
main(void)
{
    struct pstat proc_info;
    memset(&proc_info, 0, sizeof(struct pstat));

    // Fork 3 processes with 10x<i> tickets
    for(int i =1; i<4; i++)
    {
        int pid = fork();
        if(pid != 0) continue;

        if(settickets(i*10) < 0)
        {
            printf(stdout, "Failed to set num processes!\n");
            continue;
        }

        // Loop forever
        sleep(100); // give Kernel a chance to create both processes before cpus are flip/flopped
        while(1){;}
    }

    while(uptime() < 100000)
    {
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

        sleep(100);
    }

    printf(stdout, "Reached 100000 ticks uptime!\n");
}