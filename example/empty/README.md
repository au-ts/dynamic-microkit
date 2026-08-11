<!--
     Copyright 2026, UNSW
     SPDX-License-Identifier: CC-BY-SA-4.0
-->
# Example - Empty PD using template PD

This is a basic example that uses template PD which contains no program image
to late-load application payloads. A template PD is a special PD that must be
initialized without program image and as a child to other PDs who are not templates.

In the given example, a "loader" PD is designed to be the parent of a template PD
that called "loadee". The loader PD has access to the *Thread Control Block* (TCB)
of the loadee, while listens to the faulting signals from the loadee as well.
Once the loadee faults on events such as (1) initialisation, and (2) proactive fault,
the loader restarts the loadee by updating the TCB of loadee using microkit interfaces
that wrap around `seL4_TCB_WriteRegisters`. The loader PD, in this example, does not
require any other capabilities such as pagetable/SC/channels of the loadee to function.

The loader PD shares a memory region with the loadee PD. This memory region will be
a region where the loadee's late-loaded application payload execute from. The requirement
is that the application payload must be compiled and linked to an elf file, which should
executes within the boundary of the given memory region on the loadee side.

The boot phase of the loadee PD is:

- capDL initialiser sets up all resources that are required to build the template PD.
- the template PD's default TCB context is set to be all zero, making it fault at 0x0.
- the loader who receives the fault signals from the loadee (i.e., template) will
  captures the first fault from loadee that faults at 0x0.
- the loader loads a statically-linked elf which begins at a given vaddr (e.g., 0x2800000)
  to a shared memory region between the loader and the loadee.
- the loader adjusts the TCB of loadee and restarts it via microkit interfaces by setting
  the `pc` register of loadee at the given vaddr (i.e., the elf entry).
- the loadee starts executing...

This example also tests the 'sym_emit' attribute for protection domains. PDs with such an
attribute will let the microkit tool dump a build artifact, which is called "symbol bundle",
containing all microkit symbols and setvar values that should be patched at system build time.
In the given sdf, template PD 'loadee' is configured with `sym_emit="true"` and the
microkit tool will emit the microkit symbols (e.g., `microkit_name`, `microkit_notifications`)
to a file called `loadee.mktsymb` in `BUILD/symbols`.

Such an attribute is useful for the template PDs who have no program image.
The `'pd_name'.mktsymb` file contains the microkit symbols that are not directly accessible
for PDs who have no images to patch with at build time. A template PD user can use external
tools to patch their application payload with the mktsymb file asynchronously.

In our example, given that the program image of the loadee is linked with the loader before
system build time, we demonstrate the usage of sym_emit by introducing a two-stage build
procedure.
+ At the first stage, we provide an SDF and the elf of loader and loadee to microkit, the
  microkit tool will generate a `loadee.mktsymb` for the loadee. 
+ We then use a py script called `payload-patcher.py` to write all symbols in `loadee.mktsymb`
  into the elf of loadee, and regenerate the elf of loader using the new loadee elf.
+ At the second stage, we provide the same SDF with newer elfs to microkit, and create the
  final system image.

All supported platforms are supported in this example.

## Building

```sh
mkdir build
make BUILD_DIR=build MICROKIT_BOARD=<board> MICROKIT_CONFIG=<debug/release/benchmark> MICROKIT_SDK=/path/to/sdk
```

## Running

See instructions for your board in the manual.

You should see the following output:

```
INFO  [sel4_capdl_initializer::initialize] Starting CapDL initializer
INFO  [sel4_capdl_initializer::initialize] Starting threads
MON|INFO: Microkit Monitor started!
>> loader: hi
>> receive the first fault from an empty pd with id: '0'
microkit name: loadee starting
>> loader: received from loadee, mktsymb tested
>> seL4_Fault_VMFault
>> Fault address: '0x0000000000000000'
>> Fault instruction pointer: '0x0000000000028090'
microkit name: loadee starting
>> loader: received from loadee, mktsymb tested
>> seL4_Fault_VMFault
>> Fault address: '0x0000000000000000'
>> Fault instruction pointer: '0x0000000000028090'
microkit name: loadee starting
>> loader: received from loadee, mktsymb tested
>> seL4_Fault_VMFault
>> Fault address: '0x0000000000000000'
>> Fault instruction pointer: '0x0000000000028090'
>> loader: too many restarts - PD stopped
```
