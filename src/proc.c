#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

struct cpu cpus[NCPU];

struct proc proc[NPROC];

struct proc *initproc;

int nextpid = 1;
struct spinlock pid_lock;

extern void forkret(void);
static void freeproc(struct proc *p);

extern char trampoline[]; // trampoline.S

// helps ensure that wakeups of wait()ing
// parents are not lost. helps obey the
// memory model when using p->parent.
// must be acquired before any p->lock.
struct spinlock wait_lock;

void log_cpu_state(struct proc *);

struct RBTree
{
  int count;
  int treeWeight;
  struct proc *root;
  struct proc *minvRuntime;
  struct spinlock lock;
  int period;
} tree;

struct RBTree *procTree = &tree;

// Set target scheduler latency and minimum granularity constants
// Latency must be multiples of min_granularity
static int latency = NPROC / 2;
static int min_granularity = 2;

// This function will initialize the red black tree data structure.
void rbinit(struct RBTree *tree, char *lock_name)
{
  initlock(&tree->lock, lock_name);
  tree->count = 0;
  tree->root = 0;
  tree->treeWeight = 0;
  tree->minvRuntime = 0;

  // Initially set time slice factor for all processes
  tree->period = latency;
}

// This function will calculate each individual process's weight in respect to it's nice value.
int calculateWight(int niceVal)
{
  if (niceVal > 30)
  {
    niceVal = 30;
  }
  else if (niceVal < 0)
  {
    niceVal = 0;
  }

  double denom = 1.25;
  int i = 0;
  while (i < niceVal && niceVal > 0)
  {
    denom *= 1.25;
  }

  int result = (int)(1024 / denom);
  return result;
}

// This function will determine if the tree is empty or not,
// i.e the tree has no processes in it.
int isEmpty(struct RBTree *tree)
{
  if (tree->count == 0)
  {
    return 1;
  }
  else
  {
    return 0;
  }
}

// This function will determine if the tree is full,
// i.e the maximum alloted number of processes in the system
int isFull(struct RBTree *tree)
{
  if (tree->count == NPROC)
  {
    return 1;
  }
  else
  {
    return 0;
  }
}

// This process retrieval functions will retrive the
// grandparent process of the process passed into the functions.
struct proc *
getGrandparentProc(struct proc *process)
{
  if (process != 0 && process->pParent != 0)
  {
    return process->pParent->pParent;
  }
  return 0;
}

// This process retrieval functions will retrive the
// uncle process of the process passed into the functions.
struct proc *
getUncleProc(struct proc *process)
{
  struct proc *grandparent = getGrandparentProc(process);
  if (grandparent != 0)
  {
    if (process->pParent == grandparent->left)
    {
      return grandparent->right;
    }
    else
    {
      return grandparent->left;
    }
  }
  return 0;
}

// This function will perform a rotation on the process
// structure in the tree that is passed into the function.
// It will perform a left rotation, where it will move down
// leftward in the tree and its right process will be moved up to its place.
void rotateLeft(struct RBTree *tree, struct proc *positionProc)
{
  struct proc *save_right_Proc = positionProc->right;

  positionProc->right = save_right_Proc->left;
  if (save_right_Proc->left != 0)
  {
    save_right_Proc->left->pParent = positionProc;
  }
  save_right_Proc->pParent = positionProc->pParent;

  if (positionProc->pParent == 0)
  {
    tree->root = save_right_Proc;
  }
  else if (positionProc == positionProc->pParent->left)
  {
    positionProc->pParent->left = save_right_Proc;
  }
  else
  {
    positionProc->pParent->right = save_right_Proc;
  }
  save_right_Proc->left = positionProc;
  positionProc->pParent = save_right_Proc;
}

// This function will perform a rotation on the process
// structure in the tree that is passed into the function.
// It will perform a right rotation, where it will move down
// rightward in the tree and its left process will be moved up to its place.
void rotateRight(struct RBTree *tree, struct proc *positionProc)
{
  struct proc *save_left_Proc = positionProc->left;

  positionProc->left = save_left_Proc->right;

  if (save_left_Proc->right != 0)
  {
    save_left_Proc->right->pParent = positionProc;
  }
  save_left_Proc->pParent = positionProc->pParent;
  if (positionProc->pParent == 0)
  {
    tree->root = save_left_Proc;
  }
  else if (positionProc == positionProc->pParent->right)
  {
    positionProc->pParent->right = save_left_Proc;
  }
  else
  {
    positionProc->pParent->left = save_left_Proc;
  }
  save_left_Proc->right = positionProc;
  positionProc->pParent = save_left_Proc;
}

