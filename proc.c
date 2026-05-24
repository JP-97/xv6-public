#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"
#include "pstat.h"

struct {
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;

static struct proc *initproc;

int nextpid = 1;
extern void forkret(void);
extern void trapret(void);

static uint state = 1;
static void wakeup1(void *chan);
static uint holdlottery(uint totalnumtix);
static uint random(uint *state, uint max);

void
pinit(void)
{
  initlock(&ptable.lock, "ptable");
}

// Must be called with interrupts disabled
int
cpuid() {
  return mycpu()-cpus;
}

// Must be called with interrupts disabled to avoid the caller being
// rescheduled between reading lapicid and running through the loop.
struct cpu*
mycpu(void)
{
  int apicid, i;
  
  if(readeflags()&FL_IF)
    panic("mycpu called with interrupts enabled\n");
  
  apicid = lapicid();
  // APIC IDs are not guaranteed to be contiguous. Maybe we should have
  // a reverse map, or reserve a register to store &cpus[i].
  for (i = 0; i < ncpu; ++i) {
    if (cpus[i].apicid == apicid)
      return &cpus[i];
  }

  panic("unknown apicid\n");
}

// Disable interrupts so that we are not rescheduled
// while reading proc from the cpu structure
struct proc*
myproc(void) {
  struct cpu *c;
  struct proc *p;
  pushcli();
  c = mycpu();
  p = c->proc;
  popcli();
  return p;
}

//PAGEBREAK: 32
// Look in the process table for an UNUSED proc.
// If found, change state to EMBRYO and initialize
// state required to run in the kernel.
// Otherwise return 0.
static struct proc*
allocproc(void)
{
  struct proc *p;
  char *sp;

  acquire(&ptable.lock);

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == UNUSED)
      goto found;

  release(&ptable.lock);
  return 0;

found:
  p->state = EMBRYO;
  p->pid = nextpid++;
  p->time = 0;

  release(&ptable.lock);

  // Allocate kernel stack.
  if((p->kstack = kalloc()) == 0){
    p->state = UNUSED;
    return 0;
  }
  sp = p->kstack + KSTACKSIZE;

  // Leave room for trap frame.
  sp -= sizeof *p->tf;
  p->tf = (struct trapframe*)sp;

  // Set up new context to start executing at forkret,
  // which returns to trapret.
  sp -= 4;
  *(uint*)sp = (uint)trapret;

  sp -= sizeof *p->context;
  p->context = (struct context*)sp;
  memset(p->context, 0, sizeof *p->context);
  p->context->eip = (uint)forkret;

  return p;
}

