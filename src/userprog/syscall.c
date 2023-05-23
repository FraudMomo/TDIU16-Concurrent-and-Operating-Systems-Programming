#include <stdio.h>
#include <syscall-nr.h>
#include "userprog/syscall.h"
#include "threads/interrupt.h"
#include "threads/thread.h"

/* header files you probably need, they are not used yet */
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "threads/vaddr.h"
#include "threads/init.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"
#include "devices/input.h"
#include "devices/timer.h"

static void syscall_handler (struct intr_frame *);

void
syscall_init (void)
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

/* This array defined the number of arguments each syscall expects.
   For example, if you want to find out the number of arguments for
   the read system call you shall write:

   int sys_read_arg_count = argc[ SYS_READ ];

   All system calls have a name such as SYS_READ defined as an enum
   type, see `lib/syscall-nr.h'. Use them instead of numbers.
 */
const int argc[] = {
  /* basic calls */
  0, 1, 1, 1, 2, 1, 1, 1, 3, 3, 2, 1, 1,
  /* not implemented */
  2, 1,    1, 1, 2, 1, 1,
  /* extended, you may need to change the order of these two (plist, sleep) */
  0, 1
};

static void
exit (int32_t* esp)
{
  int exit_status = esp[1];
  process_exit(exit_status);
  thread_exit ();
}

static void
create (struct intr_frame *f, int32_t* esp)
{
  const char* filename = (char*)esp[1];
  unsigned initial_size = esp[2];
  f->eax = filesys_create(filename, initial_size); // return success
}

static void
remove (struct intr_frame *f, int32_t* esp)
{
  const char *filename = (char*)esp[1];
  f->eax = filesys_remove(filename); // return success
}

static void
open (struct intr_frame *f, int32_t* esp)
{
  const char* filename = (char*)esp[1];
  struct thread* t = thread_current(); // Get current thread
  struct file* file = filesys_open(filename); // Struct file, inode & curr pos

  // Insert into map and return file descriptor if file exists, else return error
  f->eax = (file != NULL) ? flist_insert(&(t->file_table), file) : -1;
}

static void
close (int32_t* esp)
{
  struct thread* t = thread_current();
  const int fd = esp[1];
  struct file* file = flist_find(&t->file_table, fd);

  if(file) {
    file_close(file); // Close file
    flist_remove(&t->file_table, fd); // Remove from map
  }
}

static void
read (struct intr_frame *f, int32_t* esp)
{
  const int fd = esp[1];
  char* buffer = (char*)esp[2];
  unsigned length = esp[3];

  // Read from stdin
  if (fd == STDIN_FILENO) {
    for(unsigned i = 0; i < length; i++) {

      // Get input
      buffer[i] = input_getc();

      // Replace \r with \n
      if (buffer[i] == '\r') buffer[i] = '\n';

      // Display input (single character)
      putbuf(&buffer[i], 1);
    }

    f->eax = length; // return length

  } else if(fd >= 2 && fd <= 32) {
    struct thread* t = thread_current();
    struct file* file = flist_find(&(t->file_table), fd);

    // Read and return bytes read if file exists, else return error
    f->eax = (file != NULL) ? file_read(file, buffer, length) : -1;
    
  // Error
  } else {
    f->eax = -1; // return -1
  }
}

static void
write (struct intr_frame *f, int32_t* esp)
{
  const int fd = esp[1];
  char* buffer = (char*)esp[2];
  unsigned length = esp[3];

  // Write to stdout
  if (fd == STDOUT_FILENO) {
    
    // Display buffer
    putbuf(buffer, length);

    f->eax = length; // return length

  // Write to file
  } else if(fd >= 2 && fd <= 32) {
    struct thread* t = thread_current();
    struct file* file = flist_find(&(t->file_table), fd);

    // Write and return bytes written if file exists, else return error
    f->eax = (file != NULL) ? file_write(file, buffer, length) : -1;

  // Error
  } else {
    f->eax = -1; // return -1
  }
}

static void
filesize (struct intr_frame *f, int32_t* esp)
{
  const int fd = esp[1];
  struct thread* t = thread_current();
  struct file* file = flist_find(&t->file_table, fd);

  // Filesize
  f->eax = (file) ? file_length(file) : -1;
}

