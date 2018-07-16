/*
 * Copyright 2018, Intelligent Automation, Inc.
 * This software was developed in part under Air Force contract number FA8750-15-C-0066 and DARPA 
 *  contract number 140D6318C0001.
 * This software was released under DARPA, public release number XXXXXXXX (to be updated once approved)
 * This software may be distributed and modified according to the terms of the BSD 2-Clause license.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY
 * WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * See "LICENSE_BSD2.txt" for details.
 *
 * @TAG(IAI_BSD)
 */
/**
 * @file init.h
 * @brief Top-level include for libinit.
 *
 */

#pragma once


#include <init/prototypes.h>
#include <init/types.h>
#include <init/globals.h>
#include <init/layouts.h>
#include "init_data.pb-c.h"

/**
 * @brief Atomically checks for initialization of the init objects
 */
static inline bool
init_check_initialized(void) {
    return __atomic_load_n(&init_objects.initialized, __ATOMIC_SEQ_CST) ? true : false;
}

static inline int
init_lock_init(seL4_CPtr notification) {
    #ifdef CONFIG_DEBUG_BUILD
    ZF_LOGF_IF(seL4_DebugCapIdentify(notification) != 6, "Init Notification has wrong cap type");
    #endif
    return sync_recursive_mutex_init(&init_objects.init_lock, notification);
}

static inline int
init_lock_objects(void) {
    return sync_recursive_mutex_lock(&init_objects.init_lock);
}

static inline int
init_unlock_objects(void) {
    return sync_recursive_mutex_unlock(&init_objects.init_lock);
}
