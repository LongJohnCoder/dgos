#include "dev_storage.h"

#include "mm.h"

class tmpfs_fs_t : public fs_base_t {
    FS_BASE_IMPL

    void* mount(fs_init_info_t *conn);
};

class tmpfs_factory_t : public fs_factory_t {
    fs_base_t *mount(fs_init_info_t *conn);
};

//
// Startup and shutdown


fs_base_t *tmpfs_factory_t::mount(fs_init_info_t *conn)
{
    (void)conn;
    return 0;
}

void* tmpfs_fs_t::mount(fs_init_info_t *conn)
{
    (void)conn;
    return 0;
}

void tmpfs_fs_t::unmount()
{
}

//
// Read directory entry information

int tmpfs_fs_t::getattr(fs_cpath_t path, fs_stat_t* stbuf)
{
    (void)path;
    (void)stbuf;
    return 0;
}

int tmpfs_fs_t::access(fs_cpath_t path, int mask)
{
    (void)path;
    (void)mask;
    return 0;
}

int tmpfs_fs_t::readlink(fs_cpath_t path, char* buf, size_t size)
{
    (void)path;
    (void)buf;
    (void)size;
    return 0;
}

//
// Scan directories

int tmpfs_fs_t::opendir(fs_file_info_t **fi, fs_cpath_t path)
{
    (void)path;
    (void)fi;
    return 0;
}

ssize_t tmpfs_fs_t::readdir(fs_file_info_t *fi,
                                   dirent_t *buf, off_t offset)
{
    (void)buf;
    (void)offset;
    (void)fi;
    return 0;
}

int tmpfs_fs_t::releasedir(fs_file_info_t *fi)
{
    (void)fi;
    return 0;
}


//
// Modify directories

int tmpfs_fs_t::mknod(fs_cpath_t path, fs_mode_t mode, fs_dev_t rdev)
{
    (void)path;
    (void)mode;
    (void)rdev;
    return 0;
}

int tmpfs_fs_t::mkdir(fs_cpath_t path, fs_mode_t mode)
{
    (void)path;
    (void)mode;
    return 0;
}

int tmpfs_fs_t::rmdir(fs_cpath_t path)
{
    (void)path;
    return 0;
}

int tmpfs_fs_t::symlink(fs_cpath_t to, fs_cpath_t from)
{
    (void)to;
    (void)from;
    return 0;
}

int tmpfs_fs_t::rename(fs_cpath_t from, fs_cpath_t to)
{
    (void)from;
    (void)to;
    return 0;
}

int tmpfs_fs_t::link(fs_cpath_t from, fs_cpath_t to)
{
    (void)from;
    (void)to;
    return 0;
}

int tmpfs_fs_t::unlink(fs_cpath_t path)
{
    (void)path;
    return 0;
}

//
// Modify directory entries

int tmpfs_fs_t::chmod(fs_cpath_t path, fs_mode_t mode)
{
    (void)path;
    (void)mode;
    return 0;
}

int tmpfs_fs_t::chown(fs_cpath_t path, fs_uid_t uid, fs_gid_t gid)
{
    (void)path;
    (void)uid;
    (void)gid;
    return 0;
}

int tmpfs_fs_t::truncate(fs_cpath_t path, off_t size)
{
    (void)path;
    (void)size;
    return 0;
}

int tmpfs_fs_t::utimens(fs_cpath_t path, const fs_timespec_t *ts)
{
    (void)path;
    (void)ts;
    return 0;
}


//
// Open/close files

int tmpfs_fs_t::open(fs_file_info_t **fi, fs_cpath_t path)
{
    (void)path;
    (void)fi;
    return 0;
}

int tmpfs_fs_t::release(fs_file_info_t *fi)
{
    (void)fi;
    return 0;
}


//
// Read/write files

ssize_t tmpfs_fs_t::read(fs_file_info_t *fi, char *buf,
                                size_t size, off_t offset)
{
    (void)buf;
    (void)size;
    (void)offset;
    (void)fi;
    return 0;
}

ssize_t tmpfs_fs_t::write(fs_file_info_t *fi, char const *buf,
                                 size_t size, off_t offset)
{
    (void)buf;
    (void)size;
    (void)offset;
    (void)fi;
    return 0;
}

int tmpfs_fs_t::ftruncate(fs_file_info_t *fi, off_t offset)
{
    (void)offset;
    (void)fi;
    // Fail, read only
    return -1;
}

//
// Query open files

int tmpfs_fs_t::fstat(fs_file_info_t *fi, fs_stat_t *st)
{
    (void)fi;
    (void)st;
    return 0;
}

//
// Sync files and directories and flush buffers

int tmpfs_fs_t::fsync(fs_file_info_t *fi, int isdatasync)
{
    (void)isdatasync;
    (void)fi;
    return 0;
}

int tmpfs_fs_t::fsyncdir(fs_file_info_t *fi, int isdatasync)
{
    (void)isdatasync;
    (void)fi;
    return 0;
}

int tmpfs_fs_t::flush(fs_file_info_t *fi)
{
    (void)fi;
    return 0;
}

//
// Get filesystem information

int tmpfs_fs_t::statfs(fs_statvfs_t* stbuf)
{
    (void)stbuf;
    return 0;
}

//
// lock/unlock file

int tmpfs_fs_t::lock(
        fs_file_info_t *fi, int cmd, fs_flock_t* locks)
{
    (void)fi;
    (void)cmd;
    (void)locks;
    return 0;
}

//
// Get block map

int tmpfs_fs_t::bmap(
        fs_cpath_t path, size_t blocksize, uint64_t* blockno)
{
    (void)path;
    (void)blocksize;
    (void)blockno;
    return 0;
}

//
// Read/Write/Enumerate extended attributes

int tmpfs_fs_t::setxattr(
        fs_cpath_t path,
        char const* name, char const* value,
        size_t size, int flags)
{
    (void)path;
    (void)name;
    (void)value;
    (void)size;
    (void)flags;
    return 0;
}

int tmpfs_fs_t::getxattr(
        fs_cpath_t path,
        char const* name, char* value,
        size_t size)
{
    (void)path;
    (void)name;
    (void)value;
    (void)size;
    return 0;
}

int tmpfs_fs_t::listxattr(
        fs_cpath_t path,
        char const* list, size_t size)
{
    (void)path;
    (void)list;
    (void)size;
    return 0;
}

//
// ioctl API

int tmpfs_fs_t::ioctl(
        fs_file_info_t *fi,
        int cmd,
        void* arg,
        unsigned int flags,
        void* data)
{
    (void)cmd;
    (void)arg;
    (void)fi;
    (void)flags;
    (void)data;
    return 0;
}

//
//

int tmpfs_fs_t::poll(fs_file_info_t *fi,
                            fs_pollhandle_t* ph,
                            unsigned* reventsp)
{
    (void)fi;
    (void)ph;
    (void)reventsp;
    return 0;
}
