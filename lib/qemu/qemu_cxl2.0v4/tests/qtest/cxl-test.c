/*
 * QTest testcase for CXL
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "libqtest-single.h"

#define QEMU_PXB_CMD "-machine q35 -object memory-backend-file,id=cxl-mem1," \
                     "share,mem-path=%s,size=512M "                          \
                     "-device pxb-cxl,id=cxl.0,bus=pcie.0,bus_nr=52,uid=0,"  \
                     "len-window-base=1,window-base[0]=0x4c0000000,memdev[0]=cxl-mem1"
#define QEMU_RP "-device cxl-rp,id=rp0,bus=cxl.0,addr=0.0,chassis=0,slot=0"

#define QEMU_T3D "-device cxl-type3,bus=rp0,memdev=cxl-mem1,id=cxl-pmem0,size=256M"

static void cxl_basic_hb(void)
{
    qtest_start("-machine q35,cxl");
    qtest_end();
}

static void cxl_basic_pxb(void)
{
    qtest_start("-machine q35 -device pxb-cxl,bus=pcie.0,uid=0");
    qtest_end();
}

static void cxl_pxb_with_window(void)
{
    GString *cmdline;
    char template[] = "/tmp/cxl-test-XXXXXX";
    const char *tmpfs;

    tmpfs = mkdtemp(template);

    cmdline = g_string_new(NULL);
    g_string_printf(cmdline, QEMU_PXB_CMD, tmpfs);

    qtest_start(cmdline->str);
    qtest_end();

    g_string_free(cmdline, TRUE);
}

static void cxl_root_port(void)
{
    GString *cmdline;
    char template[] = "/tmp/cxl-test-XXXXXX";
    const char *tmpfs;

    tmpfs = mkdtemp(template);

    cmdline = g_string_new(NULL);
    g_string_printf(cmdline, QEMU_PXB_CMD " %s", tmpfs, QEMU_RP);

    qtest_start(cmdline->str);
    qtest_end();

    g_string_free(cmdline, TRUE);
}

static void cxl_t3d(void)
{
    GString *cmdline;
    char template[] = "/tmp/cxl-test-XXXXXX";
    const char *tmpfs;

    tmpfs = mkdtemp(template);

    cmdline = g_string_new(NULL);
    g_string_printf(cmdline, QEMU_PXB_CMD " %s %s", tmpfs, QEMU_RP, QEMU_T3D);

    qtest_start(cmdline->str);
    qtest_end();

    g_string_free(cmdline, TRUE);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    qtest_add_func("/pci/cxl/basic_hostbridge", cxl_basic_hb);
    qtest_add_func("/pci/cxl/basic_pxb", cxl_basic_pxb);
    qtest_add_func("/pci/cxl/pxb_with_window", cxl_pxb_with_window);
    qtest_add_func("/pci/cxl/root_port", cxl_root_port);
    qtest_add_func("/pci/cxl/type3_device", cxl_t3d);

    return g_test_run();
}
