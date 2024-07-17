// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem_cpu[NCPU];

void
kinit()
{
  for (int i = 0; i < NCPU; i++) {
    initlock(&kmem_cpu[i].lock, "kmem");
  }
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}

// Free the page of physical memory pointed at by pa,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run *)pa;
  push_off();
  int cid = cpuid();
  acquire(&kmem_cpu[cid].lock);
  // acquire(&kmem.lock);
  r->next = kmem_cpu[cid].freelist;
  kmem_cpu[cid].freelist = r;
  release(&kmem_cpu[cid].lock);
  pop_off();

  // release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *kalloc(void) {
  struct run *r = 0;
  push_off();
  int cid = cpuid();
  acquire(&kmem_cpu[cid].lock);
  if (kmem_cpu[cid].freelist == 0) {
    release(&kmem_cpu[cid].lock);
    for (int i = 0; i < NCPU; i++) {
      if (i == cid) {
        continue;
      }
      acquire(&kmem_cpu[i].lock);
      if (kmem_cpu[i].freelist) {
        r = kmem_cpu[i].freelist;
        if (r) kmem_cpu[i].freelist = r->next;
        release(&kmem_cpu[i].lock);

        break;
      }
      release(&kmem_cpu[i].lock);
    }
  } else {
    r = kmem_cpu[cid].freelist;
    if (r) kmem_cpu[cid].freelist = r->next;
    release(&kmem_cpu[cid].lock);
  }
  pop_off();
  if (r) memset((char *)r, 5, PGSIZE);  // fill with junk
  return (void *)r;
}
