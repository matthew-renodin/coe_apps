/**
 * @file lookup.c
 * @brief Implmentation of lookup functions for the init data
 */
#include <autoconf.h>

#include <stdint.h>
#include <string.h>

#include <sel4/sel4.h>
#include <init/init.h>

#define LOOKUP(RET, SUFFIX, TYPE, LIST, FIELD)                                  \
    RET init_lookup_##SUFFIX(const char * name)                                 \
    {                                                                           \
        if(!init_objects.initialized || !init_objects.init_data) {              \
            ZF_LOGE("Invalid usage of init library");                           \
            return 0;                                                           \
        }                                                                       \
        TYPE *iter = init_objects.init_data->LIST;                              \
        ZF_LOGD_IF(iter == NULL, "No elements in list when looking up.");       \
        while(iter) {                                                           \
            if(strcmp(name, iter->name) == 0) return (RET)iter->FIELD;          \
            iter = iter->next;                                                  \
        }                                                                       \
        ZF_LOGD("Unable to locate init data with the given name");              \
        return 0;                                                               \
    }

LOOKUP(seL4_CPtr,   endpoint,       EndpointData,       ep_list_head,           cap);
LOOKUP(seL4_CPtr,   notification,   EndpointData,       notification_list_head, cap);
LOOKUP(void*,       shmem,          SharedMemoryData,   shmem_list_head,        addr);
LOOKUP(void*,       devmem_addr,    DeviceMemoryData,   devmem_list_head,       virt_addr);

int init_lookup_irq(const char * name, init_irq_info_t *info)
{
    if(!init_check_initialized() || !init_objects.init_data) {
        ZF_LOGE("Invalid usage of init library");
        return -1;
    }

    if(info == NULL) {
        ZF_LOGE("NULL pointer to info struct passed");
        return -2;
    }

    IrqData * iter = init_objects.init_data->irq_list_head;
    ZF_LOGD_IF(iter == NULL, "No elements in list when looking up.");

    while(iter) {
        if(strcmp(name, iter->name) == 0) {
             info->ep = iter->ep_cap;
             info->irq = iter->irq_cap;
             info->number = iter->number;
            return 0;
        }
        iter = iter->next;
    }
    ZF_LOGD("Unable to locate init data with the given name");
    return -3;
}


int init_lookup_devmem_info(const char * name, init_devmem_info_t *info)
{
    if(!init_objects.initialized || !init_objects.init_data) {
        ZF_LOGE("Invalid usage of init library");
        return -1;
    }

    if(info == NULL) {
        ZF_LOGE("NULL pointer to info struct passed");
        return -2;
    }

    DeviceMemoryData * iter = init_objects.init_data->devmem_list_head;
    ZF_LOGD_IF(iter == NULL, "No elements in list when looking up.");

    while(iter) {
        if(strcmp(name, iter->name) == 0) {
            info->vaddr = (void*)iter->virt_addr;
            info->paddr = (void*)iter->phys_addr;
            info->size_bits = iter->size_bits;
            info->num_pages = iter->num_pages;

            if(iter->n_caps32 == iter->num_pages && sizeof(seL4_CPtr) == sizeof(uint32_t)) {
                info->caps = (seL4_CPtr*)iter->caps32;
            } else if(iter->n_caps64 == iter->num_pages && sizeof(seL4_CPtr) == sizeof(uint64_t)) {
                info->caps = (seL4_CPtr*)iter->caps64;
            } else {
                info->caps = NULL;
            }

            return 0;
        }
        iter = iter->next;
    }
    ZF_LOGD("Unable to locate init data with the given name");
    return -3;
}