// This function will return a pointer to the address
// of the process with the smallest Virtual Runtime.
// It will do this by traversing through the left
// branch of the tree until it reaches the process.
struct proc *
setMinimumVRuntimeproc(struct proc *traversingProcess)
{
  if (traversingProcess != 0)
  {
    if (traversingProcess->left != 0)
    {
      return setMinimumVRuntimeproc(traversingProcess->left);
    }
    else
    {
      return traversingProcess;
    }
  }
  return 0;
}

struct proc *
insertProc(struct proc *traversingProcess, struct proc *insertingProcess)
{
  insertingProcess->color = RED;

  // i.e it is root or at leaf of tree
  if (traversingProcess == 0)
  {
    return insertingProcess;
  }
  // i.e everything after root
  // move process to the right of the current subtree
  if (traversingProcess->virtualRuntime <= insertingProcess->virtualRuntime)
  {
    insertingProcess->pParent = traversingProcess;
    traversingProcess->right = insertProc(traversingProcess->right, insertingProcess);
  }
  else
  {
    insertingProcess->pParent = traversingProcess;
    traversingProcess->left = insertProc(traversingProcess->left, insertingProcess);
  }

  return traversingProcess;
}

// This function will contain different cases that will incorporate
// the properties for a red black tree. It will utilize the integer
// value to determine which case need to be handled.
// cases:
// -1: if the current inserted process is the root
// -2: if the current inserted process's parent is black
// -3: if both parent and uncle processes are red, then repaint them black
// -4: if parent is red and uncle is black, but current process is red and
//     the current process is right child of parent that is left of grandparent or vice versa
// -5: same as case four but the current process is left child of parent that is left of grandparent or vice versa
void insertionCases(struct RBTree *tree, struct proc *rbProcess, int cases)
{
  struct proc *uncle;
  struct proc *grandparent;

  switch (cases)
  {
  case 1:
    if (rbProcess->pParent == 0)
      rbProcess->color = BLACK;
    else
      insertionCases(tree, rbProcess, 2);
    break;

  case 2:
    if (rbProcess->pParent->color == RED)
      insertionCases(tree, rbProcess, 3);
    break;

  case 3:
    uncle = getUncleProc(rbProcess);

    if (uncle != 0 && uncle->color == RED)
    {
      rbProcess->pParent->color = BLACK;
      uncle->color = BLACK;
      grandparent = getGrandparentProc(rbProcess);
      grandparent->color = RED;
      insertionCases(tree, grandparent, 1);
      grandparent = 0;
    }
    else
    {
      insertionCases(tree, rbProcess, 4);
    }

    uncle = 0;
    break;

  case 4:
    grandparent = getGrandparentProc(rbProcess);

    if (rbProcess == rbProcess->pParent->right && rbProcess->pParent == grandparent->left)
    {
      rotateLeft(tree, rbProcess->pParent);
      rbProcess = rbProcess->left;
    }
    else if (rbProcess == rbProcess->pParent->left && rbProcess->pParent == grandparent->right)
    {
      rotateRight(tree, rbProcess->pParent);
      rbProcess = rbProcess->right;
    }
    insertionCases(tree, rbProcess, 5);
    grandparent = 0;
    break;

  case 5:
    grandparent = getGrandparentProc(rbProcess);

    if (grandparent != 0)
    {
      grandparent->color = RED;
      rbProcess->pParent->color = BLACK;
      if (rbProcess == rbProcess->pParent->left && rbProcess->pParent == grandparent->left)
      {
        rotateRight(tree, grandparent);
      }
      else if (rbProcess == rbProcess->pParent->right && rbProcess->pParent == grandparent->right)
      {
        rotateLeft(tree, grandparent);
      }
    }

    grandparent = 0;
    break;

  default:
    break;
  }
  return;
}

