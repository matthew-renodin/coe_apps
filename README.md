# seL4 CoE Developer Framework


##Features
*  **CoE Library Features:**
	* Simple, thread-safe, posix-like API for creating and syncronizing threads
	* API for creating and interconnecting processes
	* Simplified system/process initialization
* **Additions to the existing user-land seL4 Libraries:**
	* Thread-safe wrapper for the kernel object allocator *(vka)* and virtual memory manager *(vspace)*
	* Thread-safe malloc
	* Patch fixing overpriviledged writable & executable memory mappings
* **Kernel Changes :**
	* Physical Memory Map Debugging Kernel Call
	* Sleep Kernel Call
	* ARM 64-Bit Multicore Support
	* Additional Scheduler Options

**`!!! NOTE: our kernel modifications have not been formally verified.`**

[TOC]

## Setup

### Build Dependencies
You will first need to setup your machine according to the [seL4 Host Dependencies Guide](https://docs.sel4.systems/HostDependencies.html).

In addition you will need to install the protobuf c compiler:
`apt-get install protobuf-c-compiler`

### Obtaining the code
**TODO**

### Basic File Structure
The basic file structure follows this layout. After building a number of extra folders will also be created for build artifacts.

* **apps/**
	Folder where the build system looks for apps to build.

* **configs/** _-> projects/coe/master-configs/_
	Location of default configuration files. A header file with `#define` definitions  is generated based on the active config settings.

* **kernel/**
	Location of the kernel's source

* **libs/**
	Folder where the Build system looks for libraries to build.

* **projects/**
	Locatation of the git repositories. The files and folders in apps and libs are generally symlinked into this folder.

* **tools/**
	Build tools and runtime tools.

* **.config**
	Current configuration settings

* **CMakeLists.txt** _-> projects/tools/cmake-tool/default-CMakeLists.txt_
	Root CMakeLists.txt file for any projects using CMake

* **Kbuild** -> projects/coe/build-config/Kbuild-sel4
	Root Kbuild file, generally you will not modify this file.

* **Kconfig** -> projects/coe/build-config/Kconfig-sel4
	Root Kconfig file which is used to setup how the configs are created (i.e. what config options are available). This will need to be modified when a new app or library is added to the build process.

* **Makefile** _-> projects/coe/build-config/Makefile-sel4_
	Root makefile for the system. 


### Building
Our framework is based off the seL4 kernel version 8.0.0 which uses the Kbuild build system. This system is documented [here](https://docs.sel4.systems/Developing/Building/OldBuildSystem/). 

In short, you will need to select a configuration file based on your target platform. The default configuration files are stored in the configs folder.  The following command copies a default config's options  your _.config_ file, overwiting it:
`make zynqmp_aarch64_debug_xml_defconfig`

Then you can use a simple `make` to start the build.

### Running

#### Supported Hardware
| First Header  | Second Header |
| ------------- | ------------- |
| Content Cell  | Content Cell  |
| Content Cell  | Content Cell  |


## CoE Library Design


### Structure

```flow
st=>start: Login
op=>operation: Login operation
cond=>condition: Successful Yes or No?
e=>end: To admin

st->op->cond
cond(yes)->e
cond(no)->op
```

```seq
Andrew->China: Says Hello 
Note right of China: China thinks\nabout it 
China-->Andrew: How are you? 
Andrew->>China: I am good thanks!
```

### Objects

| Function name | Description                    |
| ------------- | ------------------------------ |
| `help()`      | Display the help window.       |
| `destroy()`   | **Destroy your computer!**     |

| Item      | Value |
| --------- | -----:|
| Computer  | $1600 |
| Phone     |   $12 |
| Pipe      |    $1 |

| Left-Aligned  | Center Aligned  | Right Aligned |
| :------------ |:---------------:| -----:|
| col 3 is      | some wordy text | $1600 |
| col 2 is      | centered        |   $12 |
| zebra stripes | are neat        |    $1 |


## Developing with the seL4 CoE Framework
### Examples
#### How to add an app
#### How to start a process

## In Progress Efforts and Known Issues
- [x] GFM task list 1
- [x] GFM task list 2
- [ ] GFM task list 3
    - [ ] GFM task list 3-1
    - [ ] GFM task list 3-2
    - [ ] GFM task list 3-3
- [ ] GFM task list 4
    - [ ] GFM task list 4-1
    - [ ] GFM task list 4-2