//PAGEBREAK: 32
// Set up first user process.
void
userinit(void)
{
  struct proc *p;
  extern char _binary_initcode_start[], _binary_initcode_size[];

  p = allocproc();
  
  initproc = p;
  if((p->pgdir = setupkvm()) == 0)
    panic("userinit: out of memory?");
  inituvm(p->pgdir, _binary_initcode_start, (int)_binary_initcode_size);
  p->sz = PGSIZE;
  memset(p->tf, 0, sizeof(*p->tf));
  p->tf->cs = (SEG_UCODE << 3) | DPL_USER;
  p->tf->ds = (SEG_UDATA << 3) | DPL_USER;
  p->tf->es = p->tf->ds;
  p->tf->ss = p->tf->ds;
  p->tf->eflags = FL_IF;
  p->tf->esp = PGSIZE;
  p->tf->eip = 0;  // beginning of initcode.S

  p->numthreads = 1;

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  p->numtix = DEFAULT_NUM_TICKETS;

  // this assignment to p->state lets other cores
  // run this process. the acquire forces the above
  // writes to be visible, and the lock is also needed
  // because the assignment might not be atomic.
  acquire(&ptable.lock);

  p->state = RUNNABLE;

  release(&ptable.lock);
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint sz;
  struct proc *curproc = myproc();

  sz = curproc->sz;
  if(n > 0){
    if((sz = allocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  } else if(n < 0){
    if((sz = deallocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  curproc->sz = sz;
  switchuvm(curproc);
  return 0;
}

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int
fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *curproc = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // Copy process state from proc.
  if((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0){
    kfree(np->kstack);
    np->kstack = 0;
    np->state = UNUSED;
    return -1;
  }

  np->sz = curproc->sz;
  np->mainpid = np;
  np->parent = curproc;
  *np->tf = *curproc->tf;
  np->numthreads = 1;

  // Clear %eax so that fork returns 0 in the child.
  np->tf->eax = 0;

  for(i = 0; i < NOFILE; i++)
    if(curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));

  pid = np->pid;

  // Child process inherits same number of tickets allocated to parent
  np->numtix = curproc->numtix;

  acquire(&ptable.lock);

  np->state = RUNNABLE;

  release(&ptable.lock);

  return pid;
}

// Creates a new 'process' (thread) which re-uses the VM and fds
// of the caller.
// Newly created proc will have its own kern thread, state,
// pid, tf, context, chan, etc.
// Processes created through a call to clone will be linked
// to their parent proc via parent proc ID.
// Returns newly allocated TID, otherwise -1;
int clone(int (*fn)(void *), void* stack, void* fn_args, struct clone_args* clargs)
{
  // int i, pid;
  struct proc *nt;
  struct proc *curproc = myproc();

  // Get/Validate clone input params
  // Currently, clargs doesn't do anything useful.
  if (!fn || (uint)fn > curproc->sz)
  {
    cprintf("Invalid thread function reference!\n");
    return -1;
  }
 
  if (!stack || (uint)stack > curproc->sz)
  {
    cprintf("Invalid stack reference!\n");
    return -1;
  }

  cprintf("CLONE: function ptr: %p, stack ptr: %p\n", fn, stack);

  // Allocate new process (TID), create kstack and set up
  // initial trap frame.
  // This will set up the kernel thread to start in forkret
  // which will release the ptable lock and immediately fall through to trapret,
  // which will pop the tf registers for our cloned thread.
  if((nt = allocproc()) == 0){
    return -1;
  }

  // new thread will share the same VM as the caller
  // therefore the size and pgdir are copied 1:1
  nt->sz = curproc->sz;
  nt->pgdir = curproc->pgdir;

  // Threads have no hierarchy...
  // Point everything back to initial thread that was created during fork
  nt->parent = curproc->mainpid; 
  nt->mainpid = curproc->mainpid;

  cprintf("TID %d, Parent TID is:%d\n", nt->pid, nt->mainpid->pid);

  // Set the stack and instructions pointers for the new
  // thread to point to the stac/function specified by the
  // original caller

  // TODO: Not sure where, but likely in uspace lib, make sure this allocs a guardpage
  // TODO: Figure out how to stage args passed to clone on new stack so they can be consumed by fn - just need to put them in right spot
  nt->tf->esp = (uint) stack; 
  nt->tf->eip = (uint)fn;


  // Set up the segment selectors
  // These are used to satisfy the flat segmentation
  // model when we switch to the new thread 
  nt->tf->cs = (SEG_UCODE << 3) | DPL_USER;
  nt->tf->ds = (SEG_UDATA << 3) | DPL_USER;
  nt->tf->ss = nt->tf->ds;
  nt->tf->es = nt->tf->ds;
  nt->tf->eflags = FL_IF;

  nt->numtix = DEFAULT_NUM_TICKETS;
  nt->numthreads = 1;
  nt->cwd = curproc->cwd;
  safestrcpy(nt->name, "clone", sizeof(nt->name));

  // Copy open FDs
  for(int i = 0; i < (sizeof(curproc->ofile)/sizeof(struct file*)); i++){
    nt->ofile[i] = curproc->ofile[i];
  }

  acquire(&ptable.lock);
  nt->state = RUNNABLE;
  nt->parent->numthreads++;
  release(&ptable.lock);

  return nt->pid;
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
void
exit(void)
{
  struct proc *curproc = myproc();
  struct proc *p;
  int fd;
  int hasOtherActiveThread;

  if(curproc == initproc)
    panic("init exiting");

  // Trap main thread in loop while we sentence other threads in the
  // the group to death and wait for them to exit()
  if(curproc->mainpid->pid == curproc->pid){
    while(1){
      hasOtherActiveThread = 0;
      acquire(&ptable.lock);
      for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
        if(p->mainpid->pid == curproc->pid && p->state != ZOMBIE)
        {
          // Sentence any other threads that are part of
          // thread group to death
          p->killed =1;
          hasOtherActiveThread = 1;
          p->parent = initproc; // re-parent to initproc since main thread died!
          wakeup1(initproc);
        }
      }
      release(&ptable.lock);

      if(hasOtherActiveThread){
        // cprintf("still other threads!\n");
        yield(); // Give up the CPU so that children, which have been marked as killed, can exit()
      }
      else
        break;
    }
  }


  // TODO !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!! This can't be closed until all children are reaped
  // Close all open files.
  if(curproc->mainpid->pid == curproc->pid){
    for(fd = 0; fd < NOFILE; fd++){
      if(curproc->ofile[fd]){
        fileclose(curproc->ofile[fd]);
        curproc->ofile[fd] = 0;
      }
    }
  }


  begin_op();
  iput(curproc->cwd);
  end_op();
  curproc->cwd = 0;

  acquire(&ptable.lock);

  // Parent might be sleeping in wait().
  wakeup1(curproc->parent);

  // Pass abandoned children to init.
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->parent == curproc){
      p->parent = initproc;
      if(p->state == ZOMBIE)
        wakeup1(initproc);
    }
  }

  // if(curproc->mainpid->state == ZOMBIE && curproc->pid != curproc->mainpid->pid){
  //   // Re-parent child thread to initproc since the
  //   // main thread is already dead!

  // }

  // Jump into the scheduler, never to return.
  cprintf("Setting %d to ZOMBIE!\n", curproc->pid);
  curproc->state = ZOMBIE;
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(void)
{
  struct proc *p;
  int havekids, pid;
  struct proc *curproc = myproc();
  
  acquire(&ptable.lock);
  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->parent != curproc)
        continue;

      pid = p->pid;

      // Check to make sure that no "child" threads
      // are still active. If there are, go back to
      // sleep such that they can continue using resources.
      if(pid == p->mainpid->pid){

        for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
          if(p->mainpid->pid != pid)
            continue;
          else if(p->state != UNUSED)
            // Child thread is still alive or not yet reaped  
            break;
        }
      }

      havekids = 1;
      if(p->state == ZOMBIE){
        // Found one.
        if(pid == p->mainpid->pid){
          // All of a child thread's memory
          // is managed by parent.
          // The child stack is part of parent's heap
          // and so having child free it could corrupt the heap. 
          kfree(p->kstack);
          p->kstack = 0;
          freevm(p->pgdir);
        }
          
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        p->state = UNUSED;
        release(&ptable.lock);
        return pid;
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || curproc->killed){
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(curproc, &ptable.lock);  //DOC: wait-sleep
  }
}

//PAGEBREAK: 42
// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run
//  - swtch to start running that process
//  - eventually that process transfers control
//      via swtch back to the scheduler.
void
scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();
  c->proc = 0;
  uint totalnumtix;
  uint ticketscounted;
  
  for(;;){
    totalnumtix = 0;
    ticketscounted = 0;

    // Enable interrupts on this processor.
    sti();

    // Loop over process table looking for process to run.
    // Note that interrupts will be disabled again as part of acquire
    acquire(&ptable.lock);

    // Calculate the total number of tickets in system
    // based on runnable processes
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->state == RUNNABLE)
        // TODO: Eventually, if runnable processes are in their own table, no need for this check
        totalnumtix += p->numtix;
    }
    
    if(totalnumtix == 0)
      // No runnable process was found... nothing more to do
      goto done;

    // Hold lottery and figure out the lucky winner
    uint winner = holdlottery(totalnumtix);

    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->state != RUNNABLE)
        continue;

      else if(winner > (ticketscounted + p->numtix)){
        // winning ticket is not within selected process' "range"
        ticketscounted += p->numtix;
        continue;
      }
      
      // p's ticket count falls in the range of (ticketscounted, ticketscounted + p->numtix) 
      break;
    }

    if(p->state != RUNNABLE)
      // No winner process was found... nothing more to do
      goto done;

    // Switch to chosen process.  It is the process's job
    // to release ptable.lock and then reacquire it
    // before jumping back to us.
    c->proc = p;
    switchuvm(p);
    p->state = RUNNING;

    swtch(&(c->scheduler), p->context);
    switchkvm();

