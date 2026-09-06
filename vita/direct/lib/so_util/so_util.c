/* so_util.c -- utils to load and hook .so modules
 *
 * Copyright (C) 2021 Andy Nguyen
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.	See the LICENSE file for details.
 */

#include <vitasdk.h>
#include <kubridge/kubridge.h>

#include <psp2/kernel/clib.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "utils/dialog.h"
#include "utils/telemetry.h"
#include "so_util.h"

int __ret0(void);  /* defined below; used by so_resolve's unresolved-symbol path */

#ifndef SCE_KERNEL_MEMBLOCK_TYPE_USER_RX
#define SCE_KERNEL_MEMBLOCK_TYPE_USER_RX                 (0x0C20D050)
#endif

typedef struct b_enc {
    union {
        struct __attribute__((__packed__)) {
            int imm24: 24;
            unsigned int l: 1; // Branch with Link flag
            unsigned int enc: 3; // 0b101
            unsigned int cond: 4; // 0b1110
        } bits;
        uint32_t raw;
    };
} b_enc;

typedef struct ldst_enc {
    union {
        struct __attribute__((__packed__)) {
            int imm12: 12;
            unsigned int rt: 4; // Source/Destination register
            unsigned int rn: 4; // Base register
            unsigned int bit20_1: 1; // 0: store to memory, 1: load from memory
            unsigned int w: 1; // 0: no write-back, 1: write address into base
            unsigned int b: 1; // 0: word, 1: byte
            unsigned int u: 1; // 0: subtract offset from base, 1: add to base
            unsigned int p: 1; // 0: post indexing, 1: pre indexing
            unsigned int enc: 3;
            unsigned int cond: 4;
        } bits;
        uint32_t raw;
    };
} ldst_enc;

#define B_RANGE ((1 << 24) - 1)
#define B_OFFSET(x) (x + 8) // branch jumps into addr - 8, so range is biased forward
#define B(PC, DEST) ((b_enc){.bits = {.cond = 0b1110, .enc = 0b101, .l = 0, .imm24 = (((intptr_t)DEST-(intptr_t)PC) / 4) - 2}})
#define LDR_OFFS(RT, RN, IMM) ((ldst_enc){.bits = {.cond = 0b1110, .enc = 0b010, .p = 1, .u = (IMM >= 0), .b = 0, .w = 0, .bit20_1 = 1, .rn = RN, .rt = RT, .imm12 = (IMM >= 0) ? IMM : -IMM}})

#define PATCH_SZ 0x10000 //64 KB-ish arenas
static so_module *head = NULL, *tail = NULL;

so_hook hook_thumb(uintptr_t addr, uintptr_t dst) {
    so_hook h;
    sceClibPrintf("THUMB HOOK\n");
    if (addr == 0)
        return h;
    h.thumb_addr = addr;
    addr &= ~1;
    if (addr & 2) {
        uint16_t nop = 0xbf00;
        kuKernelCpuUnrestrictedMemcpy((void *)addr, &nop, sizeof(nop));
        addr += 2;
        sceClibPrintf("THUMB UNALIGNED\n");
    }

    h.addr = addr;
    h.patch_instr[0] = 0xf000f8df; // LDR PC, [PC]
    h.patch_instr[1] = dst;
    kuKernelCpuUnrestrictedMemcpy(&h.orig_instr, (void *)addr, sizeof(h.orig_instr));
    kuKernelCpuUnrestrictedMemcpy((void *)addr, h.patch_instr, sizeof(h.patch_instr));

    return h;
}

so_hook hook_arm(uintptr_t addr, uintptr_t dst) {
    so_hook h;
    sceClibPrintf("ARM HOOK\n");
    if (addr == 0)
        return h;
    uint32_t hook[2];
    h.thumb_addr = 0;
    h.addr = addr;
    h.patch_instr[0] = 0xe51ff004; // LDR PC, [PC, #-0x4]
    h.patch_instr[1] = dst;
    kuKernelCpuUnrestrictedMemcpy(&h.orig_instr, (void *)addr, sizeof(h.orig_instr));
    kuKernelCpuUnrestrictedMemcpy((void *)addr, h.patch_instr, sizeof(h.patch_instr));

    return h;
}

