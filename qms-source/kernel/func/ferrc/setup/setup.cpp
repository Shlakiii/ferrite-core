#include <func/vfs.h>
#include <io/printg.h>
#include <bin/shell.h>

void ferrc_setup()
{
    log(1, "mounting Ferrite Core system partition...");
    vfs_mkdir("/disk");
    char mount_args[] = "ata0 /disk/0";
    cmd_mount(mount_args, true);
}
