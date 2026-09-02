#include "klog.h"
#include "types.h"
#include "IOStorageFamily/io_storage.h"
#include "IOStorageFamily/ata.h"
#include "IOStorageFamily/sata/ahci.h"
#include "IOStorageFamily/nvme/nvme.h"
#include "IOStorageFamily/ssd/ssd.h"
#include "devfs/devfs.h"
#include "IOPCIFamily/pci.h"
#include "IOAudioFamily/hda.h"
#include "klibc.h"

static int g_loop_count;
static int g_disk_count;
static int g_nvme_count;
static int g_ssd_count;

int io_storage_assign(int type, int partition, char *out, u32 out_size) {
    if (!out || out_size == 0)
        return -1;

    int *count = 0;
    switch (type) {
    case IO_STOR_LOOP:
        count = &g_loop_count;
        klibc.snprintf(out, out_size, "loop%d", (*count)++);
        break;
    case IO_STOR_DISK:
        count = &g_disk_count;
        if (partition < 0)
            klibc.snprintf(out, out_size, "disk%d", (*count)++);
        else
            klibc.snprintf(out, out_size, "disk%dp%d", *count, partition);
        break;
    case IO_STOR_NVME:
        count = &g_nvme_count;
        if (partition < 0)
            klibc.snprintf(out, out_size, "nvme%dn1", (*count)++);
        else
            klibc.snprintf(out, out_size, "nvme%dn1p%d", *count, partition);
        break;
    case IO_STOR_SSD:
        count = &g_ssd_count;
        klibc.snprintf(out, out_size, "ssd%d", (*count)++);
        break;
    default:
        return -1;
    }

    klog("IOstorage", "assign -> %s", out);
    return 0;
}

void io_storage_init(){
    klog("IOstorage","io_storage_init() start");

    g_loop_count = 0;
    g_disk_count = 0;
    g_nvme_count = 0;
    g_ssd_count  = 0;

   // pci_scan(); -> kernel/main.c
    //pci_list();
    devfs_init();
    ata_init();
    ahci_init();
    nvme_init();
    ssd_init();
    hda_init();
    devfs_list();

    klog("IOstorage","io_storage_init() done");
}
