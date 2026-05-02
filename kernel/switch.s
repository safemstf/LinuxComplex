/* kernel/switch.s - Context Switching Assembly
 * 
 * CRITICAL: Offsets must match cpu_context_t and task_t in task.h!
 */

.section .text
.global task_switch_asm

.set CONTEXT_OFFSET, 44

/* void task_switch_asm(task_t *old_task, task_t *new_task) */
task_switch_asm:
    cli                         /* Disable interrupts during switch */
    
    /* Get parameters into registers we won't overwrite */
    mov 4(%esp), %eax          /* eax = old_task */
    mov 8(%esp), %edx          /* edx = new_task */
    
    /* Skip saving if old_task is NULL */
    test %eax, %eax
    jz .load_new
    
    /* ====== SAVE OLD TASK ====== */
    /* Save general purpose registers in order */
    mov %edi, CONTEXT_OFFSET+0(%eax)   /* edi */
    mov %esi, CONTEXT_OFFSET+4(%eax)   /* esi */
    mov %ebp, CONTEXT_OFFSET+8(%eax)   /* ebp */
    
    /* Save ESP: current stack pointer + 8 (skip return address and old param) */
    lea 8(%esp), %ecx
    mov %ecx, CONTEXT_OFFSET+12(%eax)  /* esp */
    
    mov %ebx, CONTEXT_OFFSET+16(%eax)  /* ebx */
    
    /* Save actual EDX value (not the parameter!) */
    push %edx                          /* Temporarily save new_task */
    mov 12(%esp), %edx                 /* Get original EDX from stack (before call) */
    mov %edx, CONTEXT_OFFSET+20(%eax)  /* Save it */
    pop %edx                           /* Restore new_task */
    
    mov %ecx, CONTEXT_OFFSET+24(%eax)  /* ecx */
    
    /* Save eax */
    push %eax                          /* Save old_task pointer */
    mov 8(%esp), %ecx                  /* Get original EAX from stack */
    pop %eax                           /* Restore old_task pointer */
    mov %ecx, CONTEXT_OFFSET+28(%eax)  /* Save original EAX */
    
    /* Save segment registers */
    mov %ds, %cx
    mov %cx, CONTEXT_OFFSET+32(%eax)   /* ds */
    mov %es, %cx
    mov %cx, CONTEXT_OFFSET+36(%eax)   /* es */
    mov %fs, %cx
    mov %cx, CONTEXT_OFFSET+40(%eax)   /* fs */
    mov %gs, %cx
    mov %cx, CONTEXT_OFFSET+44(%eax)   /* gs */
    
    /* Save return address as EIP */
    mov (%esp), %ecx
    mov %ecx, CONTEXT_OFFSET+48(%eax)  /* eip */
    
    /* Save CS and EFLAGS */
    mov %cs, %cx
    mov %cx, CONTEXT_OFFSET+52(%eax)   /* cs */
    pushf
    pop %ecx
    mov %ecx, CONTEXT_OFFSET+56(%eax)  /* eflags */
    
.load_new:
    /* ====== LOAD NEW TASK ====== */
    /* edx contains new_task */
    
    /* Load segment registers FIRST */
    mov CONTEXT_OFFSET+32(%edx), %bx
    mov %bx, %ds
    mov CONTEXT_OFFSET+36(%edx), %bx
    mov %bx, %es
    mov CONTEXT_OFFSET+40(%edx), %bx
    mov %bx, %fs
    mov CONTEXT_OFFSET+44(%edx), %bx
    mov %bx, %gs
    
    /* Load stack pointer */
    mov CONTEXT_OFFSET+12(%edx), %esp
    
    /* Load general purpose registers (except eax, edx) */
    mov CONTEXT_OFFSET+0(%edx), %edi
    mov CONTEXT_OFFSET+4(%edx), %esi
    mov CONTEXT_OFFSET+8(%edx), %ebp
    mov CONTEXT_OFFSET+16(%edx), %ebx
    mov CONTEXT_OFFSET+24(%edx), %ecx
    
    /* Push return address (EIP) */
    push CONTEXT_OFFSET+48(%edx)
    
    /* Load EFLAGS */
    push CONTEXT_OFFSET+56(%edx)
    popf
    
    /* Load EAX first, then EDX last (destroys pointer) */
    mov CONTEXT_OFFSET+28(%edx), %eax
    mov CONTEXT_OFFSET+20(%edx), %edx
    
    sti                         /* Re-enable interrupts */
    ret                         /* Jump to saved EIP */

/* ================================================================
 * task_switch_to_user_asm - Save kernel context, IRET to user mode
 *
 * This saves the old kernel task's context, then switches to the
 * new user task's stack and does IRET to enter Ring 3.
 * When the user task exits and we switch back, we return here.
 * ================================================================ */
