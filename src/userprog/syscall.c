#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "userprog/process.h"
#include "threads/vaddr.h"
#include "filesys/filesys.h"
#include "threads/synch.h"
#include "devices/input.h"
#include "devices/shutdown.h"
struct lock filesys_lock;

static void syscall_handler(struct intr_frame*);

void syscall_init(void) {
  lock_init(&filesys_lock);
  intr_register_int(0x30, 3, INTR_ON, syscall_handler, "syscall"); 
}


static void do_exit(int status) {
  printf("%s: exit(%d)\n", thread_current()->pcb->process_name, status);
  process_exit();
}

/* Reads a byte at user virtual address UADDR.
   UADDR must be below PHYS_BASE.
   Returns the byte value if successful,
   -1 if a segfault occurred. */
static int get_user_one_byte (const uint8_t *uaddr) {
    int result;
    asm ("movl $1f, %0; movzbl %1, %0; 1:"
    : "=&a" (result) : "m" (*uaddr));
    return result;
}

static int get_user_one_word (const uint32_t *uaddr) {
    int result;
    asm ("movl $1f, %0; movl %1, %0; 1:"
    : "=&a" (result) : "m" (*uaddr));
    if (result == -1) {
      do_exit(-1);
    }
    return result;
}

static void check_args_below_PHYS_BASE(void* args, int offset) {
  if (!((char *)args <= ((char *)PHYS_BASE - offset))) {
    do_exit(-1);
  }
}

static bool copy_from_user(char *to, const char *from, unsigned long n) {
  // check_args_below_PHYS_BASE(from, n);
  for (unsigned i = 0; i < n; i++) {
    int c;
    if ((c = get_user_one_byte(from + i)) == -1 ) {
      do_exit(-1);
    }
    to[i] = c;
  }
  return true;
}

static bool copy_file_name(char *to, const char *from, unsigned long n) {
  for (unsigned i = 0; i < n; i++) {
    check_args_below_PHYS_BASE(from + i, 1);
    int c;
    if ((c = get_user_one_byte(from + i)) == -1 ) {
      do_exit(-1);
    }
    to[i] = c;
    if (c == '\0') {
      return true;
    }
  }
  to[n] = '\0';
  return true;
}

/* Writes BYTE to user address UDST.
   UDST must be below PHYS_BASE.
   Returns true if successful,
   false if a segfault occurred. */
static bool put_user_one_byte (uint8_t *udst, uint8_t byte) {
    int error_code;
    asm ("movl $1f, %0; movb %b2, %1; 1:"
    : "=&a" (error_code), "=m" (*udst) : "q" (byte));
    return error_code != -1;
}

static bool copy_from_kernel(char *to, const char *from, unsigned long n) {
  // check_args_below_PHYS_BASE(to, n);
  for (unsigned i = 0; i < n; i++) {
    if (!put_user_one_byte(to, from[i])) {
      do_exit(-1);
    }
  }
  return true;
}


