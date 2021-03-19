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
  struct proc* queue[NPROC];
  int head;
  int tail;
  int size;
} ptable;

static struct proc *initproc;

int nextpid = 1;
extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);

void enqueue(struct proc* p) { // add the process to the tail of queue
  // cprintf("Here is the size before enqueue %d\n", ptable.size);
  // if(ptable.size == 0 && ptable.head == 0 && ptable.tail == 0){ // first time to enqueue
  //   ptable.queue[ptable.head] = p;
  //   ptable.size++;
  // } else if (ptable.size < NPROC - 1) {
  //   ptable.tail = (ptable.tail + 1) % NPROC;
  //   ptable.queue[ptable.tail] = p;
  //   ptable.size++;
  // }
  if (ptable.size < NPROC -1){
    ptable.tail = (ptable.tail + 1) % NPROC;
    ptable.queue[ptable.tail] = p;
    ptable.size++;
  }
  else {
    panic("enqueue failed.\n");
  }
  // cprintf("Here is the size after enqueue %d\n", ptable.size);
}

void dequeue() {
  // cprintf("Here is the size before dequeue %d\n", ptable.size);
  if (ptable.size != 0){ // move head to the next one
    ptable.head = (ptable.head + 1) % NPROC;
    ptable.size--;
  }
  // cprintf("Here is the size after dequeue %d\n", ptable.size);
}

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
  // cprintf("We are in allocproc, and initialized all required field\n");
  p->compticks = 0;
  p->schedticks = 0;
  p->sleepticks = 0;
  p->switches = 0;
  p->curticks = 0;  
  p->target_tick = 0;
  p->cur_sleep_ticks = 0;  // intialize the all required fields
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

void
ptableinit(void)
{
  ptable.head = 0;
  ptable.tail = NPROC - 1;
  ptable.size = 0; // the queue is empty in the beginning
}