so_hook hook_addr(uintptr_t addr, uintptr_t dst) {
    if (addr == 0) {
        so_hook h;
        return h;
    }

    if (addr & 1)
        return hook_thumb(addr, dst);
    else
        return hook_arm(addr, dst);
}

void so_flush_caches(so_module *mod) {
    kuKernelFlushCaches((void *)mod->text_base, mod->text_size);
}

int _so_load(so_module *mod, SceUID so_blockid, void *so_data, uintptr_t load_addr) {
    int res = 0;
    uintptr_t data_addr = 0;

    if (memcmp(so_data, ELFMAG, SELFMAG) != 0) {
        res = -1;
        goto err_free_so;
    }

    mod->ehdr = (Elf32_Ehdr *)so_data;
    mod->phdr = (Elf32_Phdr *)((uintptr_t)so_data + mod->ehdr->e_phoff);
    mod->shdr = (Elf32_Shdr *)((uintptr_t)so_data + mod->ehdr->e_shoff);

    mod->shstr = (char *)((uintptr_t)so_data + mod->shdr[mod->ehdr->e_shstrndx].sh_offset);

    /* The module is mapped as ONE read/execute image spanning vaddr 0 through
     * the end of the executable PT_LOAD (this absorbs any read-only segment that
     * precedes .text — modern NDK libc++_shared.so puts an R-- segment first,
     * which the old "the first PT_LOAD is .text" assumption could not handle:
     * it fell into the data branch, hit data_addr==0, and bailed leaving
     * text_base==0). RW segments that follow become separate USER_RW blocks.
     * text_base therefore always == load_addr == where vaddr 0 is mapped, so the
     * section-address math (text_base + sh_addr) stays correct for every layout. */
    {
        uintptr_t rx_end_vaddr = 0;
        uint32_t  rx_align = 0x1000;
        for (int i = 0; i < mod->ehdr->e_phnum; i++) {
            if (mod->phdr[i].p_type == PT_LOAD && (mod->phdr[i].p_flags & PF_X) == PF_X) {
                rx_end_vaddr = mod->phdr[i].p_vaddr + mod->phdr[i].p_memsz;
                if (mod->phdr[i].p_align) rx_align = mod->phdr[i].p_align;
            }
        }
        if (rx_end_vaddr == 0) { res = -1; goto err_free_so; }

        // Patch/trampoline arena immediately below the image.
        mod->patch_size = ALIGN_MEM(PATCH_SZ, rx_align);
        SceKernelAllocMemBlockKernelOpt opt;
        memset(&opt, 0, sizeof(opt));
        opt.size = sizeof(opt);
        opt.attr = 0x1;
        opt.field_C = (SceUInt32)load_addr - mod->patch_size;
        res = mod->patch_blockid = kuKernelAllocMemBlock("rx_block", SCE_KERNEL_MEMBLOCK_TYPE_USER_RX, mod->patch_size, &opt);
        telemetry_log("SOALLOC", "patch bytes=%u result=0x%08x", (unsigned)mod->patch_size, (unsigned)res);
        if (res < 0) goto err_free_so;
        sceKernelGetMemBlockBase(mod->patch_blockid, (void **)&mod->patch_base);
        mod->patch_head = mod->patch_base;

        // One RX image for vaddr [0, rx_end).
        size_t rx_size = ALIGN_MEM(rx_end_vaddr, rx_align);
        void *prog_data = NULL;
        memset(&opt, 0, sizeof(opt));
        opt.size = sizeof(opt);
        opt.attr = 0x1;
        opt.field_C = (SceUInt32)load_addr;
        res = mod->text_blockid = kuKernelAllocMemBlock("rx_block", SCE_KERNEL_MEMBLOCK_TYPE_USER_RX, rx_size, &opt);
        telemetry_log("SOALLOC", "text bytes=%u result=0x%08x", (unsigned)rx_size, (unsigned)res);
        if (res < 0) goto err_free_so;
        sceKernelGetMemBlockBase(mod->text_blockid, &prog_data);

        mod->text_base = (uintptr_t)prog_data;
        mod->text_size = rx_size;
        mod->cave_base = mod->cave_head = ALIGN_MEM((uintptr_t)prog_data + rx_end_vaddr, 0x4);
        mod->cave_size = ((uintptr_t)prog_data + rx_size) - mod->cave_base;
        data_addr = (uintptr_t)prog_data + rx_size;
        sceClibPrintf("module image: base=0x%08X rx_size=0x%X cave=0x%X\n",
                      (unsigned)mod->text_base, (unsigned)rx_size, (unsigned)mod->cave_size);

        // Copy every PT_LOAD in the RX region (leading RO + the executable seg).
        for (int i = 0; i < mod->ehdr->e_phnum; i++) {
            if (mod->phdr[i].p_type != PT_LOAD) continue;
            if (mod->phdr[i].p_vaddr >= rx_end_vaddr) continue;
            uintptr_t dst = mod->text_base + mod->phdr[i].p_vaddr;
            if (mod->phdr[i].p_memsz > mod->phdr[i].p_filesz) {
                size_t bss = mod->phdr[i].p_memsz - mod->phdr[i].p_filesz;
                char *zero = calloc(1, bss);
                kuKernelCpuUnrestrictedMemcpy((void *)(dst + mod->phdr[i].p_filesz), zero, bss);
                free(zero);
            }
            kuKernelCpuUnrestrictedMemcpy((void *)dst, (void *)((uintptr_t)so_data + mod->phdr[i].p_offset), mod->phdr[i].p_filesz);
        }

        // RW segments after the RX region -> separate USER_RW blocks.
        for (int i = 0; i < mod->ehdr->e_phnum; i++) {
            if (mod->phdr[i].p_type != PT_LOAD) continue;
            if (mod->phdr[i].p_vaddr < rx_end_vaddr) continue;
            if (mod->n_data >= MAX_DATA_SEG) { res = -1; goto err_free_data; }

            uint32_t align = mod->phdr[i].p_align ? mod->phdr[i].p_align : 0x1000;
            size_t prog_size = ALIGN_MEM(mod->phdr[i].p_memsz + mod->phdr[i].p_vaddr - (data_addr - mod->text_base), align);

            memset(&opt, 0, sizeof(opt));
            opt.size = sizeof(opt);
            opt.attr = 0x1;
            opt.field_C = (SceUInt32)data_addr;
            void *seg_data = NULL;
            res = mod->data_blockid[mod->n_data] = kuKernelAllocMemBlock("rw_block", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, prog_size, &opt);
            telemetry_log("SOALLOC", "data[%d] bytes=%u result=0x%08x", mod->n_data, (unsigned)prog_size, (unsigned)res);
            if (res < 0) goto err_free_text;
            sceKernelGetMemBlockBase(mod->data_blockid[mod->n_data], &seg_data);
            data_addr = (uintptr_t)seg_data + prog_size;

            uintptr_t dst = mod->text_base + mod->phdr[i].p_vaddr;
            if (mod->phdr[i].p_memsz > mod->phdr[i].p_filesz) {
                size_t bss = mod->phdr[i].p_memsz - mod->phdr[i].p_filesz;
                char *zero = calloc(1, bss);
                kuKernelCpuUnrestrictedMemcpy((void *)(dst + mod->phdr[i].p_filesz), zero, bss);
                free(zero);
            }
            kuKernelCpuUnrestrictedMemcpy((void *)dst, (void *)((uintptr_t)so_data + mod->phdr[i].p_offset), mod->phdr[i].p_filesz);

            mod->data_base[mod->n_data] = dst;
            mod->data_size[mod->n_data] = mod->phdr[i].p_memsz;
            mod->n_data++;
        }
    }

    for (int i = 0; i < mod->ehdr->e_shnum; i++) {
        char *sh_name = mod->shstr + mod->shdr[i].sh_name;
        uintptr_t sh_addr = mod->text_base + mod->shdr[i].sh_addr;
        size_t sh_size = mod->shdr[i].sh_size;
        if (strcmp(sh_name, ".dynamic") == 0) {
            mod->dynamic = (Elf32_Dyn *)sh_addr;
            mod->num_dynamic = sh_size / sizeof(Elf32_Dyn);
        } else if (strcmp(sh_name, ".dynstr") == 0) {
            mod->dynstr = (char *)sh_addr;
        } else if (strcmp(sh_name, ".dynsym") == 0) {
            mod->dynsym = (Elf32_Sym *)sh_addr;
            mod->num_dynsym = sh_size / sizeof(Elf32_Sym);
        } else if (strcmp(sh_name, ".rel.dyn") == 0) {
            mod->reldyn = (Elf32_Rel *)sh_addr;
            mod->num_reldyn = sh_size / sizeof(Elf32_Rel);
        } else if (strcmp(sh_name, ".rel.plt") == 0) {
            mod->relplt = (Elf32_Rel *)sh_addr;
            mod->num_relplt = sh_size / sizeof(Elf32_Rel);
        } else if (strcmp(sh_name, ".init_array") == 0) {
            mod->init_array = (void *)sh_addr;
            mod->num_init_array = sh_size / sizeof(void *);
        } else if (strcmp(sh_name, ".hash") == 0) {
            mod->hash = (void *)sh_addr;
        }
    }

    if (mod->dynamic == NULL ||
        mod->dynstr == NULL ||
        mod->dynsym == NULL ||
        mod->reldyn == NULL ||
        mod->relplt == NULL) {
        res = -2;
        goto err_free_data;
    }

    for (int i = 0; i < mod->num_dynamic; i++) {
        switch (mod->dynamic[i].d_tag) {
            case DT_SONAME:
                mod->soname = mod->dynstr + mod->dynamic[i].d_un.d_ptr;
                break;
            default:
                break;
        }
    }

    sceKernelFreeMemBlock(so_blockid);

    if (!head && !tail) {
        head = mod;
        tail = mod;
    } else {
        tail->next = mod;
        tail = mod;
    }

    return 0;

    err_free_data:
    err_free_text:
    err_free_so:
    for (int i = 0; i < mod->n_data; i++)
        sceKernelFreeMemBlock(mod->data_blockid[i]);
    if (mod->text_blockid > 0) sceKernelFreeMemBlock(mod->text_blockid);
    if (mod->patch_blockid > 0) sceKernelFreeMemBlock(mod->patch_blockid);
    sceKernelFreeMemBlock(so_blockid);

    return res;
}

