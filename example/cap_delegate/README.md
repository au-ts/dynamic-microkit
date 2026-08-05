# Capability Delegation

Capability delegation allows a parent protection domain (the **delegatee**) to dynamically control a subset of capabilities associated with one of its child protection domains (the **delegator**).

Instead of installing every capability directly into the delegator PD's CSpace, capabilities marked as *delegated="true"* are placed into a separate **delegation CNode**. The delegation CNode is controlled by the delegatee PD and represents the delegation relationship between one delegatee-delegator pair.

This allows capability availability to be changed dynamically at runtime without changing the static structure of the system.

## Delegation Model

For every `(delegatee, delegator)` PD pair, Microkit creates one delegation CNode.

The delegation CNode stores:

1. capabilities to the delegator PD's CSpace objects:

   * the delegator PD's root CNode;
   * the delegator PD's microkit CNode;
   * the delegation CNode itself;

2. capabilities for all resources of the delegator PD that are marked as delegated.

Conceptually:

```text
Delegatee PD                         Delegator PD
(parent)                             (child)

    root CNode                     ----> root CNode (A)
        |                          |       |
        |                          |       |
        +-- Microkit CNode         |        +-- Microkit CNode (B)
          |                        |            ^
     -> +-- delegation CNode (C)   |            |
     |          |                  |            |
     |          +-- cap -> A -------            |
     |          +-- cap -> B --------------------
     cap <-C -- +
                |
                +-- delegated resource caps
```

The delegation CNode is therefore not simply another CNode belonging to the delegator. It acts as a capability container associated with the delegation relationship between the delegatee and delegator.

By default, the delegatee can access the delegation CNode, while the delegator cannot.

## SDF Interface

Capability delegation is expressed through attributes in the system description.

A protection domain that manages delegated capabilities is marked with:

```xml
delegatee="true"
```

A child PD that is permitted to participate in delegation contains resources that are marked as delegated. For example, a channel end or a memory region can use:

```xml
delegated="true"
```

### Example

```xml
<protection_domain name="delegatee" priority="25" delegatee="true">
    <program_image path="delegatee.elf" />

    <protection_domain
        name="delegator"
        id="0"
        priority="20"
        allow_delegation="true">
        <program_image path="delegator.elf" />
    </protection_domain>

</protection_domain>

<protection_domain name="server" priority="25">
    <program_image path="server.elf" />
</protection_domain>

<channel>
    <end pd="server" id="0" />
    <end pd="delegator" id="0" delegated="true" />
</channel>
```

In this example:

* `delegatee` is responsible for managing delegated capabilities;
* `delegator` is a child PD whose capabilities may be delegated;
* the channel end belonging to `delegator` is marked as delegated.

Normally, the channel capability used by `delegator` would be installed in the delegator's Microkit CNode.

Because the channel end is marked with `delegated="true"`, this capability is instead created inside:

```text
delegation_cnode(delegatee, delegator)
```

As a result, the channel is not directly usable by the delegator when the system starts.

## CSpace Layout

Microkit PDs use a two-level CSpace.

A PD has a small root CNode (radix=6) and a larger Microkit CNode (radix=9):

```text
PD root CNode
    |
    +-- slot 0 -> Microkit CNode
```

For delegation, the delegatee (parent) additionally contains one delegation CNode for each delegator (child).

For example:

```text
Delegatee PD

root CNode
|
+-- slot 0  -> delegatee Microkit CNode
|
+-- slot 48 -> delegation CNode for delegator 0
+-- slot 49 -> delegation CNode for delegator 1
+-- ...
```

A delegation CNode contains both delegation-management capabilities and delegated resource capabilities:

```text
Delegation CNode
|
+-- slot 0 -> delegation CNode self-reference
+-- slot 1 -> delegator Microkit CNode
+-- slot 2 -> delegator root CNode
+-- slot 3 -> grantable delegation CNode cap
+-- slot 4 -> delegator VSpace
|
+-- resource slots
    +-- delegated notification caps
    +-- delegated endpoint caps
    +-- delegated memory-region frame caps
    +-- delegated I/O port caps
```

Delegated resource capabilities retain the same slot number that they would normally occupy in the delegator's Microkit CNode.

For example, notification channel `0` normally uses:

```text
BASE_OUTPUT_NOTIFICATION_CAP + 0
```

which is slot `10`.

If the corresponding channel end is delegated, the capability is placed at:

```text
delegation CNode slot 10
```

instead of:

```text
delegator Microkit CNode slot 10
```

This makes moving a capability between the delegation CNode and the normal Microkit CNode straightforward.

## Capability Management

There are two possible ways to manage delegated capabilities.

### Delegatee-Managed Delegation

The simplest model is for the delegatee to perform all CSpace operations.

Initially:

```text
delegation CNode[10]
    |
    +-- notification capability

delegator Microkit CNode[10]
    |
    +-- empty
```

When the delegator requests access to the resource, it performs a protected procedure call to the delegatee.

The delegatee then copies the capability:

```text
delegation CNode[10]
        |
        | seL4_CNode_Copy
        v
delegator Microkit CNode[10]
```

The delegator can then use the normal Microkit API:

```c
microkit_notify(10);
```