static void syscall_handler(struct intr_frame* f UNUSED) {
  uint32_t* args = ((uint32_t*)f->esp);
  check_args_below_PHYS_BASE(args, 4);
  int syscall_num = get_user_one_word(args);
  struct process* pcb = thread_current()->pcb;

  /*
   * The following print statement, if uncommented, will print out the syscall
   * number whenever a process enters a system call. You might find it useful
   * when debugging. It will cause tests to fail, however, so you should not
   * include it in your final submission.
   */

  /* printf("System call number: %d\n", args[0]); */

  // exit handler
  if (syscall_num == SYS_EXIT) {
    check_args_below_PHYS_BASE(args, 2*4);
    int status = get_user_one_word(args + 1);
    f->eax = status;
    do_exit(status);
  }

  // write handler
  else if (syscall_num == SYS_WRITE) {
    check_args_below_PHYS_BASE(args, 4*4);
    int fd = get_user_one_word(args + 1);
    struct file* file = get_file(&pcb->open_files, fd);
    if (fd != STDOUT_FILENO && !file) {
      f->eax = -1;
      return;
    }
    const char *user_buf = (const char *)get_user_one_word(args + 2);
    unsigned size = (unsigned)get_user_one_word(args + 3);
    check_args_below_PHYS_BASE(user_buf, size);
    char ker_buf[1024];
    unsigned cnt = 0;
    while (cnt < size) {
      unsigned len = (size-cnt) < 1024 ? (size-cnt) : 1024;
      copy_from_user(ker_buf, user_buf+cnt, len);
      if (fd == STDOUT_FILENO) {
        putbuf(ker_buf, len);
      } else {
        lock_acquire(&filesys_lock);
        len = file_write(file, ker_buf, len);
        lock_release(&filesys_lock);
        if (len == 0) {
          break;
        }
      }
      cnt += len;
    }
    f->eax = cnt;
    return;
  }

  // read handler
  else if (syscall_num == SYS_READ) {
    check_args_below_PHYS_BASE(args, 4*4);
    int fd = get_user_one_word(args + 1);
    struct file* file = get_file(&pcb->open_files, fd);
    if (fd != STDIN_FILENO && !file) {
      f->eax = -1;
      return;
    }
    char *user_buf = (char *)get_user_one_word(args + 2);
    unsigned size = (unsigned)get_user_one_word(args + 3);
    check_args_below_PHYS_BASE(user_buf, size);
    char ker_buf[1024];
    unsigned cnt = 0;
    while (cnt < size) {
      unsigned len = (size-cnt) < 1024 ? (size-cnt) : 1024;
      if (fd == STDIN_FILENO) {
        for (unsigned i = 0; i < len; i++) {
          ker_buf[i] = input_getc();
        }
      } else {
        lock_acquire(&filesys_lock);
        len = file_read(file, ker_buf, len);
        lock_release(&filesys_lock);
        if (len == 0) {
          break;
        }
      }
      copy_from_kernel(user_buf+cnt, ker_buf, len);
      cnt += len;
    }
    f->eax = cnt;
    return;
  }  

  // practice handler
  else if (syscall_num == SYS_PRACTICE) {
    check_args_below_PHYS_BASE(args, 2*4);
    f->eax = get_user_one_word(args + 1) + 1;
    return;
  } 

  // create handler
  else if (syscall_num == SYS_CREATE) {
    check_args_below_PHYS_BASE(args, 3*4);
    const char *file_name = (const char*)get_user_one_word(args + 1);
    unsigned init_size = (unsigned)get_user_one_word(args + 2);
    char kernel_buf[15];
    copy_file_name(kernel_buf, file_name, 14);
    lock_acquire(&filesys_lock);
    f->eax = filesys_create(kernel_buf, init_size);
    lock_release(&filesys_lock);
    return;
  }

  // remove handler
  else if (syscall_num == SYS_REMOVE) {
    check_args_below_PHYS_BASE(args, 2*4);
    const char *file_name = (const char*)get_user_one_word(args + 1);
    char kernel_buf[15];
    copy_file_name(kernel_buf, file_name, 14);
    lock_acquire(&filesys_lock);
    f->eax = filesys_remove(kernel_buf);
    lock_release(&filesys_lock);
    return;
  }

  // open handler
  else if (syscall_num == SYS_OPEN) {
    check_args_below_PHYS_BASE(args, 2*4);
    const char *file_name = (const char*)get_user_one_word(args + 1);
    char kernel_buf[15];
    copy_file_name(kernel_buf, file_name, 14);
    lock_acquire(&filesys_lock);
    struct file* file = filesys_open(kernel_buf);
    lock_release(&filesys_lock);
    f->eax = add_a_file(&pcb->open_files, file);
    return;
  } 

  //file size handler 
  else if (syscall_num == SYS_FILESIZE) {
    check_args_below_PHYS_BASE(args, 2*4);
    int fd = get_user_one_word(args + 1);
    struct file* file = get_file(&pcb->open_files, fd);
    if (!file) {
      f->eax = -1;
    } else {
      lock_acquire(&filesys_lock);
      f->eax = file_length(file);
      lock_release(&filesys_lock);
    }
    return;
  }

  // seek handler
  else if(syscall_num == SYS_SEEK) {
    check_args_below_PHYS_BASE(args, 3*4);
    int fd = get_user_one_word(args + 1);
    struct file* file = get_file(&pcb->open_files, fd);
    if (!file) {
      return;
    }
    unsigned pos = get_user_one_word(args + 2);
    lock_acquire(&filesys_lock);
    file_seek(file, pos);
    lock_release(&filesys_lock);
  }

  // tell handler 
  else if (syscall_num == SYS_TELL) {
    check_args_below_PHYS_BASE(args, 2*4);
    int fd = get_user_one_word(args + 1);
    struct file* file = get_file(&pcb->open_files, fd);
    if (!file) {
      f->eax = -1;
    } else {
      lock_acquire(&filesys_lock);
      f->eax = file_tell(file);
      lock_release(&filesys_lock);
    }
    return;
  }

  // close handler 
  else if (syscall_num == SYS_CLOSE) {
    check_args_below_PHYS_BASE(args, 2*4);
    int fd = get_user_one_word(args + 1);
    struct file* file = get_file(&pcb->open_files, fd);
    if (!file) {
      do_exit(-1);
    } else {
      lock_acquire(&filesys_lock);
      file_close(file);
      lock_release(&filesys_lock);
      remove_a_file(&pcb->open_files, fd);
    }
    return;
  }

  // halt handler 
  else if (syscall_num == SYS_HALT) {
    shutdown_power_off();
    return;
  } 

  // unknown syscall num 
  else {
    do_exit(-1);
  }
}
