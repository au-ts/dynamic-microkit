/*
 * Copyright 2026, UNSW
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <microkit.h>

#include "elf.h"

extern const unsigned char __pc_start[];
extern const unsigned char __pc_end[];

/*
 * The shared memory region represents a region within
 * the virtual address space of the template PD.
 * The size of the region is 0x40000 and the region begins at 0x20000
 * 
 * The entry/loadable regions of the elf payload should live
 * within the region mentioned above.
 */
#define LOADER_MR_BASE (0xC00000)
#define LOADEE_MR_BASE (0x20000)
#define MR_SIZE (0x40000)

static uint8_t restart_count = 0;

static inline char hexchar(unsigned int v)
{
    return v < 10 ? '0' + v : ('a' - 10) + v;
}

/* stolen from monitor/src/util.c */
void puthex64(seL4_Uint64 val)
{
    char buffer[16 + 3];
    buffer[0] = '0';
    buffer[1] = 'x';
    buffer[16 + 3 - 1] = 0;
    for (unsigned i = 16 + 1; i > 1; i--) {
        buffer[i] = hexchar(val & 0xf);
        val >>= 4;
    }
    microkit_dbg_puts(buffer);
}


void *custom_memcpy(void *restrict dest,
                    const void *restrict src,
                    size_t n)
{
    unsigned char *d = (unsigned char *)dest;
    const unsigned char *s = (const unsigned char *)src;

    for (size_t i = 0; i < n; ++i) {
        d[i] = s[i];
    }
    return dest;
}

void *custom_memset(void *dest, int c, size_t n)
{
    unsigned char *d = (unsigned char *)dest;

    for (size_t i = 0; i < n; ++i) {
        d[i] = (unsigned char)c;
    }
    return dest;
}

static inline bool
custom_check_elf_segment(const Elf64_Phdr *phdr)
{
    /* Check each loadable segment lives within the pre-populated MR
     * (i.e., base=0x20000,size=0x40000).
     * We can verify all segments by comparing its boundary with the MR
     */
    size_t     seg_size = phdr->p_memsz;
    Elf64_Addr seg_base = phdr->p_vaddr;
    Elf64_Addr seg_end  = seg_base + seg_size;

    if (seg_base < LOADEE_MR_BASE || seg_end > LOADEE_MR_BASE + MR_SIZE) {
        microkit_dbg_puts("loader: PT_LOAD is outside the loadee MR\n");
        return false;
    }
    return true;
}

static inline bool
custom_check_elf_entry(const Elf64_Ehdr *ehdr)
{
    if (ehdr->e_entry < LOADEE_MR_BASE ||
        ehdr->e_entry >= LOADEE_MR_BASE + MR_SIZE) {
        microkit_dbg_puts("loader: entry point is outside loadee MR\n");
        return false;
    }
    return true;
}

bool custom_elfload(const Elf64_Ehdr *ehdr)
{
    if (custom_check_elf_entry(ehdr) != true) {
        return false;
    }

    const Elf64_Phdr *phdr =
        (const Elf64_Phdr *)((const char *)ehdr + ehdr->e_phoff);

    // try to load all loadable segements...
    //
    for (Elf64_Half i = 0; i < ehdr->e_phnum; i++) {
        if (phdr[i].p_type != PT_LOAD) {
            continue;
        }
        // check loadability of segements
        if (custom_check_elf_segment(&phdr[i]) != true) {
            return false;
        }

        // given that each segement lives within the pre-populated MR,
        // we need to calculate the offset to determine the relative
        // location of the segement to load.
        size_t seg_offset = (size_t)(phdr[i].p_vaddr - LOADEE_MR_BASE);

        // target location from the perspective of the loader.
        void *dest = (char *)(LOADER_MR_BASE + seg_offset);

        // relative location in the image.
        const void *infile_loc = (const char *)ehdr + phdr[i].p_offset;

        custom_memcpy(dest, infile_loc, phdr[i].p_filesz);

        // loadable segment could contain empty region...
        if (phdr[i].p_memsz > phdr[i].p_filesz) {
            size_t bss_size = phdr[i].p_memsz - phdr[i].p_filesz;
            // memzero the remaining region that should be zeroed
            custom_memset((char *)dest + phdr[i].p_filesz, 0, bss_size);
        }
    }

    return true;
}

int custom_memcmp(const unsigned char *s1, const unsigned char *s2, int n)
{
    for (int i = 0; i < n; i++) {
        if (s1[i] != s2[i]) {
            return (s1[i] - s2[i]);
        }
    }
    return 0;
}

void init(void)
{
    microkit_dbg_puts(">> loader: hi\n");
}

void notified(microkit_channel ch)
{
}

seL4_MessageInfo_t protected(microkit_channel ch, microkit_msginfo msginfo)
{
    return microkit_msginfo_new(0, 0);
}

seL4_Bool fault(microkit_child child, microkit_msginfo msginfo, microkit_msginfo *reply_msginfo)
{
    seL4_Word label = microkit_msginfo_get_label(msginfo);
    if (label == seL4_Fault_VMFault) {
        seL4_Word ip = microkit_mr_get(seL4_VMFault_IP);
        seL4_Word address = microkit_mr_get(seL4_VMFault_Addr);
        seL4_Word notify_flag = ip | address;
        if (notify_flag) {
            microkit_dbg_puts(">> seL4_Fault_VMFault\n");
            microkit_dbg_puts(">> Fault address: '");
            puthex64(address);
            microkit_dbg_puts("'\n");
            microkit_dbg_puts(">> Fault instruction pointer: '");
            puthex64(ip);
            microkit_dbg_puts("'\n");
        } else {
            microkit_dbg_puts(">> receive the first fault from an empty pd with id: '");
            microkit_dbg_put32(child);
            microkit_dbg_puts("'\n");
        }
    }

    const Elf64_Ehdr *hdr = (const Elf64_Ehdr *)(__pc_start);
    if (restart_count == 0) {
        if (custom_memcmp(hdr->e_ident, (const unsigned char *)ELFMAG, SELFMAG) != 0) {
            while (1);
        }
        if (!custom_elfload(hdr)) {
            microkit_dbg_puts("loader: failed to load ELF\n");
            while (1);
        }
    }
    if (restart_count < 3) {
        microkit_pd_restart(child, hdr->e_entry);
    } else {
        microkit_pd_stop(child);
        microkit_dbg_puts(">> loader: too many restarts - PD stopped\n");
    }
    restart_count++;

    /* We explicitly restart the thread so we do not need to 'reply' to the fault. */
    return seL4_False;
}