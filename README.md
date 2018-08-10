# seL4 CoE Developer Framework

## Features
*  **CoE Library Features:**
	* Simple, thread-safe, posix-like API for creating and syncronizing threads
	* API for creating and interconnecting processes
	* Simplified system/process initialization
* **Additions to the existing user-land seL4 Libraries:**
	* Thread-safe wrapper for the kernel object allocator *(vka)* and virtual memory manager *(vspace)*
	* Thread-safe malloc
	* Patch fixing overpriviledged writable & executable memory mappings
* **Kernel Changes :**
	* Physical Memory Map Debugging Kernel Call (seL4_DebugProcMap)
	* Sleep Kernel Call
	* ARM 64-Bit Multicore Support
	* Additional Scheduler Options

**`!!! NOTE: our kernel modifications have not been formally verified.`**

-----

## Setup

### Build Dependencies
You will first need to setup your machine according to the [seL4 Host Dependencies Guide](https://docs.sel4.systems/HostDependencies.html).

In addition you will need to install the protobuf c compiler:
`apt-get install protobuf-c-compiler`

### Obtaining the code
TODO


### Basic File Structure
The basic file structure follows this layout. After building a number of extra folders will also be created for build artifacts.

| Folder | Symlink | Description |
|--------|---------|-------------|
| **apps/** | _apps are individually linked_ | Folder where the build system looks for apps to build. |
| **configs/** | projects/coe/master-configs/ | Location of default configuration files. A header file with `#define` statements is generated based on the active config settings. |
| **kernel/** | n/a | Location of the kernel's source |
| **libs/** | _libs are individually linked_ | Folder where the Build system looks for libraries to build. |
| **projects/** | n/a | Locatation of the git repositories. The files and folders in apps and libs are generally symlinked into this folder. |
| **tools/** | n/a | Build tools and runtime tools |
| **.config** | n/a | Current configuration settings |
| **CMakeLists.txt** | projects/tools/cmake-tool/default-CMakeLists.txt | Root CMakeLists.txt file for any projects using CMake |
| **Kbuild** | projects/coe/build-config/Kbuild-sel4 | Root Kbuild file, generally you will not modify this file. |
| **Kconfig** | projects/coe/build-config/Kconfig-sel4 |	Root Kconfig file which is used to setup how the configs are created (i.e. what config options are available). This will need to be modified when a new app or library is added to the build process. |
| **Makefile** | projects/coe/build-config/Makefile-sel4 | Root makefile for the system. |


### Building
Our framework is based off the seL4 kernel version 8.0.0 which uses the Kbuild build system. This system is documented [here](https://docs.sel4.systems/Developing/Building/OldBuildSystem/). 

In short, you will need to select a configuration file based on your target platform. The default configuration files are stored in the configs folder.  The following command copies a default config's options  your _.config_ file, overwiting it:

**Building ZynqMP:**
```
make clean
make zynqmp_debug_defconfig
make
```

**Building Raspberry Pi:** 
```
make clean
make rpi3_debug_defconfig
make
```

**Building Zynq7000 for Qemu:**
```
make clean
make zynq7000_debug_sim_defconfig
make
```

**Note:** Some devices have separate simulation config files. If you plan on running a simulation in qemu, use those configs.

### Running
You can use Qemu to simulate the image you just built. The build system supports simulation of a few systems.

**Zync7000 Simulation:**
```
make simulate-zynq7000 image=images/<image file name>
```

**Sabrelite Simulation:**
```
make simulate-sabre image=images/<image file name>
```

For more information on simulating sel4 with qemu, [see here](https://docs.sel4.systems/Hardware/Qemu/). Checkout the seL4 wiki for the details on how to run sel4 on hardware.

#### Supported/Tested Hardware


| Platform  | Cores | Supported Architecture
| ------------- | ------------- | -------------|
| ZynqMP  | 4  | ARM 64/32 |
| Zynq7000 | 2\* | ARM 32 |
| Raspberry Pi | 1 | ARM 32 |
| Sabrelite | 4\*  | ARM 32 |
| Nvidia Tegra K1 | 1 | ARM 32 |
| Beaglebone | 1  | ARM 32 |


\*Qemu can only simulate these devices with a single core.

See [seL4's Supported Hardware Page](https://docs.sel4.systems/Hardware/) for more.

### Debugging
If you get a cap fault like this with running:
```
Caught cap fault in send phase at address 0x0
while trying to handle:
vm fault on data at address 0x0 with status 0x7
in thread 0xffdfd900 "rootserver" at address 0xdeadbeef
```
There are a few ways to debug using existing tools like objdump and gdb. If we take the address `deadbeef` listed in the fault message. We can find where the fault occured by searching for the address in the app that crashed. In this case, `"rootserver"` is a thread running in our "root task" app. 

```
make objdump app=root_task | less
```
Once inside less, type `/deadbeef` to search for the crash location. If we want to plug in gdb to a qemu simulation we can simulate with the -gdb option. This will stop qemu before it loads the image to allow a debugger to be attached. For example:
```
make simulate-zynq7000-gdb image=images/<image file name>
```
In a separate terminal instance use gdb:
```
arm-none-eabi-gbd 
target remote localhost:1234
<set breakpoints>
c
```

-----


## CoE Library Design
Our libraries abstract away from the seL4 Libraries and allow the programmer to better focus on developing the core logic of their applications.

```
+----------+-----------+
| App      | App       |
+----------+-----------+
| CoE Libraries        |
+----------------------+
| seL4 Libraries       |
+----------------------+
| seL4 Kernel API      |
+----------------------+
```

### Structure
Our libraries are split into three external libraries for developers and two internal libraries.


* Public Libraries:
	* **Libinit** - All things initialization. Setup virtual memory manager and untyped memory manager.
	* **Libthread** - Start and destroy threads. Spinlocks. Wrapper for libsel4sync (mutexes using notifications)
	* **Libprocess** - Start and destroy processes. Interconnect children with IPC primitives.
* Internal Libraries:
	* **Libmmap** - Helper for mapping memory. Is able to map memory marked as non-executable.
	* **Liblockwrapper** - Thread safety shims implementing the vka and vspace interfaces.

### Libinit
If a process is going to use the existing seL4 Libraries a significant amount of boilerplate code is necesarry for each process. This library takes care of that for both the root task (init process started by the kernel) and its ancestors.

Root task example initialization:
```c
int main() {
	err_code = init_root_task();
	...
```
Example for children that have been started using libprocess:
```c
int main(int argc, char *argv[]) {
	err_code = init_process();
	...
```
Once initialization is completed. A global variable `init_objects` is available. The internal allocators/managers can be accessed if needed via `init_objects.vka`  and `init_objects.vspace`.

**Looking up resources given by parent**
If our parent gave us any resources or connections we can use a few init calls to look them up:

| Function name | Description                    |
| ------------- | ------------------------------ |
| `seL4_CPtr init_lookup_endpoint(const char*)`  | Return the cap to the endpoint with a given string name.  |
| `seL4_CPtr init_lookup_notification(const char*)`  | Return the cap to the notification with a given string name.  |
| `void* init_lookup_shmem(const char*)`  | Return a pointer to the shared memory with a given string name.  |
| `void* init_lookup_devmem(const char*)`  | Return a pointer to the device memory with a given string name.  |
| `int init_lookup_devmem_info(const char*, init_devmem_info_t *)`  | Output a pointer to information for the device memory with a given string name.  |
| `int init_lookup_irq(const char*, init_irq_info_t *)`  | Output a pointer to information for ther IRQ device with a given string name.  |

### Libthread

**Starting and Destroying Threads.**
If your process has untyped memory available, you may start a new thread. All the memory/resources needed to create the thread comes from untyped memory objects. The following example shows how to start and destroy a thread. Note that you can also destroy running threads.
```c
// Create a handle
thread_handle_t *handle = thread_handle_create(&thread_1mb_high_priority);

// Start the execution of the thread at worker_thread_func
err_code = thread_start(handle, worker_thread_func, (void*)arg);

// Wait for the thread to finish
void *ret = thread_join(handle);

// Destroy the thread and free the handle
err_code = thread_destroy_free_handle(&handle);
```

**Using Synchronization Primitives.**
We provide notification-based locks and spin locks with our library. Notification-based locks use help from the kernel using notification endpoints.
```c
cond_init(&cond_var, LOCK_NOTIFICATION);

//...

cond_lock_aquire(&conv_var);
if(flag) {
  cond_broadcast(&cond_var);
} else {
  flag = 1;
  cond_wait(&cond_var);
}
cond_lock_release(&cond_var);
```

### Libprocess
**Creating and Destroying Processes.**
```c
err_code = process_create("file_name", "proc_name", &process_default_attrs, &handle);

char * argv[] = { "arg1", "arg2" };
err_code = process_run(&handle, sizeof(argv)/sizeof(argv[0]), argv);

//...

err_code = process_destroy(&handle);
```

**Interconnecting Processes (Connection Objects).**
This table covers the three basic connection objects (conn_obj) available in our library. For more information about the semantics of these objects checkout the [seL4 Manual](https://sel4.systems/Info/Docs/seL4-manual-9.0.0.pdf).

| Connection Object | Macro | Description |
| ----------------- | ----- | ----------- |
| Endpoint | PROCESS_ENDPOINT | Rendevous point between two threads/processes. Use seL4_Send/seL4_Recv or seL4_Call/seL4_Reply to pass messages that are less than 127 words. |
| Notification | PROCESS_NOTIFICATION | Asynchronous flag setting, follows basic semaphore semantics with seL4_Signal/seL4_Wait kernel calls. |
| Shared Memory | PROCESS_SHARED_MEMORY | A physical region of memory which is mapped into two or more address spaces. |

You can pair these objects together to make more sophisticated systems. _E.g._ a notification can act as a semaphore, controlling access to a shared region of memory.

**Using Connection Objects.**
The following code segment illustrates how to create arbitrary IPC graphs with connection objects and processes.

```c
// Create an endpoint connection object with the name example-ep (NULL indicates to used the default attributes).
err_code = process_create_conn_obj(PROCESS_ENDPOINT, "example-ep", NULL, &ep_obj);

// Connect the new endpoint to proc1 with read/write permissions (NULL for attributes and for the out param).
err_code = process_connect(&proc1_handle, ep_obj, process_rw, NULL, NULL);

// Connect the new endpoint to proc2 with read only permissions.
err_code = process_connect(&proc2_handle, ep_obj, process_ro, NULL, NULL);

// Connect the new endpoint to ourselves (for endpoints this just mean pulling out the inner Cptr).
err_code = process_connect(PROCESS_SELF, ep_obj, process_rwg, NULL, &connect_ret);
seL4_CPtr parent_cap = connect_ret.self_cap;


//... Once we are finished

// We need to destroy the procs before we can cleanup the endpoint.
err_code = process_destroy(&proc1_handle);
err_code = process_destroy(&proc2_handle);

// Our self connection is automatically destroyed
err_code = process_free_conn_obj(&ep_obj);
```

Before the destruction steps this is the IPC graph we have created:

```
+---------+                                +---------+
| proc1   | --RW--> ( example-ep ) <--R--- | proc2   |
+---------+                ^               +---------+
                           |RWG
                       +---------+ 
                       | parent  |
                       +---------+
```

Each `process_connect` step creates a new edge.


**Adding devices to processes**
```c
```


## Potential Future Efforts
- [ ] Error codes and better error handling
- [ ] Intel support
- [ ] Custom fault endpoints for threads
- [ ] Better performance on syncronization/thread safety.
- [ ] Better performance on process startup and destruction
- [ ] Hypervisor support
- [ ] Thread pool
- [ ] cpio / filesystem improvements

## Known Issues
- [ ] seL4test MULTICORE0003 failure






