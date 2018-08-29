# seL4 USA Center of Excellence (CoE) is now Open for Business

If you have any questions, please send an email to
[mail@sel4-us.com](mailto:mail@sel4-us.org).
  
If there is something on the project that should be improved, please 
create a GitHub issue.
  
Of course, one of the main purposes of the CoE is to foster contributions.
Please join the CoE by posting changes and contributions.

# Overview

The CoE is the center point for the USA based ecosystem for seL4.
The goals of the CoE are:

* Increase Collaboration between seL4 contributors
* Increase Industry Adoption of seL4
* Increase the Maturity of seL4 by:
  * Adding formally verifiable features and libraries
  * Increasing stability
  * Adopting modern software engineering practices

For more information, please visit https://sel4-us.org

# What's New

* POSIX-like process and thread creation
* Simplified capability generation/passing
* Simplified messaging configuration
* Memory map verification for AARCH32 and AARCH64
* Corrected RW- and R-X memory on AARCH32 and AARCH64
* Thread-safe malloc()
* Thread-safe vka() and vspace()
* Round-Robin scheduling
* Sleep() and GetTicker() functions
* POSIX-like mutexes, conditions, and reentrant locks
* Multi-core AARCH64

# Caveats

CoE-developed software is expressly provided "as is." The CoE makes no warranty
of any kind, express, implied, in fact, or arising by operation of law,
including, without limitation, the implied warranty of merchantability, fitness
for a particular purpose, non-infringement and data accuracy. The CoE neither
represents not warrants that the operation of the software will be uninterrupted
or error-free, or that any defects will be corrected. The CoE does not warrant
or make any representations regarding the use of the software or the results
thereof, including but not limited to the correctness, accuracy, reliability,
or usefulness of the software.

You are solely responsible for determining the appropriateness of using and
distributing the software and you assume all risks associated with its use,
including but not limited to the risks and costs of program errors, compliance
with applicable laws, damage to or loss of data, programs, or equipment, and
the unavailability or interruption of operation. This software is not intended
to be used in any situation where a failure could cause risk of injury or damage
to property.

**WARNING: SEL4 IS PROVIDED AS A RESEARCH PROJECT. DO NOT USE FOR PRODUCTION.**
The formally verified microkernel code can be found as portions of the file
“kernel_all.c”. Everything else, all other software that is provided, is for
convenience only, and is known to be untrusted, untested, unverified,
non-secure, and unsuitable for any use other than furthering the R&D into
secure microkernels.