#include "types.h"
#include "x86.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "pstat.h"
#include "clone.h"

int
sys_fork(void)
{
  return fork();
}

int
sys_clone(void)
{
  char* fn;
  char* stack;
  char* args;
  char* clargs;
  
  if(argptr(0, &fn, sizeof(void*)) != 0)
  {
    return -1;
  }

  if(argptr(1, &stack, sizeof(void*)) != 0)
  {
    return -1;
  }
  
  if(argptr(2, &args, sizeof(void*)) != 0)
  {
    return -1;
  }

  if(argptr(3, &clargs, sizeof(struct clone_args*)) != 0)
  {
    return -1;
  }

  return clone((int (*)(void*)) fn,
               (void *) stack,
               (void *) args,
               (struct clone_args *) clargs);
}

int
sys_exit(void)
{
  exit();
  return 0;  // not reached
}

int
sys_wait(void)
{
  return wait();
}

int
sys_kill(void)
{
  int pid;

  if(argint(0, &pid) < 0)
    return -1;
  return kill(pid);
}

int
sys_getpid(void)
{
  return myproc()->pid;
}

int
sys_sbrk(void)
{
  int addr;
  int n;

  if(argint(0, &n) < 0)
    return -1;
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

int
sys_sleep(void)
{
  int n;
  uint ticks0;

  if(argint(0, &n) < 0)
    return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    struct proc *p = myproc();
    if(p->killed){
      release(&tickslock);
      return -1;
    }

    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

// return how many clock tick interrupts have occurred
// since start.
int
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

// Set number of tickets allocated to calling process.
// Higher number means higher scehduling frequency.
int
sys_settickets(void)
{
  int n;

  if(argint(0, &n) < 0)
    return -1;

  else if(n < MIN_NUM_TICKETS || n > MAX_NUM_TICKETS)
    return -1;

  return setnumtix(n);
}

// Get information about all processes in the system
int
sys_getpinfo(void)
{
  struct pstat *result;

  if(argptr(0, (void *)&result, sizeof(struct pstat)) < 0)
    return -1;

  return getprocessinfo(result);
}

