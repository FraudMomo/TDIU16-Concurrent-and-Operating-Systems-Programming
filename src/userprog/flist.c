#include <stddef.h>
#include "flist.h"

void flist_init(struct flist* m) {
    lock_init(&m->flist_lock);
    for (int i = 2; i <= FLIST_SIZE; i++) m->content[i] = NULL;
}

key_t flist_insert(struct flist* m, value_t v) {
    // lock_acquire(&m->flist_lock);
    for (int i = 2; i <= FLIST_SIZE; i++) {
        if (m->content[i] == NULL) {
            m->content[i] = v;
            return i;
        }
    }
    // lock_release(&m->flist_lock);
    return -1;
}

value_t flist_find(struct flist* m, key_t k) {
    if (k >= 2 && k <= FLIST_SIZE)
    {
        // lock_acquire(&m->flist_lock);
        value_t item = m->content[k];
        // lock_release(&m->flist_lock);
        return item;
    }
    return NULL;
}

value_t flist_remove(struct flist* m, key_t k) {
    if (k >= 2 && k <= FLIST_SIZE) {
        // lock_acquire(&m->flist_lock);
        value_t removed_item = m->content[k];
        m->content[k] = NULL;
        // lock_release(&m->flist_lock);
        return removed_item;
    }
    return NULL;
}

void flist_purge(struct flist* m) {
    for (int i = 2; i <= FLIST_SIZE; i++) flist_remove(m, i);
}
