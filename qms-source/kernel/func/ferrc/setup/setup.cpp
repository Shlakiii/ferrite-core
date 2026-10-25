#include <func/vfs.h>
#include <io/printg.h>
#include <bin/shell.h>

VFS_Dir dir_result;

#define EMU_A0   "/emulated/0"

void find_or_create_dir(const char* path) {
    dir_result = vfs_readdir(path);
    if (dir_result.error == VFS_ERR_NOT_FOUND) {
        log(3, "Creating directory: %s", path);
        vfs_mkdir(path);
        log(3, "Ready: %s", path);
    }else{
        log(3, "Found directory: %s", path);
    }
}

void find_or_create_file(const char* path) {
    VFS_Stat stat;
    VFS_Error stat_error = vfs_stat_path(path, &stat);
    if (stat_error == VFS_OK) {
        if (stat.type == VFS_ENT_FILE) {
            log(3, "Found file: %s", path);
        } else {
            log(1, "Cannot create file '%s': path is not a file", path);
        }
        return;
    }

    if (stat_error != VFS_ERR_NOT_FOUND) {
        log(1, "Failed to check file '%s': %s", path, vfs_strerror(stat_error));
        return;
    }

    log(3, "Creating file: %s", path);
    VFS_Error create_error = vfs_creat(path, VFS_ENT_FILE);
    if (create_error != VFS_OK) {
        log(1, "Failed to create file '%s': %s", path, vfs_strerror(create_error));
        return;
    }
    log(3, "Ready: %s", path);
}

void ferrc_setup()
{
    log(3, "Mounting Ferrite Core system partition...");
    vfs_mkdir("/emulated");
    char mount_args[] = "ata0 /emulated/0";
    cmd_mount(mount_args, true);
    log(3, "Ready ata0 : /emulated/0/");
    log(3, "Seek dirs");
    bool result = vfs_cd("/emulated/0");
    if (!result){
        log(1, "Failed to seek : /emulated/0");
    }else{
        log(3, "Switched to /emulated/0");
    }
    const char* search_dir = EMU_A0 "/ferrc";
    find_or_create_dir(search_dir);
    find_or_create_dir(EMU_A0 "/home");
    find_or_create_dir(EMU_A0 "/home/user");
    find_or_create_dir(EMU_A0 "/ferrc/lua");
    find_or_create_dir(EMU_A0 "/ferrc/.config");
    find_or_create_file(EMU_A0 "/ferrc/.config/video.conf");
    vfs_setattribute("/emulated/0/ferrc", "+sys");

    //end
    vfs_cd("/");

}