//PAGEBREAK: 32
// Set up first user process.
void
userinit(void)
{
  struct proc *p;
  extern char _binary_initcode_start[], _binary_initcode_size[];

  ptableinit();  // initialize the queue in ptable
  // cprintf("We are here in userinit\n");
  p = allocproc();
  // cprintf("We have allocated the first user process\n");
  
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

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  // this assignment to p->state lets other cores
  // run this process. the acquire forces the above
  // writes to be visible, and the lock is also needed
  // because the assignment might not be atomic.
  acquire(&ptable.lock);

  // cprintf("We have add this user process to the queue\n");
  p->state = RUNNABLE;
  p->time_slice = 1; // initial user process has time_slice 1
  p->switches = 1; // because it will be schedule first
  enqueue(p); // add the first user process to queue

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
fork2(int slice)
{ 
  if (slice <= 0){
    return -1;
  }

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
  np->parent = curproc;
  *np->tf = *curproc->tf;

  // Clear %eax so that fork returns 0 in the child.
  np->tf->eax = 0;

  for(i = 0; i < NOFILE; i++)
    if(curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));

  pid = np->pid;

  // cprintf("We are in the fork2 function\n");
  
  acquire(&ptable.lock);
  
  // enqueue(np); // add this process to the queue
  // cprintf("We are in fork2 enqueue\n");
  np->state = RUNNABLE;
  np->time_slice = slice; // set up the process to its parent slice
  enqueue(np); // add this process to the queue

  release(&ptable.lock);

  return pid;
}

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int
fork(void)
{
  // cprintf("We are in the fork function\n");
  int slice = getslice(myproc()->pid); // get caller's slice
  return fork2(slice);
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

  if(curproc == initproc)
    panic("init exiting");

  // Close all open files.
  for(fd = 0; fd < NOFILE; fd++){
    if(curproc->ofile[fd]){
      fileclose(curproc->ofile[fd]);
      curproc->ofile[fd] = 0;
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

  dequeue(); // remove the exited process from queue
  // Jump into the scheduler, never to return.
  curproc->state = ZOMBIE;
  // cprintf("We are in exit stage and call dequeue\n");
  // dequeue(); // remove the exited process from queue
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
      havekids = 1;
      if(p->state == ZOMBIE){
        // Found one.
        pid = p->pid;
        kfree(p->kstack);
        p->kstack = 0;
        freevm(p->pgdir);
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
    // cprintf("We are in the wait stage and it called sleep\n");

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(curproc, &ptable.lock);  //DOC: wait-sleep
  }
}

int setslice(int pid, int slice){
  struct proc *cur_p;
  if (pid < 0 || slice <= 0){
    return -1;
  }
  acquire(&ptable.lock);
  for(cur_p = ptable.proc; cur_p < &ptable.proc[NPROC]; cur_p++){
    if (cur_p->pid == pid){
      cur_p->time_slice = slice;
      release(&ptable.lock);
      return 0;
    }
  }
  release(&ptable.lock);
  return -1;
}

int getslice(int pid){
  struct proc *cur_p;
  acquire(&ptable.lock);
  for(cur_p = ptable.proc; cur_p < &ptable.proc[NPROC]; cur_p++){
    if (cur_p->pid == pid){
      release(&ptable.lock);
      return cur_p->time_slice;
    }
  }
  release(&ptable.lock);
  return -1;
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

  for(;;){
    // Enable interrupts on this processor.
    sti();

    // Loop over process table looking for process to run.
    acquire(&ptable.lock);
    
    p = ptable.queue[ptable.head]; // we get the head (which is current running process)
    if(p == 0){ // it is used for no runnable process
      release(&ptable.lock);
      continue;
    }
    // cprintf("This is the pid for process %d\n", p->pid);
    // cprintf("This is the name of process %s\n", p->name);

    if (p->curticks < (p->time_slice + p->cur_sleep_ticks)){
      p->curticks++;
      p->schedticks++;
      if (p->curticks > p->time_slice){
        p->compticks++;
      }
    } else {
      dequeue();
      if (p->state != SLEEPING){
        enqueue(p);
      }
      p->curticks = 0;
      p->cur_sleep_ticks = 0;
      p->switches++;
      release(&ptable.lock);
      continue;
    }

    // Switch to chosen process.  It is the process's job
    // to release ptable.lock and then reacquire it
    // before jumping back to us.
    c->proc = p;
    switchuvm(p);
    p->state = RUNNING;

    swtch(&(c->scheduler), p->context);
    switchkvm();

    // Process is done running for now.
    // It should have changed its p->state before coming back.
    c->proc = 0;
    release(&ptable.lock);

    // it means we should deschedule the current process
    // if(p->curticks >= p->time_slice + p->cur_sleep_ticks){ // need to check here again, should we increment first or check slice first
    //   // cprintf("Here is the current time_slice for this process %d\n", p->time_slice);
    //   int next = (ptable.head + 1) % NPROC; // move to the next process
    //   p->curticks = 0; // we are ready to deschedule it, so updat its current tick to 0 for next time
    //   p->state = RUNNABLE; // mark the process into RUNNABLE state for next time
    //   dequeue(); // remove it from queue
    //   enqueue(p); // add it to tail
    //   // cprintf("We dequeue the process\n");

    //   if(ptable.size == 1){ // only current one are ready for next time, still increment its swithces number
    //     p->switches++;
    //     // cprintf("We are in the condition ptable.size == 1\n");
    //     // cprintf("The process name is %s\n", p->name);
    //   } else{ // switch to new one
    //     ptable.head = next;
    //     p = ptable.queue[ptable.head]; // update p to chosen process
    //     p->switches++;  // update its number of switches
    //     // cprintf("We are in the condition to switch to new one\n");
    //   }

    //   // Switch to chosen process.  It is the process's job
    //   // to release ptable.lock and then reacquire it
    //   // before jumping back to us.
    //   c->proc = p;
    //   switchuvm(p);
    //   p->state = RUNNING;

    //   swtch(&(c->scheduler), p->context);
    //   switchkvm();

    //   // Process is done running for now.
    //   // It should have changed its p->state before coming back.
    //   c->proc = 0;
    // } else{
    //   p->curticks++;
    //   p->schedticks++;
    // }

    //  release(&ptable.lock);

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
  // cprintf("We are in sched and passed the myproc()\n");

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
  // p->cur_sleep_ticks = 0;
  // cprintf("The sleep process name is %s\n", p->name);
  // cprintf("We are in sleep stage and call dequeue\n");
  //dequeue(); // move the current sleeping process out of queue
  // panic("We are dequeue here\n");

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

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == SLEEPING && p->chan == chan){
      // acquire(&tickslock);
      if(chan == &ticks){
        if(ticks >= p->target_tick){
          p->state = RUNNABLE;
          enqueue(p);
        } 
      } else{
        p->state = RUNNABLE;
        enqueue(p);
      }
      // if (ticks >= p->target_tick && chan != &ticks){
      //   // panic("We are in wakeup");
      //   p->state = RUNNABLE;
      //   enqueue(p);
      // } else {
      //   p->sleepticks++;
      //   p->cur_sleep_ticks++; // track the compticks
      // }
      // release(&tickslock);
    }
  }
  // struct proc *p;

  // for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  //   if(p->state == SLEEPING && p->chan == chan)
  //     p->state = RUNNABLE;
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

int getpinfo(struct pstat* stat) {
  // stat = (struct pstat*) malloc(sizeof(struct pstat*));
  // if(stat == 0) {
  //   return -1;
  // }

  int index = 0;  // index to put info into pstat
  struct proc *p;
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if (p->state != UNUSED){
      stat->inuse[index] = 1;
    } else {
      stat->inuse[index] = 0;
    }
    stat->pid[index] = p->pid;
    stat->timeslice[index] = p->time_slice;
    stat->compticks[index] = p->compticks;
    stat->schedticks[index] = p->schedticks;
    stat->sleepticks[index] = p->sleepticks;
    stat->switches[index] = p->switches;

    index++;
  }

  return 0;
}

