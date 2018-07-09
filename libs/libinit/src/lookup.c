/**
 * @file lookup.c
 * @brief Implmentation of lookup functions for the init data
 */
#include <autoconf.h>

#include <stdint.h>
#include <string.h>

#include <sel4/sel4.h>
#include <init/init.h>

seL4_CPtr init_lookup_ep(const char * name)
{
    if(!init_objects.initialized || !init_objects.init_data) {
        ZF_LOGE("Invalid usage of init library");
        return seL4_CapNull;
    }

    EndpointData * iter = init_objects.init_data->ep_list_head;
    ZF_LOGD_IF(iter == NULL, "No endpoints in list when looking up.");

    while(iter) {
        if(strcmp(name, iter->name) == 0) return (seL4_CPtr)iter->cap;
        iter = iter->next;
    }
    ZF_LOGD("Unable to locate an ep with the given name");
    return seL4_CapNull;
}


seL4_CPtr init_lookup_notification(const char * name)
{
    if(!init_objects.initialized || !init_objects.init_data) {
        ZF_LOGE("Invalid usage of init library");
        return seL4_CapNull;
    }

    EndpointData * iter = init_objects.init_data->notification_list_head;
    ZF_LOGD_IF(iter == NULL, "No endpoints in list when looking up.");

    while(iter) {
        if(strcmp(name, iter->name) == 0) return (seL4_CPtr)iter->cap;
        iter = iter->next;
    }
    ZF_LOGD("Unable to locate an ep with the given name");
    return seL4_CapNull;
}

void * init_lookup_shmem(const char * name)
{
    if(!init_objects.initialized || !init_objects.init_data) {
        ZF_LOGE("Invalid usage of init library");
        return seL4_CapNull;
    }

    SharedMemoryData * iter = init_objects.init_data->shmem_list_head;
    ZF_LOGD_IF(iter == NULL, "No shmem regions in list when looking up.");

    while(iter) {
        if(strcmp(name, iter->name) == 0) return (void *)iter->addr;
        iter = iter->next;
    }
    ZF_LOGD("Unable to locate a shmem region with the given name");
    return NULL;
}

void * init_lookup_device_addr(const char * name)
{
    if(!init_objects.initialized || !init_objects.init_data) {
        ZF_LOGE("Invalid usage of init library");
        return seL4_CapNull;
    }

    DeviceMemoryData * iter = init_objects.init_data->devmem_list_head;
    ZF_LOGD_IF(iter == NULL, "No device regions in list when looking up.");

    while(iter) {
        if(strcmp(name, iter->name) == 0) return (void *)iter->virt_addr;
        iter = iter->next;
    }
    ZF_LOGD("Unable to locate a device region with the given name");
    return NULL;
}