int so_mem_load(so_module *mod, void *buffer, size_t so_size, uintptr_t load_addr) {
    SceUID so_blockid;
    void *so_data;

    memset(mod, 0, sizeof(so_module));

    so_blockid = sceKernelAllocMemBlock("so block", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, (so_size + 0xfff) & ~0xfff, NULL);
    if (so_blockid < 0)
        return so_blockid;

    sceKernelGetMemBlockBase(so_blockid, &so_data);
    sceClibMemcpy(so_data, buffer, so_size);

    return _so_load(mod, so_blockid, so_data, load_addr);
}

int so_file_load(so_module *mod, const char *filename, uintptr_t load_addr) {
    SceUID so_blockid;
    void *so_data;

    memset(mod, 0, sizeof(so_module));

    SceUID fd = sceIoOpen(filename, SCE_O_RDONLY, 0);
    if (fd < 0)
        return fd;

    SceOff file_size = sceIoLseek(fd, 0, SCE_SEEK_END);
    if (file_size < (SceOff)sizeof(Elf32_Ehdr) || file_size > 0x7ffff000 ||
        sceIoLseek(fd, 0, SCE_SEEK_SET) < 0) {
        telemetry_log("SOFILE", "invalid size or seek for %s", filename);
        sceIoClose(fd);
        return -1;
    }
    size_t so_size = (size_t)file_size;

    so_blockid = sceKernelAllocMemBlock("so block", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, (so_size + 0xfff) & ~0xfff, NULL);
    telemetry_log("SOALLOC", "file staging bytes=%u result=0x%08x", (unsigned)((so_size + 0xfff) & ~0xfff), (unsigned)so_blockid);
    if (so_blockid < 0) {
        sceIoClose(fd);
        return so_blockid;
    }

    int rc = sceKernelGetMemBlockBase(so_blockid, &so_data);
    if (rc < 0) {
        sceIoClose(fd);
        sceKernelFreeMemBlock(so_blockid);
        return rc;
    }

    size_t done = 0;
    while (done < so_size) {
        int got = sceIoRead(fd, (char *)so_data + done, so_size - done);
        if (got <= 0) {
            telemetry_log("SOFILE", "read failed at %u/%u bytes result=%d", (unsigned)done, (unsigned)so_size, got);
            sceIoClose(fd);
            sceKernelFreeMemBlock(so_blockid);
            return got < 0 ? got : -1;
        }
        done += (size_t)got;
    }
    sceIoClose(fd);

    return _so_load(mod, so_blockid, so_data, load_addr);
}

