/**
 * @file mmap.c
 * @brief Core implementation of libmmap
 */
#include <stdio.h>
#include <stdlib.h>

#include <sel4/sel4.h>
#include <vka/vka.h>
#include <vspace/vspace.h>
#include <utils/util.h>
#include <sel4utils/helpers.h>

#include <init/init.h>

