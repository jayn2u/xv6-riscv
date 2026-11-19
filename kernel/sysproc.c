#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#include "vm.h"
#include "mman.h"

uint64
sys_exit(void)
{
  int n;
  argint(0, &n);
  kexit(n);
  return 0;  // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return kfork();
}

uint64
sys_wait(void)
{
  uint64 p;
  argaddr(0, &p);
  return kwait(p);
}

uint64
sys_sbrk(void)
{
  uint64 addr;
  int t;
  int n;

  argint(0, &n);
  argint(1, &t);
  addr = myproc()->sz;

  if(t == SBRK_EAGER || n < 0) {
    if(growproc(n) < 0) {
      return -1;
    }
  } else {
    // Lazily allocate memory for this process: increase its memory
    // size but don't allocate memory. If the processes uses the
    // memory, vmfault() will allocate it.
    if(addr + n < addr)
      return -1;
    if(addr + n > TRAPFRAME)
      return -1;
    myproc()->sz += n;
  }
  return addr;
}

uint64
sys_pause(void)
{
  int n;
  uint ticks0;

  argint(0, &n);
  if(n < 0)
    n = 0;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(killed(myproc())){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

uint64
sys_kill(void)
{
  int pid;

  argint(0, &pid);
  return kkill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

uint64
sys_mmap(void)
{
  uint64 addr;
  int prot;

  argaddr(0, &addr);
  argint(1, &prot);
  
  if (addr % PGSIZE != 0) return -1;

  struct proc *p = myproc();

  if (addr >= MAXVA) return -1;

  int perm = PTE_U;

  if (prot & PROT_READ)
    perm = perm | PTE_R;
  
  if (prot & PROT_WRITE)
    perm = perm | PTE_W;

  if (prot & PROT_EXEC)
    perm = perm | PTE_X;

  pte_t* pte = walk(p->pagetable, addr, 0);

  if (pte != 0 && (*pte & PTE_V))
    return -1;

  int i;
  for (i = 0; i < MAX_MMAP; i++) {
    if (p->mmap_used[i] == 0)
      break;
  }
  
  if (i == MAX_MMAP) {
    return -1;
  }
  
  char* mem;

  mem = kalloc();
  if (mem == 0)
    return -1;
  memset(mem, 0, PGSIZE);
  if(mappages(p->pagetable, addr, PGSIZE, (uint64)mem, perm) != 0) {
    kfree(mem);
    return -1;
  }

  p->mmapped[i] = addr;
  p->mmap_used[i] = 1;
  
  return 0;
}

uint64
sys_munmap(void)
{
  uint64 addr;
  struct proc *p = myproc();

  argaddr(0, &addr);

  for(int i = 0; i < MAX_MMAP; i++) {
    if (p->mmap_used[i] != 0 && p->mmapped[i] == addr) {
      uvmunmap(p->pagetable, addr, 1, 1);
      p->mmap_used[i] = 0;
      return 0;
    }
  }
  return -1;
}

uint64
sys_printpagetbl(void)
{
  return 0;
}