int so_relocate(so_module *mod) {
    uintptr_t val;
    for (int i = 0; i < mod->num_reldyn + mod->num_relplt; i++) {
        Elf32_Rel *rel = i < mod->num_reldyn ? &mod->reldyn[i] : &mod->relplt[i - mod->num_reldyn];
        Elf32_Sym *sym = &mod->dynsym[ELF32_R_SYM(rel->r_info)];
        uintptr_t *ptr = (uintptr_t *)(mod->text_base + rel->r_offset);

        int type = ELF32_R_TYPE(rel->r_info);
        switch (type) {
            case R_ARM_ABS32:
                if (sym->st_shndx != SHN_UNDEF) {
                    val = *ptr + mod->text_base + sym->st_value;
                    kuKernelCpuUnrestrictedMemcpy(ptr, &val, sizeof(uintptr_t));
                }
                break;
            case R_ARM_RELATIVE:
                val = *ptr + mod->text_base;
                kuKernelCpuUnrestrictedMemcpy(ptr, &val, sizeof(uintptr_t));
                break;
            case R_ARM_GLOB_DAT:
            case R_ARM_JUMP_SLOT:
            {
                if (sym->st_shndx != SHN_UNDEF) {
                    val = mod->text_base + sym->st_value;
                    kuKernelCpuUnrestrictedMemcpy(ptr, &val, sizeof(uintptr_t));
                }
                break;
            }
            default:
                fatal_error("Error unknown relocation type %x\n", type);
                break;
        }
    }

    return 0;
}

