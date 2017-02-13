
#include "qemu/osdep.h"
#include "qemu/iov.h"
#include "qemu/timer.h"
#include "qemu-common.h"
#include "qemu/bswap.h"
#include "hw/virtio/virtio.h"
#include "hw/virtio/virtio-pci.h"
#include "hw/virtio/virtio-test-pci.h"
#include "sysemu/kvm.h"
#include "exec/address-spaces.h"
#include "qapi/visitor.h"
#include "qapi-event.h"
#include "trace.h"

#include "hw/virtio/virtio-bus.h"
#include "hw/virtio/virtio-access.h"

#define TEST_PCI_DEVICE_DEBUG

#ifdef  TEST_PCI_DEVICE_DEBUG
    #define tprintf(fmt, ...) printf("## (%3d) %-20s: " fmt, __LINE__, __func__, ## __VA_ARGS__)
#else
    #define tprintf(fmt, ...)
#endif

static void virtio_test_pci_get_config(VirtIODevice *vdev, uint8_t *config_data)
{
    //VirtIOTestPCI *s = VIRTIO_TEST_PCI_PCI(vdev);
    tprintf("enter\n");
}

static void virtio_test_pci_set_config(VirtIODevice *vdev,
                                      const uint8_t *config_data)
{
    //VirtIOTestPCI *s = VIRTIO_TEST_PCI_PCI(vdev);
    tprintf("enter\n");
}

static uint64_t virtio_test_pci_get_features(VirtIODevice *vdev, uint64_t f,
                                            Error **errp)
{
    //VirtIOTestPCI *s = VIRTIO_TEST_PCI_PCI(vdev);
    tprintf("enter\n");
    return f;
}

static int virtio_test_pci_post_load_device(void *opaque, int version_id)
{
    //VirtIOTestPCI *s = VIRTIO_TEST_PCI_PCI(opaque);
    tprintf("enter\n");
    return 0;
}

static const VMStateDescription vmstate_virtio_test_pci_device = {
    .name = "virtio-test-pci-device",
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = virtio_test_pci_post_load_device,
    .fields = (VMStateField[]) {
        VMSTATE_END_OF_LIST()
    },
};

/* ring helper functions */
static inline uint16_t ring_inc(uint16_t index)
{
    return (++index % TEST_PCI_DEVICE_RING_SIZE);
}

static bool ring_full(uint16_t head, uint16_t tail)
{
    return (((head + 1) % TEST_PCI_DEVICE_RING_SIZE) == tail);
}

static bool ring_empty(uint16_t head, uint16_t tail)
{
    return (((tail + 1) % TEST_PCI_DEVICE_RING_SIZE) == head);
}

static bool ring_inc_head(uint16_t *head, uint16_t tail)
{
    if (ring_full(*head, tail))
        return false;
    *head = ring_inc(*head);
    return true;
}

static bool ring_inc_tail(uint16_t head, uint16_t *tail)
{
    if (ring_empty(head, *tail))
        return false;
    *tail = ring_inc(*tail);
    return true;
}

static uint16_t ring_count(uint16_t head, uint16_t tail)
{
    return ((((head + TEST_PCI_DEVICE_RING_SIZE) - tail) %
             TEST_PCI_DEVICE_RING_SIZE) - 1);
}

static void virtio_test_pci_device_cmd_cb(VirtIODevice *vdev, VirtQueue *vq)
{
    VirtIOTestPCI *s = VIRTIO_TEST_PCI(vdev);
    VirtQueueElement *elem;
    VirtIOTestPCICmd cmd;
    VirtIOTestPCIEvent event;
    int count = 0;

    tprintf("enter\n");

    for (;;) {
        size_t len;

        if (ring_empty(s->event_head, s->event_tail)) {
            tprintf("no events. abort!\n");
            goto error;
        } 

        elem = virtqueue_pop(vq, sizeof(VirtQueueElement));
        if (!elem) {
            goto error;
        }
   
        len = iov_size(elem->out_sg, elem->out_num);

        if (len != sizeof(cmd)) {
            tprintf("inv cmd len! abort! len %lu expect %lu\n", len, 
                sizeof(cmd));
            goto error;
        }

        memset(&cmd, 0, sizeof(cmd));

        len = iov_to_buf(elem->out_sg, elem->out_num, 0, &cmd, sizeof(cmd));

        if (len != sizeof(cmd)) {
            tprintf("inv cmd iov len! abort! len %lu expect %lu\n", len, 
                sizeof(cmd));
            goto error;
        }

        virtqueue_push(vq, elem, len);

        tprintf("cmd: seq %lu op %u\n", be64_to_cpu(cmd.seq),
            be16_to_cpu(cmd.op));

        /* prepare the completion event */
        memset(&event, 0, sizeof(event));
        event.seq = cmd.seq;
        event.op = cmd.op;
        event.status = 0; /* success */

        if (!ring_inc_tail(s->event_head, &s->event_tail)) {
            tprintf("failed to move ring tail! abort!\n");
            goto error;
        }

        /* copy the event to the completion queue */
        elem = &s->events[s->event_tail].elem;

        len = iov_from_buf(elem->in_sg, elem->in_num, 0, &event,
                           sizeof(event));
        
        if (len != sizeof(event)) {
            tprintf("inv event iov len! abort! len %lu expect %lu\n", len, 
                sizeof(event));
            goto error;
        }

        count++;
    }

    /* send interrupt to guest driver */
    virtio_notify(vdev, vq);

error: 
    tprintf("used %d ring %d\n", count,
        ring_count(s->event_head, s->event_tail));
}