When the capability should no longer be available, the delegatee can remove it from the delegator's CSpace.

The control flow is therefore:

```text
Delegator                  Delegatee

    |                          |
    | request capability       |
    |------------------------->|
    |                          |
    |                   copy capability
    |                   into delegator
    |                          |
    |<-------------------------|
    |
    | use capability
    |
```

This model keeps all capability management inside the delegatee.

## Delegator Self-Management

Capability management can also be temporarily offloaded to the delegator.

In this model, instead of copying individual resource capabilities on behalf of the delegator (child), the delegatee (parent) temporarily grants the delegator (child) access to its delegation CNode.

This requires two assumptions:

1. the delegatee (parent) explicitly grants access to the delegation CNode;
2. the capability-management code executed by the delegator (child) is trusted to follow the delegation protocol.

The delegation CNode already contains:

```text
slot 1 -> delegator Microkit CNode
slot 2 -> delegator root CNode
```

so once the delegator gains access to the delegation CNode, it can manage its own delegated capabilities.

### Grant

Initially:

```text
Delegator root CNode
|
+-- delegation access slot -> empty
```

The delegator asks the delegatee for temporary access.

The delegatee (parent) installs a capability to the *delegation CNode* into a reserved slot of the delegator's (child) root CNode:

```text
Delegator root CNode
|
+-- slot N -> delegation CNode
```

At this point, the delegator can directly access the delegation CNode.

### Restore a delegated capability

Suppose notification channel `0` is stored at delegation CNode slot `10`.

The delegator can use the Microkit CNode capability stored in delegation CNode slot `1` as the destination:

```text
delegation CNode
|
+-- slot 1  -> delegator Microkit CNode
|
+-- slot 10 -> notification cap
                 |
                 | seL4_CNode_Copy
                 v
         delegator Microkit CNode[10]
```

After this operation:

```text
delegator Microkit CNode[10]
    |
    +-- notification capability
```

and normal Microkit APIs can be used.

### Release

Once the delegator (child) finishes using the delegated resources, it removes the temporary resource capabilities from its normal Microkit CNode. It then calls back into the delegatee (parent) to indicate that capability self-management is complete. The delegatee (parent) finally removes the temporary delegation CNode capability from the delegator's (child) root CNode.

The complete flow is:

```text
Delegator                         Delegatee

    |                                 |
    | request delegation access       |
    |-------------------------------->|
    |                                 |
    |                    grant access to
    |                    delegation CNode
    |                                 |
    |<--------------------------------|
    |
    | copy delegated caps into
    | own Microkit CNode
    |
    | use delegated resources
    |
    | remove temporary resource caps
    |
    | release delegation access
    |-------------------------------->|
    |                                 |
    |                    remove access to
    |                    delegation CNode
    |                                 |
    |<--------------------------------|
```

This mode reduces the amount of per-capability management performed by the delegatee.

The delegatee controls when self-management begins and ends, while the delegator performs the individual capability operations.


## Demo

This example demonstrates delegation of a notification capability.

The system contains three PDs:

```text
delegatee
    |
    +-- delegator

server
```

There is a notification channel between `delegator` and `server`.

The delegator's channel end is marked:

```xml
delegated="true"
```

Therefore, at system startup:

```text
delegator Microkit CNode[10] = empty

delegation CNode[10] =
    notification capability to server
```

The demo first attempts:

```c
microkit_notify(10);
```

before the capability has been restored.

The capability is then made available using one of the delegation-management modes described above.

After the capability is installed into:

```text
delegator Microkit CNode[10]
```

the same call:

```c
microkit_notify(CH_SERVER);
```

can successfully use the delegated channel.

### Log

```
Booting all finished, dropped to user space
INFO  [sel4_capdl_initializer::initialize] Starting CapDL initializer
INFO  [sel4_capdl_initializer::initialize] Starting threads
MON|INFO: Microkit Monitor started!
 (server)  init
[delegatee] init
<delegator> hello world
<delegator> request delegation CNode
[delegatee] grant delegation CNode
<delegator> notify server from channel: 10
<<seL4(CPU 0) [decodeInvocation/643 T0x80602e2800 "delegator" @2000e0]: Attempted to invoke a null cap #10.>>
<delegator> notify server from channel: 10
<<seL4(CPU 0) [decodeInvocation/643 T0x80602e2800 "delegator" @2000e0]: Attempted to invoke a null cap #10.>>
<delegator> notify server from channel: 10
<<seL4(CPU 0) [decodeInvocation/643 T0x80602e2800 "delegator" @2000e0]: Attempted to invoke a null cap #10.>>
<delegator> restore cap: 10
<delegator> notify server from channel: 10
 (server)  received signal from delegator
<delegator> notify server from channel: 10
 (server)  received signal from delegator
<delegator> notify server from channel: 10
 (server)  received signal from delegator
<delegator> remove cap: 10
<delegator> release delegation CNode
[delegatee] release delegation CNode
[delegatee] notify delegator
<delegator>::notified: received signal from delegatee
<delegator>::notified: try notifying server
<<seL4(CPU 0) [decodeInvocation/643 T0x80602e2800 "delegator" @200398]: Attempted to invoke a null cap #10.>>
```