done:
    // Process is done running for now.
    // It should have changed its p->state before coming back.
    c->proc = 0;

    release(&ptable.lock);
  }
}

// Enter scheduler.  Must hold only ptable.lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->ncli, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&ptable.lock))
    panic("sched ptable.lock");
  if(mycpu()->ncli != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched running");
  if(readeflags()&FL_IF)
    panic("sched interruptible");
  intena = mycpu()->intena;
  swtch(&p->context, mycpu()->scheduler);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  acquire(&ptable.lock);  //DOC: yieldlock
  myproc()->state = RUNNABLE;
  sched();
  release(&ptable.lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch here.  "Return" to user space.
void
forkret(void)
{
  static int first = 1;
  // Still holding ptable.lock from scheduler.
  release(&ptable.lock);

  if (first) {
    // Some initialization functions must be run in the context
    // of a regular process (e.g., they call sleep), and thus cannot
    // be run from main().
    first = 0;
    iinit(ROOTDEV);
    initlog(ROOTDEV);
  }

  // Return to "caller", actually trapret (see allocproc).
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  
  if(p == 0)
    panic("sleep");

  if(lk == 0)
    panic("sleep without lk");

  // Must acquire ptable.lock in order to
  // change p->state and then call sched.
  // Once we hold ptable.lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup runs with ptable.lock locked),
  // so it's okay to release lk.
  if(lk != &ptable.lock){  //DOC: sleeplock0
    acquire(&ptable.lock);  //DOC: sleeplock1
    
    release(lk);
  }
  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  if(lk != &ptable.lock){  //DOC: sleeplock2
    release(&ptable.lock);
    acquire(lk);
  }
}

//PAGEBREAK!
// Wake up all processes sleeping on chan.
// The ptable lock must be held.
static void
wakeup1(void *chan)
{
  struct proc *p;

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == SLEEPING && p->chan == chan)
      p->state = RUNNABLE;
}

static uint
random(uint *state, uint max)
{
  *state = ((unsigned long) *state) * 48271 % 0x7fffffff;
  return *state % max;
}

// Hold a lottery to decide the lucky winner that gets
// to run next
static uint
holdlottery(uint totalnumtix)
{
  return totalnumtix ? random(&state, totalnumtix) : 0;
}

int
setnumtix(uint amount)
{
  acquire(&ptable.lock);

  struct proc *currproc = myproc();
  
  currproc->numtix = amount;

  release(&ptable.lock);
  return 0;
}

// Wake up all processes sleeping on chan.
void
wakeup(void *chan)
{
  acquire(&ptable.lock);
  wakeup1(chan);
  release(&ptable.lock);
}

// Kill the process with the given pid.
// Process won't exit until it returns
// to user space (see trap in trap.c).
int
kill(int pid)
{
  struct proc *p;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){
      p->killed = 1;
      // Wake process from sleep if necessary.
      if(p->state == SLEEPING)
        p->state = RUNNABLE;
      release(&ptable.lock);
      return 0;
    }
  }
  release(&ptable.lock);
  return -1;
}