uintptr_t so_resolve_link(so_module *mod, const char *symbol) {
    for (int i = 0; i < mod->num_dynamic; i++) {
        switch (mod->dynamic[i].d_tag) {
            case DT_NEEDED:
            {
                so_module *curr = head;
                while (curr) {
                    if (curr != mod && strcmp(curr->soname, mod->dynstr + mod->dynamic[i].d_un.d_ptr) == 0) {
                        uintptr_t link = so_symbol(curr, symbol);
                        if (link)
                            return link;
                    }
                    curr = curr->next;
                }

                break;
            }
            default:
                break;
        }
    }

    return 0;
}

void reloc_err(uintptr_t got0)
{
    // Find to which module this missing symbol belongs
    int found = 0;
    so_module *curr = head;
    while (curr && !found) {
        for (int i = 0; i < curr->n_data; i++)
            if ((got0 >= curr->data_base[i]) && (got0 <= (uintptr_t)(curr->data_base[i] + curr->data_size)))
                found = 1;

        if (!found)
            curr = curr->next;
    }

    if (curr) {
        // Attempt to find symbol name and then display error
        for (int i = 0; i < curr->num_reldyn + curr->num_relplt; i++) {
            Elf32_Rel *rel = i < curr->num_reldyn ? &curr->reldyn[i] : &curr->relplt[i - curr->num_reldyn];
            Elf32_Sym *sym = &curr->dynsym[ELF32_R_SYM(rel->r_info)];
            uintptr_t *ptr = (uintptr_t *)(curr->text_base + rel->r_offset);

            int type = ELF32_R_TYPE(rel->r_info);
            switch (type) {
                case R_ARM_JUMP_SLOT:
                {
                    if (got0 == (uintptr_t)ptr) {
                        fatal_error("Unknown symbol \"%s\" (%p).\n", curr->dynstr + sym->st_name, (void*)got0);
                    }
                    break;
                }
            }
        }
    }

    // Ooops, this shouldn't have happened.
    fatal_error("Unknown symbol \"???\" (%p).\n", (void*)got0);
}

