#ifndef QEMU_VIRTIO_TEST_PCI_H
#define QEMU_VIRTIO_TEST_PCI_H

#include "standard-headers/linux/virtio_test_pci.h"
#include "hw/virtio/virtio.h"
#include "hw/pci/pci.h"

#define TYPE_VIRTIO_TEST_PCI "virtio-test-pci-device"
#define VIRTIO_TEST_PCI(obj) \
        OBJECT_CHECK(VirtIOTestPCI, (obj), TYPE_VIRTIO_TEST_PCI)

#define TEST_PCI_DEVICE_RING_SIZE   (128)

typedef struct virtio_test_pci_stat VirtIOTestPCIStat;

typedef struct virtio_test_pci_stat_modern {
} VirtIOTestPciStatModern;

typedef struct virtio_tetst_pci_event_buf {
    VirtQueueElement elem;
} VrtIOTestPciEventBuf;

typedef struct VirtIOTestPCI {
    VirtIODevice parent_obj;
    VirtQueue *vq_cmd, *vq_event;
    /* completion events ring */
    uint16_t event_head, event_tail; /* 0 <= X < TEST_PCI_DEVICE_RING_SIZE */
    VrtIOTestPciEventBuf events[TEST_PCI_DEVICE_RING_SIZE];
} VirtIOTestPCI;


/* commands/events excahanged between the guest driver and the emulated hw */

/* NOTE!! - all values are in big endian! */

typedef struct virtio_test_pci_cmd {
        /* seq number of this command (can be matched with ack cmd_seq) */
        uint64_t  seq;
        /* op of this command (select the below struct from union) */
        uint16_t  op;
        uint16_t  pad1;
        uint32_t  pad2;
        union {
            struct {
                uint64_t pad3;
            } dummy;
        } cmd;
} VirtIOTestPCICmd;

/*
 * each command has matching completion event so the event
 * seq equal the command seq and can tell the command and
 * event index in the ring.
 */
typedef struct virtio_test_pci_event {
        uint64_t  seq;
        uint16_t  op;
        uint16_t  status;
        uint32_t  pad1;
        union {
            struct {
                uint64_t pad2;
            } dummy;
        } event;
} VirtIOTestPCIEvent;


#endif