void insertProcess(struct RBTree *tree, struct proc *p)
{
  acquire(&tree->lock);
  if (!isFull(tree))
  {
    // actually insert process into tree
    tree->root = insertProc(tree->root, p);
    if (tree->count == 0)
      tree->root->pParent = 0;
    tree->count += 1;

    // Calculate process weight
    p->weightVal = calculateWight(p->niceVal);

    // perform total weight calculation
    tree->treeWeight += p->weightVal;

    // Check for possible cases for Red Black tree property violations
    insertionCases(tree, p, 1);

    // This function call will find the process with the smallest virtualRuntime,
    // unless there was no insertion of a process that has a smaller minimum
    // virtual runtime then the process that is being pointed by minvRuntime
    if (tree->minvRuntime == 0 || tree->minvRuntime->left != 0)
      tree->minvRuntime = setMinimumVRuntimeproc(tree->root);
  }
  release(&tree->lock);
}

// This function will check for violations of the red black tree to ensure
// the trees properties are not broken when we remove the process out of the tree.
// cases:
// 1: We remove the process that needs to be retrieved and ensure that either
//    the process or the process's child is red, but not both of them.
// 2: if both the process we want to remove is black and it has child that is black,
//    then we would have to perform recoloring and rotations to ensure red black tree property is met.
void retrievingCases(struct RBTree *tree, struct proc *parentProc, struct proc *process, int cases)
{
  struct proc *parentProcess;
  struct proc *childProcess;
  struct proc *siblingProcess;

  switch (cases)
  {
  case 1:
    // Replace smallest virtual Runtime process with its right child
    parentProcess = parentProc;
    childProcess = process->right;

    // if the process being removed is on the root
    if (process == tree->root)
    {

      tree->root = childProcess;
      if (childProcess != 0)
      {
        childProcess->pParent = 0;
        childProcess->color = BLACK;
      }
    }
    else if (childProcess != 0 && !(process->color == childProcess->color))
    {
      // Replace current process by it's right child
      childProcess->pParent = parentProcess;
      parentProcess->left = childProcess;
      childProcess->color = BLACK;
    }
    else if (process->color == RED)
    {
      parentProcess->left = childProcess;
    }
    else
    {
      if (childProcess != 0)
        childProcess->pParent = parentProcess;

      parentProcess->left = childProcess;
      retrievingCases(tree, parentProcess, childProcess, 2);
    }

    process->pParent = 0;
    process->left = 0;
    process->right = 0;
    parentProcess = 0;
    childProcess = 0;
    break;

  case 2:
    // Check if process is not root,i.e parentProc != 0, and process is black
    while (process != tree->root && (process == 0 || process->color == BLACK))
    {

      ////Obtain sibling process
      if (process == parentProc->left)
      {
        siblingProcess = parentProc->right;

        if (siblingProcess != 0 && siblingProcess->color == RED)
        {
          siblingProcess->color = BLACK;
          parentProc->color = RED;
          rotateLeft(tree, parentProc);
          siblingProcess = parentProc->right;
        }
        if ((siblingProcess->left == 0 || siblingProcess->left->color == BLACK) && (siblingProcess->right == 0 || siblingProcess->right->color == BLACK))
        {
          siblingProcess->color = RED;
          // Change process pointer and parentProc pointer
          process = parentProc;
          parentProc = parentProc->pParent;
        }
        else
        {
          if (siblingProcess->right == 0 || siblingProcess->right->color == BLACK)
          {
            // Color left child
            if (siblingProcess->left != 0)
            {
              siblingProcess->left->color = BLACK;
            }
            siblingProcess->color = RED;
            rotateRight(tree, siblingProcess);
            siblingProcess = parentProc->right;
          }

          siblingProcess->color = parentProc->color;
          parentProc->color = BLACK;
          siblingProcess->right->color = BLACK;
          rotateLeft(tree, parentProc);
          process = tree->root;
        }
      }
    }
    if (process != 0)
      process->color = BLACK;

    break;

  default:
    break;
  }
  return;
}