__attribute__((naked)) void plt0_stub()
{
    register uintptr_t got0 asm("r12");
    reloc_err(got0);
}

int so_resolve(so_module *mod, so_default_dynlib *default_dynlib, int size_default_dynlib, int default_dynlib_only) {
    uintptr_t val;
    for (int i = 0; i < mod->num_reldyn + mod->num_relplt; i++) {
        Elf32_Rel *rel = i < mod->num_reldyn ? &mod->reldyn[i] : &mod->relplt[i - mod->num_reldyn];
        Elf32_Sym *sym = &mod->dynsym[ELF32_R_SYM(rel->r_info)];
        uintptr_t *ptr = (uintptr_t *)(mod->text_base + rel->r_offset);

        int type = ELF32_R_TYPE(rel->r_info);
        switch (type) {
            case R_ARM_ABS32:
            case R_ARM_GLOB_DAT:
            case R_ARM_JUMP_SLOT:
            {
                if (sym->st_shndx == SHN_UNDEF) {
                    int resolved = 0;
                    if (!default_dynlib_only) {
                        uintptr_t link = so_resolve_link(mod, mod->dynstr + sym->st_name);
                        if (link) {
                            sceClibPrintf("Resolved from dependencies: %s\n", mod->dynstr + sym->st_name);
                            if (type == R_ARM_ABS32) {
                                val = *ptr + link;
                                kuKernelCpuUnrestrictedMemcpy(ptr, &val, sizeof(uintptr_t));
                            } else {
                                val = link;
                                kuKernelCpuUnrestrictedMemcpy(ptr, &val, sizeof(uintptr_t));
                            }
                            resolved = 1;
                        }
                    }

                    for (int j = 0; j < size_default_dynlib / sizeof(so_default_dynlib); j++) {
                        if (strcmp(mod->dynstr + sym->st_name, default_dynlib[j].symbol) == 0) {
                            val = default_dynlib[j].func;
                            kuKernelCpuUnrestrictedMemcpy(ptr, &val, sizeof(uintptr_t));
                            resolved = 1;
                            break;
                        }
                    }

                    if (!resolved) {
                        /* Bring-up: don't hard-crash on a missing symbol. Point
                         * unresolved JUMP_SLOTs at __ret0 (call returns 0) and
                         * LOG the name, so the loader survives and loader.log
                         * lists exactly what was missing instead of dying on the
                         * first call to it. (Was: *ptr = &plt0_stub -> fatal
                         * "Unknown symbol ???".) */
                        sceClibPrintf("Unresolved import (dummied->ret0): %s\n", mod->dynstr + sym->st_name);
                        if (type == R_ARM_JUMP_SLOT) {
                            *ptr = (uintptr_t)&__ret0;
                        }
                    }
                }

                break;
            }
            default:
                break;
        }
    }

    return 0;
}