static void virtio_test_pci_device_event_cb(VirtIODevice *vdev, VirtQueue *vq)
{
    VirtIOTestPCI *s = VIRTIO_TEST_PCI(vdev);
    VirtQueueElement *elem;
    VrtIOTestPciEventBuf *ev_buf;
    int count = 0;
 
    tprintf("enter\n");
    
    for (;;) {
        size_t len;
        elem = NULL;
    
        if (ring_full(s->event_head, s->event_tail)) {
            tprintf("event ring full. abort!\n");
            goto error;
        }

        elem = virtqueue_pop(vq, sizeof(VirtQueueElement));
        if (!elem) {
            goto error;
        }
   
        len = iov_size(elem->in_sg, elem->in_num);

        if (len != sizeof(VirtIOTestPCIEvent)) {
            tprintf("inv event len! abort! len %lu expect %lu\n", len, 
                sizeof(VirtIOTestPCIEvent));
            goto error;
        }
        
        virtqueue_push(vq, elem, len);
     
        ev_buf = &s->events[s->event_head]; 
        memset(ev_buf, 0, sizeof(*ev_buf));
        ev_buf->elem = *elem; 

        if (!ring_inc_head(&s->event_head, s->event_tail)) {
            tprintf("failed to move ring head! abort!\n");
            goto error;
        } 

        count++;
    }

error:
    tprintf("added %d ring %d\n", count,
        ring_count(s->event_head, s->event_tail));
}

static void virtio_test_pci_device_realize(DeviceState *dev, Error **errp)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VirtIOTestPCI *s = VIRTIO_TEST_PCI(dev);

    tprintf("enter\n");
    virtio_init(vdev, "virtio-test-pci", VIRTIO_ID_TEST_PCI,
                sizeof(struct virtio_test_pci_config));
    s->vq_cmd = virtio_add_queue(vdev, TEST_PCI_DEVICE_RING_SIZE,
                                 virtio_test_pci_device_cmd_cb); 
    s->vq_event = virtio_add_queue(vdev, TEST_PCI_DEVICE_RING_SIZE,
                                   virtio_test_pci_device_event_cb); 
    s->event_head = 0;
    s->event_tail = TEST_PCI_DEVICE_RING_SIZE - 1;
}

static void virtio_test_pci_device_unrealize(DeviceState *dev, Error **errp)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    //VirtIOTestPCI *s = VIRTIO_TEST_PCI_PCI(dev);
    tprintf("enter\n");
    virtio_cleanup(vdev);
}

static void virtio_test_pci_device_reset(VirtIODevice *vdev)
{
    //VirtIOTestPCI *s = VIRTIO_TEST_PCI_PCI(vdev);
    tprintf("enter\n");
}

static void virtio_test_pci_set_status(VirtIODevice *vdev, uint8_t status)
{
    //VirtIOTestPCI *s = VIRTIO_TEST_PCI_PCI(vdev);
    tprintf("enter 0x%x\n", status);
}

static void virtio_test_pci_instance_init(Object *obj)
{
    //VirtIOTestPCI *s = VIRTIO_TEST_PCI_PCI(obj);
    tprintf("enter\n");
}

static const VMStateDescription vmstate_virtio_test_pci = {
    .name = "virtio-test-pci",
    .minimum_version_id = 1,
    .version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_VIRTIO_DEVICE,
        VMSTATE_END_OF_LIST()
    },
};

static Property virtio_test_pci_properties[] = {
    DEFINE_PROP_END_OF_LIST(),
};

static void virtio_test_pci_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtioDeviceClass *vdc = VIRTIO_DEVICE_CLASS(klass);

    tprintf("enter\n");
    dc->props = virtio_test_pci_properties;
    dc->vmsd = &vmstate_virtio_test_pci;
    set_bit(DEVICE_CATEGORY_NETWORK, dc->categories);
    vdc->realize = virtio_test_pci_device_realize;
    vdc->unrealize = virtio_test_pci_device_unrealize;
    vdc->reset = virtio_test_pci_device_reset;
    vdc->get_config = virtio_test_pci_get_config;
    vdc->set_config = virtio_test_pci_set_config;
    vdc->get_features = virtio_test_pci_get_features;
    vdc->set_status = virtio_test_pci_set_status;
    vdc->vmsd = &vmstate_virtio_test_pci_device;
}

static const TypeInfo virtio_test_pci_info = {
    .name = TYPE_VIRTIO_TEST_PCI,
    .parent = TYPE_VIRTIO_DEVICE,
    .instance_size = sizeof(VirtIOTestPCI),
    .instance_init = virtio_test_pci_instance_init,
    .class_init = virtio_test_pci_class_init,
};

static void virtio_test_pci_register_types(void)
{
    tprintf("enter\n");
    type_register_static(&virtio_test_pci_info);
}

type_init(virtio_test_pci_register_types)
