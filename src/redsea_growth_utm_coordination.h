/* Experimental, read-only admission checks for borrowing the UTM EFI journal.
 * The guest does not call this yet. Recovery must run before this admission
 * check and before EFI recovery, and the caller must hold exclusive I/O. */
#pragma once
#include "redsea_growth_shared_journal.h"
#include "upgrade_preflight.h"
#include "efi_fat32_reclaim.h"
#include "efi_upgrade_owner.h"

static int rsg_utm_admit(struct zu_commit_io io, struct zu_journal *out) {
    struct zeal_upgrade_target target;
    struct zu_source_pending pending;
    struct efi_source_reader reader;
    struct efi_reclaim_cursor cursor;
    uint8_t gpt[512],expected[512],a[512],b[512];
    if (!out || io.journal != 34 || !zu_valid_io(&io) ||
        !zeal_upgrade_preflight(io.read,io.context,io.capacity,&target))return 0;
    struct zu_journal efi={io,2048,130911,ZU_JOURNAL_MAX_SLOTS,0};
    struct zu_journal legacy=efi; legacy.slots=512;
    /* The legacy shadow lies inside the large journal's record area; stale
     * large-journal data may be nonempty, but a valid legacy header means an
     * older EFI transaction remains active and must not be overwritten. */
    if (!io.read(io.context,34+2+3ull*legacy.slots,a) ||
        zu_journal_header_ok(&legacy,a)) return 0;
    if (zu_pending_read(io.read,io.context,io.capacity,target.bank_a,target.bank_b,&pending)!=0 ||
        !io.read(io.context,34,a) || !zu_empty(a) ||
        !io.read(io.context,34+2+3ull*efi.slots,b) || !zu_empty(b) ||
        !efi_reclaim_reader(io.read,io.context,target.esp_first,target.esp_sectors,&reader) ||
        efi_reclaim_find_active(&reader,&cursor)!=0) return 0;
    if (!zeal_upgrade_gap_empty(io.read,io.context)) {
        /* Existing EFI ownership must be fully published: do not let the
         * ownership helper repair a partial owner during a read-only check. */
        if (!io.read(io.context,1,gpt) || !io.read(io.context,ZU_OWNER_LBA,a) ||
            !io.read(io.context,ZU_OWNER_SHADOW_LBA,b))return 0;
        zu_owner_encode(expected,gpt);
        for(unsigned i=0;i<512;i++)if(a[i]!=expected[i] || b[i]!=expected[i])return 0;
        if(!zu_owner_prepare(&efi,0))return 0;
    }
    uint64_t first=target.legacy?ZEAL_RS_LBA_BASE:target.data_first;
    uint64_t sectors=target.legacy?ZEAL_RS_SECTS:target.bank_a-target.data_first;
    if(first>=io.capacity || sectors>io.capacity-first)return 0;
    *out=(struct zu_journal){io,first,first+sectors,ZU_JOURNAL_MAX_SLOTS,0};
    return 1;
}