int __ret0() {
    return 0;
}

int so_resolve_with_dummy(so_module *mod, so_default_dynlib *default_dynlib, int size_default_dynlib, int default_dynlib_only) {
    for (int i = 0; i < mod->num_reldyn + mod->num_relplt; i++) {
        Elf32_Rel *rel = i < mod->num_reldyn ? &mod->reldyn[i] : &mod->relplt[i - mod->num_reldyn];
        Elf32_Sym *sym = &mod->dynsym[ELF32_R_SYM(rel->r_info)];
        uintptr_t *ptr = (uintptr_t *)(mod->text_base + rel->r_offset);

        int type = ELF32_R_TYPE(rel->r_info);
        switch (type) {
            case R_ARM_ABS32:
            case R_ARM_GLOB_DAT:
            case R_ARM_JUMP_SLOT:
            {
                if (sym->st_shndx == SHN_UNDEF) {
                    for (int j = 0; j < size_default_dynlib / sizeof(so_default_dynlib); j++) {
                        if (strcmp(mod->dynstr + sym->st_name, default_dynlib[j].symbol) == 0) {
                            *ptr = (uintptr_t) &__ret0;
                            break;
                        }
                    }
                }

                break;
            }
            default:
                break;
        }
    }

    return 0;
}

void so_initialize(so_module *mod) {
    for (int i = 0; i < mod->num_init_array; i++) {
        if (mod->init_array[i] && (int)mod->init_array[i] != -1)
            mod->init_array[i]();
    }
}

uint32_t so_hash(const uint8_t *name) {
    uint64_t h = 0, g;
    while (*name) {
        h = (h << 4) + *name++;
        if ((g = (h & 0xf0000000)) != 0)
            h ^= g >> 24;
        h &= 0x0fffffff;
    }
    return h;
}

static int so_symbol_index(so_module *mod, const char *symbol)
{
    if (mod->hash) {
        uint32_t hash = so_hash((const uint8_t *)symbol);
        uint32_t nbucket = mod->hash[0];
        uint32_t *bucket = &mod->hash[2];
        uint32_t *chain = &bucket[nbucket];
        for (int i = bucket[hash % nbucket]; i; i = chain[i]) {
            if (mod->dynsym[i].st_shndx == SHN_UNDEF)
                continue;
            if (mod->dynsym[i].st_info != SHN_UNDEF && strcmp(mod->dynstr + mod->dynsym[i].st_name, symbol) == 0)
                return i;
        }
    }

    for (int i = 0; i < mod->num_dynsym; i++) {
        if (mod->dynsym[i].st_shndx == SHN_UNDEF)
            continue;
        if (mod->dynsym[i].st_info != SHN_UNDEF && strcmp(mod->dynstr + mod->dynsym[i].st_name, symbol) == 0)
            return i;
    }

    return -1;
}

/*
 * alloc_arena: allocates space on either patch or cave arenas,
 * range: maximum range from allocation to dst (ignored if NULL)
 * dst: destination address
*/
uintptr_t so_alloc_arena(so_module *so, uintptr_t range, uintptr_t dst, size_t sz) {
    // Is address in range?
#define inrange(lsr, gtr, range) \
		(((uintptr_t)(range) == (uintptr_t)NULL) || ((uintptr_t)(range) >= ((uintptr_t)(gtr) - (uintptr_t)(lsr))))
    // Space left on block
#define blkavail(type) (so->type##_size - (so->type##_head - so->type##_base))

    // keep allocations 4-byte aligned for simplicity
    sz = ALIGN_MEM(sz, 4);

    if (sz <= (blkavail(patch)) && inrange(so->patch_base, dst, range)) {
        so->patch_head += sz;
        return (so->patch_head - sz);
    } else if (sz <= (blkavail(cave)) && inrange(dst, so->cave_base, range)) {
        so->cave_head += sz;
        return (so->cave_head - sz);
    }

    return (uintptr_t)NULL;
}

