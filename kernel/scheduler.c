/* kernel/scheduler.c - Round-Robin Task Scheduler
 *
 * Manages task execution order using round-robin algorithm.
 * Each task gets an equal time slice (10ms by default).
 */

#include "scheduler.h"
#include "kernel.h"
#include "task.h"

/* ================================================================
 * GLOBAL STATE
 * ================================================================ */

static scheduler_stats_t stats = {0};
static bool scheduler_running = false;

/* Ready queue - circular linked list of tasks (NOT including kernel_task) */
static task_t *ready_queue = NULL;
static task_t *ready_queue_tail = NULL;

/* External references */
extern task_t *current_task;
extern task_t *kernel_task;

/* Forward declarations */
static void update_statistics(void);
static task_t *get_next_ready_task(void);

/* ================================================================
 * INITIALIZATION
 * ================================================================ */

void scheduler_init(void)
{
    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    terminal_writestring("[SCHEDULER] Initializing scheduler...\n");

    memset(&stats, 0, sizeof(stats));

    ready_queue = NULL;
    ready_queue_tail = NULL;
    scheduler_running = true;

    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
    terminal_writestring("[SCHEDULER] Scheduler ready\n");
    terminal_setcolor(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK));
}

/* ================================================================
 * TASK QUEUE MANAGEMENT
 * ================================================================ */

void scheduler_add_task(task_t *task)
{
    if (!task)
        return;

    /* Never add kernel_task to ready_queue */
    if (task == kernel_task)
        return;

    /* Don't add if already in queue */
    if (ready_queue)
    {
        task_t *curr = ready_queue;
        do
        {
            if (curr == task)
                return;
            curr = curr->sched_next;
        } while (curr && curr != ready_queue);
    }

    /* Add to ready queue (circular linked list) */
    if (!ready_queue)
    {
        ready_queue = task;
        ready_queue_tail = task;
        task->sched_next = task;
    }
    else
    {
        ready_queue_tail->sched_next = task;
        ready_queue_tail = task;
        task->sched_next = ready_queue;
    }

    if (task->state == TASK_ZOMBIE)
        return;
        
    task->state = TASK_READY;
    stats.total_tasks++;
}

void scheduler_remove_task(task_t *task)
{
    if (!task || !ready_queue)
        return;

    /* Find and remove from queue */
    if (ready_queue == task)
    {
        /* Removing head */
        if (ready_queue == ready_queue_tail)
        {
            /* Only task in queue */
            ready_queue = NULL;
            ready_queue_tail = NULL;
        }
        else
        {
            ready_queue_tail->sched_next = task->sched_next;
            ready_queue = task->sched_next;
        }
    }
    else
    {
        /* Find previous task */
        task_t *prev = ready_queue;
        while (prev->sched_next != task && prev->sched_next != ready_queue)
        {
            prev = prev->sched_next;
        }

        if (prev->sched_next == task)
        {
            prev->sched_next = task->sched_next;
            if (task == ready_queue_tail)
            {
                ready_queue_tail = prev;
            }
        }
    }

    task->sched_next = NULL;
    if (stats.total_tasks > 0)
        stats.total_tasks--;
}

/* ================================================================
 * TASK SELECTION
 * ================================================================ */

static task_t *get_next_ready_task(void)
{
    if (!ready_queue)
    {
        return kernel_task;
    }

    task_t *task = ready_queue;
    task_t *first = ready_queue;

    do
    {
        if (task->state == TASK_READY)
        {
            return task;
        }
        task = task->sched_next;
    } while (task && task != first);

    /* No READY tasks → run kernel task */
    return kernel_task;
}

task_t *scheduler_pick_next(void)
{
    update_statistics();

    /* Get next ready task */
    task_t *next = get_next_ready_task();

    /* Reset time slice */
    next->time_slice = SCHEDULER_TIME_SLICE_MS;

    return next;
}

/* ================================================================
 * SCHEDULING
 * ================================================================ */

void scheduler_tick(void)
{
    if (!scheduler_running)
        return;

    stats.total_ticks++;

    /* Update sleeping tasks */
    if (ready_queue)
    {
        task_t *task = ready_queue;
        task_t *first = ready_queue;
        do
        {
            if (task->state == TASK_SLEEPING)
            {
                if (task->wake_time > 0 && stats.total_ticks >= task->wake_time)
                {
                    task->state = TASK_READY;
                    task->wake_time = 0;
                }
            }
            task = task->sched_next;
        } while (task && task != first);
    }

    /* Decrement current task's time slice */
    if (current_task && current_task->state == TASK_RUNNING)
    {
        if (current_task->time_slice > 0)
        {
            current_task->time_slice--;
            current_task->total_time++;
        }

        /* Time slice expired? */
        if (current_task->time_slice == 0)
        {
            scheduler_schedule();
        }
    }
}

void scheduler_schedule(void)
{
    if (!scheduler_running)
        return;

    /* If current task is ZOMBIE, do NOT touch it */
    if (current_task && current_task->state == TASK_ZOMBIE)
    {
        /* Just switch away - task_switch will handle it */
    }
    else if (current_task && current_task->state == TASK_RUNNING)
    {
        current_task->state = TASK_READY;
    }

    task_t *next = scheduler_pick_next();

    /* Must not fall back into a ZOMBIE */
    if (!next)
    {
        next = kernel_task;
    }

    /* Don't run ZOMBIE tasks */
    if (next->state == TASK_ZOMBIE) {
        next = kernel_task;
    }

    if (!next || next == current_task)
    {
        if (current_task && current_task->state != TASK_ZOMBIE)
        {
            current_task->state = TASK_RUNNING;
            current_task->time_slice = SCHEDULER_TIME_SLICE_MS;
        }
        return;
    }

    stats.context_switches++;
    task_switch(next);
}

/* ================================================================
 * STATISTICS
 * ================================================================ */

static void update_statistics(void)
{
    stats.ready_tasks = 0;
    stats.blocked_tasks = 0;

    if (!ready_queue)
        return;

    task_t *task = ready_queue;
    task_t *first = ready_queue;
    do
    {
        switch (task->state)
        {
        case TASK_READY:
            stats.ready_tasks++;
            break;
        case TASK_BLOCKED:
        case TASK_SLEEPING:
            stats.blocked_tasks++;
            break;
        default:
            break;
        }
        task = task->sched_next;
    } while (task && task != first);
}

scheduler_stats_t scheduler_get_stats(void)
{
    update_statistics();
    return stats;
}
