#include <debug.h>
#include <stddef.h>
#include "plist.h"
#include <stdio.h>
#include <string.h>

// Pintos global, so we store it here
struct process_element plist[PLIST_SIZE];

struct lock plist_lock;

void plist_init()
{
    for (int i = 0; i < PLIST_SIZE; i++) plist[i].used = false;
    lock_init(&plist_lock);
}

int plist_insert(int process_id, char process_name[], int parent_id)
{
    lock_acquire(&plist_lock);
    for (int i = 0; i < PLIST_SIZE; i++)
    {
        // Find free element
        if (!plist[i].used) {
            struct process_element* p = &plist[i];
            lock_release(&plist_lock);

            struct process_element* p_parent = plist_find(parent_id);

            lock_acquire(&plist_lock);
            p->process_id = process_id;
            strlcpy(p->process_name, process_name, PLIST_NAME_SIZE);
            p->parent_id = parent_id;
            p->exit_status = -1;
            p->alive = true;
            p->parent_alive = (p_parent) ? p_parent->alive : false;
            p->used = true;

            sema_init(&p->exit_sync, 0);

            lock_release(&plist_lock);
            return process_id;
        }
    }
    lock_release(&plist_lock);
    return -1;
}

struct process_element* plist_find(int process_id)
{
    lock_acquire(&plist_lock);
    for (int i = 0; i < PLIST_SIZE; i++) {
        if (plist[i].process_id == process_id && plist[i].used) {
            struct process_element* p = &plist[i];
            lock_release(&plist_lock);
            return p; // Returns plist element if found and used is true 
        }
    }
    lock_release(&plist_lock);
    return NULL;
}

int plist_remove(int process_id)
{
    struct process_element* p = plist_find(process_id);
    if (p)
    {   
        lock_acquire(&plist_lock);
        /* Remove self and broadcast to children */
        p->alive = false;

        /* If parent is alive, don't free the position otherwise free it*/
        p->used = p->parent_alive;
        lock_release(&plist_lock);

        /* Tell all children that I am dead (parent) */
        plist_broadcast_to_children(p->process_id);
        
        return p->process_id;
    }
    return -1;
}

void plist_purge(void)
{
    for (int i = 0; i < PLIST_SIZE; i++) plist_remove(i);
}

void plist_broadcast_to_children(int parent_id)
{
    struct process_element* p_parent = plist_find(parent_id);

    lock_acquire(&plist_lock);
    for (int i = 0; i < PLIST_SIZE; i++) {
        struct process_element* p = &plist[i];
        if (p->parent_id == parent_id && p->used) {   
            /* Updates to parent's alive status, if parent does not exist set to false */
            p->parent_alive = (p_parent != NULL) ? p_parent->alive : false;
            p->used = (p_parent != NULL) ? p_parent->alive : false;
        }
    }
    lock_release(&plist_lock);
}

void plist_print_all()
{
    lock_acquire(&plist_lock);
    int count = 0;
    // Print table header
    printf("ProcessID\tProcessName\tParentID\tExitStatus\tAlive\tParentAlive\n");
    printf("---------\t-----------\t--------\t----------\t-----\t-----------\n");
    for (int i = 0; i< PLIST_SIZE; i++) {
        struct process_element* p = &plist[i];
        if (p->used)
        {
            // Align values to table header
            printf("%9d\t%11s\t%8d\t%10d\t%5s\t%11s\n",
                    p->process_id,
                    p->process_name,
                    p->parent_id,
                    p->exit_status,
                    (p->alive) ? "true" : "false",
                    (p->parent_alive) ? "true" : "false");
            count++;
        }
    }
    printf("\nTotal processes: %d\n", count);
    printf("--------------------------\n");
    lock_release(&plist_lock);
}