struct proc *
retrieveProcess(struct RBTree *tree)
{
  struct proc *foundProcess; // Process pointer utilized to hold the address of the process with smallest virtualRuntime

  acquire(&tree->lock);
  if (!isEmpty(tree))
  {

    // If the number of processes are greater than the division between latency and minimum granularity
    // then recalculate the period for the processes
    // This condition is performed when the scheduler selects the next process to run
    // The formula can be found in CFS tuning article by Jacek Kobus and Refal Szklarski
    // In the CFS schduler tuning section:
    if (tree->count > (latency / min_granularity))
    {
      tree->period = tree->count * min_granularity;
    }

    // retrive the process with the smallest virtual runtime by removing it from the red black tree and returning it
    foundProcess = tree->minvRuntime;

    // Determine if the process that is being chosen is runnable at the time of the selection, if it is not, then don't return it.
    if (foundProcess->state != RUNNABLE)
    {
      release(&tree->lock);
      return 0;
    }

    retrievingCases(tree, tree->minvRuntime->pParent, tree->minvRuntime, 1);
    tree->count -= 1;

    // Determine new process with the smallest virtual runtime
    tree->minvRuntime = setMinimumVRuntimeproc(tree->root);

    // Calculate retrieved process's time slice based on formula: period*(process's weight/ red black tree weight)
    // Where period is the length of the epoch
    // The formula can be found in CFS tuning article by Jacek Kobus and Refal Szklarski
    // In the scheduling section:
    foundProcess->maxExecutionTime = (tree->period * foundProcess->weightVal / tree->treeWeight);

    // Recalculate total weight of red-black tree
    tree->treeWeight -= foundProcess->weightVal;
  }
  else
    foundProcess = 0;

  release(&tree->lock);
  return foundProcess;
}

// This function will determine if the process should be preempted.
// Preemption Cases:
// 1- if the current running process virtual runtime is greater than the smallest virtual runtime
// 2- if current running process currentRuntime has exceeded the maximum execution time
// 3- Allow the current running process to continue running until preemption should occur
int checkPreemption(struct proc *current, struct proc *minvRuntime)
{

  // Utilize integer variable to compare current runtime with the minimum granularity
  int procRuntime = current->currentRuntime;

  // Determine if the currently running process has exceed its time slice.
  if ((procRuntime >= current->maxExecutionTime) && (procRuntime >= min_granularity))
  {
    return 1;
  }

  // If the virtual runtime of the currently running process is greater than the smallest process,
  // then context switching should occur
  if (minvRuntime != 0 && minvRuntime->state == RUNNABLE && current->virtualRuntime > minvRuntime->virtualRuntime)
  {

    // Allow preemption if the process has ran for at least the min_granularity.
    // Due to the calls of checking for preemption, there needs to be made a
    // distinction between when the preemption function
    // is called after a process has just be selected by the cfs scheduler and
    // when a process has been currently running.
    if ((procRuntime != 0) && (procRuntime >= min_granularity))
    {
      return 1;
    }
    else if (procRuntime == 0)
    {
      return 1;
    }
  }

  // No preemption should occur
  return 0;
}

// Allocate a page for each process's kernel stack.
// Map it high in memory, followed by an invalid
// guard page.
void proc_mapstacks(pagetable_t kpgtbl)
{
  struct proc *p;

  for (p = proc; p < &proc[NPROC]; p++)
  {
    char *pa = kalloc();
    if (pa == 0)
      panic("kalloc");
    uint64 va = KSTACK((int)(p - proc));
    kvmmap(kpgtbl, va, (uint64)pa, PGSIZE, PTE_R | PTE_W);
  }
}

// initialize the proc table.
void procinit(void)
{
  rbinit(procTree, "tree");

  struct proc *p;

  initlock(&pid_lock, "nextpid");
  initlock(&wait_lock, "wait_lock");
  for (p = proc; p < &proc[NPROC]; p++)
  {
    initlock(&p->lock, "proc");
    p->state = UNUSED;
    p->kstack = KSTACK((int)(p - proc));
  }
}

// Must be called with interrupts disabled,
// to prevent race with process being moved
// to a different CPU.
int cpuid()
{
  int id = r_tp();
  return id;
}

// Return this CPU's cpu struct.
// Interrupts must be disabled.
struct cpu *
mycpu(void)
{
  int id = cpuid();
  struct cpu *c = &cpus[id];
  return c;
}