//PAGEBREAK: 36
// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [EMBRYO]    "embryo",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  int i;
  struct proc *p;
  char *state;
  uint pc[10];

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    cprintf("%d %s %s", p->pid, state, p->name);
    if(p->state == SLEEPING){
      getcallerpcs((uint*)p->context->ebp+2, pc);
      for(i=0; i<10 && pc[i] != 0; i++)
        cprintf(" %p", pc[i]);
    }
    cprintf("\n");
  }
}

// Update p at index i based on proc pref
static void
updatepstat(struct pstat *p, int index, struct proc *pref)
{
  if(!pref)
    return;
  else if(index < 0 || index >= NPROC)
    return;

  p->inuse[index] = (pref->state == RUNNING || pref->state == SLEEPING || pref->state == RUNNABLE) ? 1 : 0;
  p->tickets[index] = pref->numtix;
  p->pid[index] = pref->pid;
  p->ticks[index] = pref->time;
}

int
getprocessinfo(struct pstat *pinfo)
{
  if(!pinfo)
  {
    cprintf("Invalid process info reference!\n");
    return -1;
  }
  
  acquire(&ptable.lock);
  
  for(int i=0; i < NPROC; i++)
  {
    updatepstat(pinfo, i, &ptable.proc[i]);
  }

  release(&ptable.lock);
  return 0;
}