static void trampoline_ldm(so_module *mod, uint32_t *dst) {
    uint32_t trampoline[1];
    uint32_t funct[20] = {0xFAFAFAFA};
    uint32_t *ptr = funct;

    int cur = 0;
    int baseReg = ((*dst) >> 16) & 0xF;
    int bitMask = (*dst) & 0xFFFF;

    uint32_t stored = (uint32_t) NULL;
    for (int i = 0; i < 16; i++) {
        if (bitMask & (1 << i)) {
            // If the register we're reading the offset from is the same as the one we're writing,
            // delay it to the very end so that the base pointer ins't clobbered
            if (baseReg == i)
                stored = LDR_OFFS(i, baseReg, cur).raw;
            else
                *ptr++ = LDR_OFFS(i, baseReg, cur).raw;
            cur += 4;
        }
    }

    // Perform the delayed load if needed
    if (stored) {
        *ptr++ = stored;
    }

    *ptr++ = (uint32_t) 0xe51ff004; // LDR PC, [PC, -0x4] ; jmp to [dst+0x4]
    *ptr++ = (uint32_t) dst+1; // .dword <...>	; [dst+0x4]

    size_t trampoline_sz =	((uintptr_t)ptr - (uintptr_t)&funct[0]);
    uintptr_t patch_addr = so_alloc_arena(mod, B_RANGE, (uintptr_t) B_OFFSET(dst), trampoline_sz);

    if (!patch_addr) {
        fatal_error("Failed to patch LDMIA at 0x%08X, unable to allocate space.\n", dst);
    }

    // Create sign extended relative address rel_addr
    trampoline[0] = B(dst, patch_addr).raw;

    kuKernelCpuUnrestrictedMemcpy((void*)patch_addr, funct, trampoline_sz);
    kuKernelCpuUnrestrictedMemcpy(dst, trampoline, sizeof(trampoline));
}

uintptr_t so_symbol(so_module *mod, const char *symbol) {
    int index = so_symbol_index(mod, symbol);
    if (index == -1)
        return (uintptr_t) NULL;

    return mod->text_base + mod->dynsym[index].st_value;
}

size_t so_symbol_size(so_module *mod, const char *symbol) {
    int index = so_symbol_index(mod, symbol);
    if (index == -1)
        return 0;

    return mod->dynsym[index].st_size;
}

void so_symbol_fix_ldmia(so_module *mod, const char *symbol) {
    // This is meant to work around crashes due to unaligned accesses (SIGBUS :/) due to certain
    // kernels not having the fault trap enabled, e.g. certain RK3326 Odroid Go Advance clone distros.
    // TODO:: Maybe enable this only with a config flag? maybe with a list of known broken functions?
    // Known to trigger on GM:S's "_Z11Shader_LoadPhjS_" - if it starts happening on other places,
    // might be worth enabling it globally.

    int idx = so_symbol_index(mod, symbol);
    if (idx == -1)
        return;

    uintptr_t st_addr = mod->text_base + mod->dynsym[idx].st_value;
    for (uintptr_t addr = st_addr; addr < st_addr + mod->dynsym[idx].st_size; addr+=4) {
        uint32_t inst = *(uint32_t*)(addr);

        //Is this an LDMIA instruction with a R0-R12 base register?
        if (((inst & 0xFFF00000) == 0xE8900000) && (((inst >> 16) & 0xF) < 13) ) {
            sceClibPrintf("Found possibly misaligned LDMIA on 0x%08X, trying to fix it... (instr: 0x%08X, to 0x%08X)\n", addr, *(uint32_t*)addr, mod->patch_head);
            trampoline_ldm(mod, (uint32_t *) addr);
        }
    }
}
