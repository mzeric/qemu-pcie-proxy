#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#if defined(__cplusplus)
#define _Static_assert static_assert
#endif

#include "libvfio-user.h"

#define CHIP_BAR_FLAG (VFU_REGION_FLAG_64_BITS | VFU_REGION_FLAG_MEM | VFU_REGION_FLAG_PREFETCH)

// --- 配置参数 ---
#define SOCKET_PATH "/tmp/vfio_user.sock"
#define VENDOR_ID   0x1234
#define DEVICE_ID 0x5678
#define BAR0_SIZE (64 * 1024)
#define BAR2_SIZE (1 * 1024 * 1024)

// --- BAR0 (MMIO) 的读写回调 ---
static ssize_t bar0_access(vfu_ctx_t *vfu_ctx, char *const buf, size_t count,
                           int64_t offset, const bool is_write) {
    if (is_write)
    {
        printf("[DAEMON] BAR0 WRITE: offset=0x%lx, size=%zu, data=", offset, count);
        for (size_t i = 0; i < count; i++)
        {
            printf("%02x ", (unsigned char)buf[i]);
        }
        printf("\n");
    }
    else
    {
        printf("[DAEMON] BAR0 READ:  offset=0x%lx, size=%zu\n", offset, count);
        // 返回一些虚拟数据，让 guest 能读到东西
        memset(buf, 0xAA, count);
    }
    return count;
}

// --- BAR2 (64-bit MMIO) 的读写回调 ---
static ssize_t bar2_access(vfu_ctx_t *vfu_ctx, char *const buf, size_t count,
                           int64_t offset, const bool is_write) {
    if (is_write)
    {
        printf("[DAEMON] BAR2 (64-bit) WRITE: offset=0x%lx, size=%zu, data=", offset, count);
        for (size_t i = 0; i < count; i++)
        {
            printf("%02x ", (unsigned char)buf[i]);
        }
        printf("\n");
    }
    else
    {
        printf("[DAEMON] BAR2 (64-bit) READ:  offset=0x%lx, size=%zu\n", offset, count);
        memset(buf, 0xCC, count);
    }
    return count;
}

int main(int argc, char *argv[])
{
    vfu_ctx_t *vfu_ctx = NULL;
    int ret;

    // 创建 vfio-user 上下文，监听在指定的 socket
    vfu_ctx = vfu_create_ctx(VFU_TRANS_SOCK, SOCKET_PATH, 0, NULL, VFU_DEV_TYPE_PCI);
    if (vfu_ctx == NULL)
    {
        perror("Failed to create vfu context");
        return 1;
    }

    // 初始化 PCI 设备配置 (Vendor/Device ID, Class, etc.)
    // VFU_PCI_TYPE_EXPRESS: 模拟一个 PCIe 设备
    ret = vfu_pci_init(vfu_ctx, VFU_PCI_TYPE_EXPRESS,
                       PCI_HEADER_TYPE_NORMAL, 0);
    if (ret < 0)
    {
        perror("Failed to initialize PCI device");
        goto out;
    }

    vfu_pci_set_id(vfu_ctx, VENDOR_ID, DEVICE_ID, 0x80, 0x00); // 0x80 = Misc device

    ret = vfu_setup_region(vfu_ctx, VFU_PCI_DEV_CFG_REGION_IDX, PCI_CFG_SPACE_EXP_SIZE,
                           NULL,
                           VFU_REGION_FLAG_RW, NULL, 0, -1, 0);
    // 将 BARs 与回调函数绑定
    ret = vfu_setup_region(vfu_ctx, VFU_PCI_DEV_BAR0_REGION_IDX, BAR0_SIZE,
                           bar0_access,
                           VFU_REGION_FLAG_RW | CHIP_BAR_FLAG, NULL, 0, -1, 0);

    ret = vfu_setup_region(vfu_ctx, VFU_PCI_DEV_BAR2_REGION_IDX, BAR2_SIZE,
                           bar2_access,
                           VFU_REGION_FLAG_RW | CHIP_BAR_FLAG, NULL, 0, -1, 0);

    if (ret < 0)
    {
        perror("Failed to setup BAR0 access");
        goto out;
    }

    // 完成设备设置并启动事件循环
    ret = vfu_realize_ctx(vfu_ctx);
    if (ret < 0)
    {
        perror("Failed to realize vfu context");
        goto out;
    }

    ret = vfu_attach_ctx(vfu_ctx);
    if (ret < 0)
    {
        perror("Failed to attach vfu context");
        goto out;
    }

    printf("Daemon is running. Waiting for QEMU to connect to %s...\n", SOCKET_PATH);
    printf("Device: Vendor=0x%x, Device=0x%x, 3 BARs\n", VENDOR_ID, DEVICE_ID);

    // 运行事件循环，处理来自 QEMU 的请求
    do
    {
        printf("waiting msg\n");
        ret = vfu_run_ctx(vfu_ctx);
        if (ret < 0 && errno != EAGAIN && errno != EINTR)
        {
            fprintf(stderr, "vfu_run_ctx failed: %s\n", strerror(errno));
        }
    } while (ret == 0);

out:
    vfu_destroy_ctx(vfu_ctx);
    return ret;
}