.global task_switch_to_user_asm
task_switch_to_user_asm:
    cli

    /* Get parameters */
    mov 4(%esp), %eax          /* eax = old_task */
    mov 8(%esp), %edx          /* edx = new_task */

    /* Skip saving if old_task is NULL */
    test %eax, %eax
    jz .user_load

    /* ====== SAVE OLD KERNEL TASK ====== */
    mov %edi, CONTEXT_OFFSET+0(%eax)
    mov %esi, CONTEXT_OFFSET+4(%eax)
    mov %ebp, CONTEXT_OFFSET+8(%eax)

    /* Save ESP (skip return address and params) */
    lea 8(%esp), %ecx
    mov %ecx, CONTEXT_OFFSET+12(%eax)

    mov %ebx, CONTEXT_OFFSET+16(%eax)

    /* Save EDX (not the parameter) */
    push %edx
    mov 12(%esp), %edx
    mov %edx, CONTEXT_OFFSET+20(%eax)
    pop %edx

    mov %ecx, CONTEXT_OFFSET+24(%eax)

    /* Save EAX (not the pointer) */
    push %eax
    mov 8(%esp), %ecx
    pop %eax
    mov %ecx, CONTEXT_OFFSET+28(%eax)

    /* Save segment registers */
    mov %ds, %cx
    mov %cx, CONTEXT_OFFSET+32(%eax)
    mov %es, %cx
    mov %cx, CONTEXT_OFFSET+36(%eax)
    mov %fs, %cx
    mov %cx, CONTEXT_OFFSET+40(%eax)
    mov %gs, %cx
    mov %cx, CONTEXT_OFFSET+44(%eax)

    /* Save return address as EIP - THIS IS KEY! */
    mov (%esp), %ecx
    mov %ecx, CONTEXT_OFFSET+48(%eax)

    /* Save CS and EFLAGS */
    mov %cs, %cx
    mov %cx, CONTEXT_OFFSET+52(%eax)
    pushf
    pop %ecx
    mov %ecx, CONTEXT_OFFSET+56(%eax)

.user_load:
    /* ====== IRET TO USER MODE ====== */
    /* edx = new_task, context.esp points to IRET frame */

    /* Switch to user task's kernel stack (which has IRET frame) */
    mov CONTEXT_OFFSET+12(%edx), %esp

    /* Zero GPRs to avoid information leakage */
    xorl %eax, %eax
    xorl %ebx, %ebx
    xorl %ecx, %ecx
    xorl %edx, %edx
    xorl %esi, %esi
    xorl %edi, %edi
    xorl %ebp, %ebp

    /* IRET will pop: EIP, CS, EFLAGS, ESP, SS */
    iret

/* ================================================================
 * task_switch_and_die - Switch to new task, NEVER return
 *
 * Called when a task exits. Does NOT save the dying task's context
 * (it's dead anyway). Simply loads the target task's context and
 * jumps there. The dying task's stack is abandoned.
 *
 * void task_switch_and_die(task_t *new_task);
 * ================================================================ */
.global task_switch_and_die
task_switch_and_die:
    cli

    /* Get new_task pointer - it's passed as first argument */
    mov 4(%esp), %edx

    /* Load kernel data segments */
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs

    /* Load the new task's saved stack pointer */
    mov CONTEXT_OFFSET+12(%edx), %esp

    /* Get the saved EIP into a register first */
    mov CONTEXT_OFFSET+48(%edx), %eax

    /* DEBUG: Write 'A' to VGA to show we got here */
    movw $0x0F41, 0xB8000    /* White 'A' at top-left */

    /* Push EIP for ret */
    push %eax

    /* DEBUG: Write 'B' to show push worked */
    movw $0x0F42, 0xB8002    /* White 'B' */

    /* Load EFLAGS */
    mov CONTEXT_OFFSET+56(%edx), %eax
    push %eax
    popf

    /* DEBUG: Write 'C' */
    movw $0x0F43, 0xB8004    /* White 'C' */

    /* Load general purpose registers */
    mov CONTEXT_OFFSET+0(%edx), %edi
    mov CONTEXT_OFFSET+4(%edx), %esi
    mov CONTEXT_OFFSET+8(%edx), %ebp
    mov CONTEXT_OFFSET+16(%edx), %ebx
    mov CONTEXT_OFFSET+24(%edx), %ecx
    mov CONTEXT_OFFSET+28(%edx), %eax
    mov CONTEXT_OFFSET+20(%edx), %edx

    /* DEBUG: Write 'D' - we're about to ret */
    movw $0x0F44, 0xB8006    /* White 'D' */

    sti
    ret                         /* Pop EIP and jump */

.section .note.GNU-stack,"",@progbits