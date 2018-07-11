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
LOOKUP(void*,       device_addr,    DeviceMemoryData,   devmem_list_head,       virt_addr);

init_irq_caps_t init_lookup_irq(const char * name)
{
    if(!init_check_initialized() || !init_objects.init_data) {
        ZF_LOGE("Invalid usage of init library");
        return (init_irq_caps_t){.ep = seL4_CapNull, .irq = seL4_CapNull};
    }

    IrqData * iter = init_objects.init_data->irq_list_head;
    ZF_LOGD_IF(iter == NULL, "No elements in list when looking up.");

    while(iter) {
        if(strcmp(name, iter->name) == 0) {
            return (init_irq_caps_t) {
                .ep = iter->ep_cap,
                .irq = iter->irq_cap,
            };
        }
        iter = iter->next;
    }
    ZF_LOGD("Unable to locate init data with the given name");
    return (init_irq_caps_t){.ep = seL4_CapNull, .irq = seL4_CapNull};
}