// Return the current struct proc *, or zero if none.
struct proc *
myproc(void)
{
  push_off();
  struct cpu *c = mycpu();
  struct proc *p = c->proc;
  pop_off();
  return p;
}

int allocpid()
{
  int pid;

  acquire(&pid_lock);
  pid = nextpid;
  nextpid = nextpid + 1;
  release(&pid_lock);

  return pid;
}

// Look in the process table for an UNUSED proc.
// If found, initialize state required to run in the kernel,
// and return with p->lock held.
// If there are no free procs, or a memory allocation fails, return 0.
static struct proc *
allocproc(void)
{
  struct proc *p;

  for (p = proc; p < &proc[NPROC]; p++)
  {
    acquire(&p->lock);
    if (p->state == UNUSED)
    {
      goto found;
    }
    else
    {
      release(&p->lock);
    }
  }
  return 0;

found:
  p->pid = allocpid();
  p->state = USED;

  // Allocate a trapframe page.
  if ((p->trapframe = (struct trapframe *)kalloc()) == 0)
  {
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // An empty user page table.
  p->pagetable = proc_pagetable(p);
  if (p->pagetable == 0)
  {
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // Set up new context to start executing at forkret,
  // which returns to user space.
  memset(&p->context, 0, sizeof(p->context));
  p->context.ra = (uint64)forkret;
  p->context.sp = p->kstack + PGSIZE;

  p->virtualRuntime = 0;
  p->currentRuntime = 0;
  p->maxExecutionTime = 0;
  p->niceVal = 0;

  p->left = 0;
  p->right = 0;
  p->pParent = 0;

  return p;
}

// free a proc structure and the data hanging from it,
// including user pages.
// p->lock must be held.
static void
freeproc(struct proc *p)
{
  if (p->trapframe)
    kfree((void *)p->trapframe);
  p->trapframe = 0;
  if (p->pagetable)
    proc_freepagetable(p->pagetable, p->sz);
  p->pagetable = 0;
  p->sz = 0;
  p->pid = 0;
  p->parent = 0;
  p->name[0] = 0;
  p->chan = 0;
  p->killed = 0;
  p->xstate = 0;
  p->state = UNUSED;
}

// Create a user page table for a given process, with no user memory,
// but with trampoline and trapframe pages.
pagetable_t
proc_pagetable(struct proc *p)
{
  pagetable_t pagetable;

  // An empty page table.
  pagetable = uvmcreate();
  if (pagetable == 0)
    return 0;

  // map the trampoline code (for system call return)
  // at the highest user virtual address.
  // only the supervisor uses it, on the way
  // to/from user space, so not PTE_U.
  if (mappages(pagetable, TRAMPOLINE, PGSIZE,
               (uint64)trampoline, PTE_R | PTE_X) < 0)
  {
    uvmfree(pagetable, 0);
    return 0;
  }

  // map the trapframe page just below the trampoline page, for
  // trampoline.S.
  if (mappages(pagetable, TRAPFRAME, PGSIZE,
               (uint64)(p->trapframe), PTE_R | PTE_W) < 0)
  {
    uvmunmap(pagetable, TRAMPOLINE, 1, 0);
    uvmfree(pagetable, 0);
    return 0;
  }

  return pagetable;
}

// Free a process's page table, and free the
// physical memory it refers to.
void proc_freepagetable(pagetable_t pagetable, uint64 sz)
{
  uvmunmap(pagetable, TRAMPOLINE, 1, 0);
  uvmunmap(pagetable, TRAPFRAME, 1, 0);
  uvmfree(pagetable, sz);
}

// a user program that calls exec("/init")
// assembled from ../user/initcode.S
// od -t xC ../user/initcode
uchar initcode[] = {
    0x17, 0x05, 0x00, 0x00, 0x13, 0x05, 0x45, 0x02,
    0x97, 0x05, 0x00, 0x00, 0x93, 0x85, 0x35, 0x02,
    0x93, 0x08, 0x70, 0x00, 0x73, 0x00, 0x00, 0x00,
    0x93, 0x08, 0x20, 0x00, 0x73, 0x00, 0x00, 0x00,
    0xef, 0xf0, 0x9f, 0xff, 0x2f, 0x69, 0x6e, 0x69,
    0x74, 0x00, 0x00, 0x24, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00};

// Set up first user process.
void userinit(void)
{
  struct proc *p;

  p = allocproc();
  initproc = p;

  // allocate one user page and copy initcode's instructions
  // and data into it.
  uvmfirst(p->pagetable, initcode, sizeof(initcode));
  p->sz = PGSIZE;

  // prepare for the very first "return" from kernel to user.
  p->trapframe->epc = 0;     // user program counter
  p->trapframe->sp = PGSIZE; // user stack pointer

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  p->state = RUNNABLE;
  insertProcess(procTree, p);

  release(&p->lock);
}

// Grow or shrink user memory by n bytes.
// Return 0 on success, -1 on failure.
int growproc(int n)
{
  uint64 sz;
  struct proc *p = myproc();

  sz = p->sz;
  if (n > 0)
  {
    if ((sz = uvmalloc(p->pagetable, sz, sz + n, PTE_W)) == 0)
    {
      return -1;
    }
  }
  else if (n < 0)
  {
    sz = uvmdealloc(p->pagetable, sz, sz + n);
  }
  p->sz = sz;
  return 0;
}

// Create a new process, copying the parent.
// Sets up child kernel stack to return as if from fork() system call.
int fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *p = myproc();

  // Allocate process.
  if ((np = allocproc()) == 0)
  {
    return -1;
  }

  // Copy user memory from parent to child.
  if (uvmcopy(p->pagetable, np->pagetable, p->sz) < 0)
  {
    freeproc(np);
    release(&np->lock);
    return -1;
  }
  np->sz = p->sz;

  // copy saved user registers.
  *(np->trapframe) = *(p->trapframe);

  // Cause fork to return 0 in the child.
  np->trapframe->a0 = 0;

  // increment reference counts on open file descriptors.
  for (i = 0; i < NOFILE; i++)
    if (p->ofile[i])
      np->ofile[i] = filedup(p->ofile[i]);
  np->cwd = idup(p->cwd);

  safestrcpy(np->name, p->name, sizeof(p->name));

  pid = np->pid;

  release(&np->lock);

  acquire(&wait_lock);
  np->parent = p;
  release(&wait_lock);

  acquire(&np->lock);
  np->state = RUNNABLE;
  release(&np->lock);

  insertProcess(procTree, np);

  return pid;
}

// Pass p's abandoned children to init.
// Caller must hold wait_lock.
void reparent(struct proc *p)
{
  struct proc *pp;

  for (pp = proc; pp < &proc[NPROC]; pp++)
  {
    if (pp->parent == p)
    {
      pp->parent = initproc;
      wakeup(initproc);
    }
  }
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait().
void exit(int status)
{
  struct proc *p = myproc();

  if (p == initproc)
    panic("init exiting");

  // Close all open files.
  for (int fd = 0; fd < NOFILE; fd++)
  {
    if (p->ofile[fd])
    {
      struct file *f = p->ofile[fd];
      fileclose(f);
      p->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(p->cwd);
  end_op();
  p->cwd = 0;

  acquire(&wait_lock);

  // Give any children to init.
  reparent(p);

  // Parent might be sleeping in wait().
  wakeup(p->parent);

  acquire(&p->lock);

  p->xstate = status;
  p->state = ZOMBIE;

  release(&wait_lock);

  // Jump into the scheduler, never to return.
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int wait(uint64 addr)
{
  struct proc *pp;
  int havekids, pid;
  struct proc *p = myproc();

  acquire(&wait_lock);

  for (;;)
  {
    // Scan through table looking for exited children.
    havekids = 0;
    for (pp = proc; pp < &proc[NPROC]; pp++)
    {
      if (pp->parent == p)
      {
        // make sure the child isn't still in exit() or swtch().
        acquire(&pp->lock);

        havekids = 1;
        if (pp->state == ZOMBIE)
        {
          // Found one.
          pid = pp->pid;
          if (addr != 0 && copyout(p->pagetable, addr, (char *)&pp->xstate,
                                   sizeof(pp->xstate)) < 0)
          {
            release(&pp->lock);
            release(&wait_lock);
            return -1;
          }
          freeproc(pp);
          release(&pp->lock);
          release(&wait_lock);
          return pid;
        }
        release(&pp->lock);
      }
    }

    // No point waiting if we don't have any children.
    if (!havekids || killed(p))
    {
      release(&wait_lock);
      return -1;
    }

    // Wait for a child to exit.
    sleep(p, &wait_lock); // DOC: wait-sleep
  }
}

// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
// - choose a process to run.
// - swtch to start running that process.
// - eventually that process transfers control
//   via swtch back to the scheduler.
void scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();

  c->proc = 0;
  for (;;)
  {
    // The most recent process to run may have had interrupts
    // turned off; enable them to avoid a deadlock if all
    // processes are waiting.
    intr_on();

    int found = 0;
    p = retrieveProcess(procTree);
    while (p != 0)
    {
      acquire(&p->lock);
      if (p->state == RUNNABLE)
      {
        uint startTick, endTick;

        acquire(&tickslock);
        startTick = ticks;
        release(&tickslock);

        p->state = RUNNING;
        c->proc = p;
        // log_cpu_state(p);
        swtch(&c->context, &p->context);

        acquire(&tickslock);
        endTick = ticks;
        release(&tickslock);

        if (strncmp(p->name, "benchmark2", 9) == 0 || strncmp(p->name, "benchmark3", 9) == 0 ||strncmp(p->name, "benchmark1", 9) == 0 )
          printf(">>{p%d,s%d,e%d}", p->pid, startTick, endTick);

        c->proc = 0;
        found = 1;
      }
      release(&p->lock);
      p = retrieveProcess(procTree);
    }
    if (found == 0)
    {
      // nothing to run; stop running on this core until an interrupt.
      intr_on();
      asm volatile("wfi");
    }
  }
}

void log_cpu_state(struct proc *p)
{
    // Get current tick
    acquire(&tickslock);
    uint64 current_tick = ticks;
    release(&tickslock);

    char buffer[NPROC + 20]; // Extra space for ">>", tick number, "{ }", and '\0'
    int index = 0;

    // Start the buffer with ">>"
    buffer[index++] = '\n';
    buffer[index++] = '>';
    buffer[index++] = '>';

    // Convert tick number to string and append it
    char tick_str[12]; // Enough for a 64-bit number
    int tick_len = 0;
    uint64 temp_tick = current_tick;

    if (temp_tick == 0)
    {
        tick_str[tick_len++] = '0';
    }
    else
    {
        while (temp_tick > 0)
        {
            tick_str[tick_len++] = (temp_tick % 10) + '0';
            temp_tick /= 10;
        }
        // Reverse the tick string
        for (int i = 0; i < tick_len / 2; i++)
        {
            char tmp = tick_str[i];
            tick_str[i] = tick_str[tick_len - i - 1];
            tick_str[tick_len - i - 1] = tmp;
        }
    }
    
    // Copy tick string to buffer
    for (int i = 0; i < tick_len; i++)
    {
        buffer[index++] = tick_str[i];
    }

    // Add '{'
    buffer[index++] = '{';

    // Append process states
    for (int i = 0; i < NPROC; i++)
    {
        char state;
        switch (proc[i].state)
        {
        case SLEEPING:
            state = 'S';
            break;
        case RUNNABLE:
            state = 'Q';
            break;
        case RUNNING:
            state = 'R';
            break;
        case ZOMBIE:
            state = 'Z';
            break;
        case UNUSED:
            state = 'X';
            break;
        case USED:
            state = 'U';
            break;
        default:
            state = '?';
        }
        buffer[index++] = state;
    }

    // Add closing '}'
    buffer[index++] = '}';
    
    // Null-terminate the buffer
    buffer[index] = '\0';

    // Print the buffer
    printf("%s", buffer);
}

// Switch to scheduler.  Must hold only p->lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->noff, but that would
// break in the few places where a lock is held but
// there's no process.
void sched(void)
{
  int intena;
  struct proc *p = myproc();

  if (!holding(&p->lock))
    panic("sched p->lock");
  if (mycpu()->noff != 1)
    panic("sched locks");
  if (p->state == RUNNING)
    panic("sched running");
  if (intr_get())
    panic("sched interruptible");

  intena = mycpu()->intena;
  swtch(&p->context, &mycpu()->context);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void yield(void)
{
  struct proc *p = myproc();
  acquire(&p->lock);
  if (checkPreemption(p, procTree->minvRuntime))
  {
    p->state = RUNNABLE;
    p->virtualRuntime = p->virtualRuntime + p->currentRuntime;
    p->currentRuntime = 0;
    insertProcess(procTree, p);
    sched();
  }
  release(&p->lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch to forkret.
void forkret(void)
{
  static int first = 1;

  // Still holding p->lock from scheduler.
  release(&myproc()->lock);

  if (first)
  {
    // File system initialization must be run in the context of a
    // regular process (e.g., because it calls sleep), and thus cannot
    // be run from main().
    fsinit(ROOTDEV);

    first = 0;
    // ensure other cores see first=0.
    __sync_synchronize();
  }

  usertrapret();
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();

  // Must acquire p->lock in order to
  // change p->state and then call sched.
  // Once we hold p->lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup locks p->lock),
  // so it's okay to release lk.

  acquire(&p->lock); // DOC: sleeplock1
  release(lk);

  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  release(&p->lock);
  acquire(lk);
}

// Wake up all processes sleeping on chan.
// Must be called without any p->lock.
void wakeup(void *chan)
{
  struct proc *p;

  for (p = proc; p < &proc[NPROC]; p++)
  {
    if (p != myproc())
    {
      acquire(&p->lock);
      if (p->state == SLEEPING && p->chan == chan)
      {
        p->state = RUNNABLE;
        p->virtualRuntime = p->virtualRuntime + p->currentRuntime;
        p->currentRuntime = 0;
        insertProcess(procTree, p);
      }
      release(&p->lock);
    }
  }
}

// Kill the process with the given pid.
// The victim won't exit until it tries to return
// to user space (see usertrap() in trap.c).
int kill(int pid)
{
  struct proc *p;

  for (p = proc; p < &proc[NPROC]; p++)
  {
    acquire(&p->lock);
    if (p->pid == pid)
    {
      p->killed = 1;
      if (p->state == SLEEPING)
      {
        // Wake process from sleep().
        p->state = RUNNABLE;

        p->virtualRuntime = p->virtualRuntime + p->currentRuntime;
        p->currentRuntime = 0;

        insertProcess(procTree, p);
      }
      release(&p->lock);
      return 0;
    }
    release(&p->lock);
  }
  return -1;
}

void setkilled(struct proc *p)
{
  acquire(&p->lock);
  p->killed = 1;
  release(&p->lock);
}

int killed(struct proc *p)
{
  int k;

  acquire(&p->lock);
  k = p->killed;
  release(&p->lock);
  return k;
}

// Copy to either a user address, or kernel address,
// depending on usr_dst.
// Returns 0 on success, -1 on error.
int either_copyout(int user_dst, uint64 dst, void *src, uint64 len)
{
  struct proc *p = myproc();
  if (user_dst)
  {
    return copyout(p->pagetable, dst, src, len);
  }
  else
  {
    memmove((char *)dst, src, len);
    return 0;
  }
}

// Copy from either a user address, or kernel address,
// depending on usr_src.
// Returns 0 on success, -1 on error.
int either_copyin(void *dst, int user_src, uint64 src, uint64 len)
{
  struct proc *p = myproc();
  if (user_src)
  {
    return copyin(p->pagetable, dst, src, len);
  }
  else
  {
    memmove(dst, (char *)src, len);
    return 0;
  }
}

// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void procdump(void)
{
  static char *states[] = {
      [UNUSED] "unused",
      [USED] "used",
      [SLEEPING] "sleep ",
      [RUNNABLE] "runble",
      [RUNNING] "run   ",
      [ZOMBIE] "zombie"};
  struct proc *p;
  char *state;

  printf("\n");
  for (p = proc; p < &proc[NPROC]; p++)
  {
    if (p->state == UNUSED)
      continue;
    if (p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    printf("%d %s %s", p->pid, state, p->name);
    printf("\n");
  }
}