static void
seek (int32_t* esp)
{
  const int fd = esp[1];
  const unsigned newPosition = esp[2];
  struct thread* t = thread_current();
  struct file* file = flist_find(&t->file_table, fd);
  const unsigned filesize = file_length(file);
  
  // Check if newPosition is within file size and file exists, then seek
  if (newPosition <= filesize && file) file_seek(file, newPosition);
}

static void
tell (struct intr_frame *f, int32_t* esp)
{
  const int fd = esp[1];
  struct thread* t = thread_current();
  struct file* file = flist_find(&t->file_table, fd);

  // Tell
  f->eax = (file) ? file_tell(file) : -1;
}

static void
sleep (int32_t* esp)
{
  int64_t ms = (int64_t) esp[1];
  timer_msleep(ms);
}

static void
plist (void)
{
  process_print_list();
}

static void
exec (struct intr_frame *f, int32_t* esp)
{
  char* file_name = (char*) esp[1];

  int process_id = process_execute(file_name);

  f->eax = (uint32_t) process_id;
}

static void
wait (struct intr_frame *f, int32_t* esp)
{
  // Child process id
  int process_id = (int) esp[1];

  // Wait for child process to finish
  f->eax = (uint32_t) process_wait(process_id);
}

static bool verify_fix_length(void* start, unsigned length)
{
  // Null pointer
  if(start == NULL) return false;

  struct thread* t = thread_current();
  char* start_addr = (char*)start;
  char* end_addr = start_addr + length;

  // Address in kernel space
  if (is_kernel_vaddr(start_addr) || is_kernel_vaddr(end_addr)) return false;

  for (char* addr = pg_round_down(start_addr); addr < end_addr; addr += PGSIZE) {
    if (pagedir_get_page(t->pagedir, addr) == NULL) return false;
  }
  return true;
}

static bool verify_variable_length(char* start)
{
  // Null pointer or address in kernel space
  if(start == NULL || is_kernel_vaddr(start)) return false;

  struct thread* t = thread_current();

  /* To detect page changes */
  unsigned current_page = pg_no(start);

  /* Check that the start address is valid */
  if (pagedir_get_page(t->pagedir, start) == NULL) return false;

  for (char* addr = start; ; addr++)
  {
    /* Check if we have changed page */
    if (current_page != pg_no(addr)) {
      current_page = pg_no(addr);
      
      /* Check that the addr in new page is valid */
      if (pagedir_get_page(t->pagedir, addr) == NULL) return false;
    }

    /* Check if we have reached the end of the string */
    if (*addr == '\0') return true;
  }
}

static void
syscall_handler (struct intr_frame *f)
{
  int32_t* esp = (int32_t*)f->esp;

  // Verify syscall number
  if (!verify_fix_length((void*)esp, 4)) thread_exit();

  if (esp[0] > SYS_NUMBER_OF_CALLS) thread_exit();

  // Verify arguments
  if (!verify_fix_length((void*)esp + 4, argc[esp[0]] * 4)) thread_exit();

  // Verify variable length pointer
  if ((esp[0] == SYS_EXEC || esp[0] == SYS_CREATE || esp[0] == SYS_REMOVE || esp[0] == SYS_OPEN) && !verify_variable_length(esp[1]))
    thread_exit();

  // Verify fixed length pointer
  if ((esp[0] == SYS_READ || esp[0] == SYS_WRITE) && !verify_fix_length(esp[2], esp[3]))
    thread_exit();

  switch ( esp[0] )
  {
    case SYS_HALT: power_off (); break;
    case SYS_EXIT: exit (esp); break;
    case SYS_CREATE: create (f, esp); break;
    case SYS_REMOVE: remove (f, esp); break;
    case SYS_OPEN: open (f, esp); break;
    case SYS_CLOSE: close (esp); break;
    case SYS_READ: read (f, esp); break;
    case SYS_WRITE: write (f, esp); break;
    case SYS_FILESIZE: filesize (f, esp); break;
    case SYS_SEEK: seek (esp); break;
    case SYS_TELL: tell (f, esp); break;
    case SYS_SLEEP: sleep (esp); break;
    case SYS_PLIST: plist (); break;
    case SYS_EXEC: exec (f, esp); break;
    case SYS_WAIT: wait (f, esp); break;
    default:
    {
      printf ("Executed an unknown system call!\n");

      printf ("Stack top + 0: %d\n", esp[0]);
      printf ("Stack top + 1: %d\n", esp[1]);

      thread_exit ();
    }
  }
}
