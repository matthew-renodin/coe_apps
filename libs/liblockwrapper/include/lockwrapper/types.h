#pragma once

#include <sel4/sel4.h>
#include <vka/vka.h>
#include <vspace/vspace.h>

#include <stdbool.h>

typedef struct lock_interface lock_interface_t;

struct lock_interface {
	void *data;
	int (*mutex_lock)(void *m);
	int (*mutex_unlock)(void *m);
};

typedef struct lockvka{
    vka_t parent_vka;
    lock_interface_t lock;
} lockvka_t;

typedef struct lockvspace{
    vspace_t parent_vspace;
    lock_interface_t lock;
} lockvspace_t;