#include "fd.h"
#include "process.h"
#include "thread.h"
#include "fileio.h"

static int err(errno_t errno)
{
    thread_set_error(errno);
    return -1;
}

static int err(int negerr)
{
    assert(negerr < 0 && -negerr < int(errno_t::MAX_ERRNO));
    thread_set_error(errno_t(-negerr));
    return -1;
}

static int badf_err()
{
    return err(errno_t::EBADF);
}

int sys_open(char const* pathname, int flags, mode_t mode)
{
    process_t *p = fast_cur_process();
    
    int fd = p->ids.desc_alloc.alloc();
    
    int id = file_open(pathname, flags, mode);
    
    if (likely(id >= 0)) {
        p->ids.ids[fd] = id;
        return fd;
    }
    
    p->ids.desc_alloc.free(fd);
    return err(-id);
}

int sys_creat(const char *path, mode_t mode)
{
    process_t *p = fast_cur_process();
    
    int fd = p->ids.desc_alloc.alloc();
    
    int id = file_creat(path, mode);
    
    if (likely(id >= 0)) {
        p->ids.ids[fd] = id;
        return fd;
    }
    
    p->ids.desc_alloc.free(fd);
    return err(-id);
}

ssize_t sys_read(int fd, void *bufaddr, size_t count)
{
    process_t *p = fast_cur_process();
    
    int id = p->fd_to_id(fd);
    
    if (unlikely(id < 0))
        return badf_err();
    
    ssize_t sz = file_read(id, bufaddr, count);
    
    if (sz < 0)
        thread_set_error(errno_t(-sz));
    
    return sz;
}

ssize_t sys_write(int fd, void const *bufaddr, size_t count)
{
    process_t *p = fast_cur_process();
    
    int id = p->fd_to_id(fd);
    
    if (unlikely(id < 0))
        return badf_err();
    
    ssize_t sz = file_write(id, bufaddr, count);
    
    if (sz >= 0)
        return sz;
        
    return err(sz);
}

int sys_close(int fd)
{
    process_t *p = fast_cur_process();
    
    int id = p->fd_to_id(fd);
    
    if (unlikely(id < 0))
        return badf_err();
    
    int status = file_close(id);
    if (status != 0)
        return 0;

    return err(status);
}

ssize_t sys_pread64(int fd, void *bufaddr, size_t count, off_t ofs)
{
    process_t *p = fast_cur_process();
    
    int id = p->fd_to_id(fd);
    
    if (unlikely(id < 0))
        return badf_err();
    
    int sz = file_pread(id, bufaddr, count, ofs);
    if (sz >= 0)
        return sz;

    return err(sz);
}

ssize_t sys_pwrite64(int fd, void const *bufaddr, 
                     size_t count, off_t ofs)
{
    process_t *p = fast_cur_process();
    
    int id = p->fd_to_id(fd);
    
    if (unlikely(id < 0))
        return badf_err();
    
    int sz = file_pwrite(id, bufaddr, count, ofs);
    if (sz >= 0)
        return sz;

    return err(sz);
}

off_t sys_lseek(int fd, off_t ofs, int whence)
{
    process_t *p = fast_cur_process();
    
    int id = p->fd_to_id(fd);
    
    if (unlikely(id < 0))
        return badf_err();
    
    int pos = file_seek(id, ofs, whence);
    if (pos >= 0)
        return pos;

    return err(pos);
}

int sys_fsync(int fd)
{
    process_t *p = fast_cur_process();
    
    int id = p->fd_to_id(fd);
    
    if (unlikely(id < 0))
        return badf_err();
    
    int status = file_fsync(id);
    if (status >= 0)
        return status;

    return err(status);
}

int sys_fdatasync(int fd)
{
    process_t *p = fast_cur_process();
    
    int id = p->fd_to_id(fd);
    
    if (unlikely(id < 0))
        return badf_err();
    
    int status = file_fdatasync(id);
    if (status >= 0)
        return status;

    return err(status);
}

int sys_ftruncate(int fd, off_t size)
{
    process_t *p = fast_cur_process();
    
    int id = p->fd_to_id(fd);
    
    if (unlikely(id < 0))
        return badf_err();
    
    int status = file_ftruncate(id, size);
    if (status >= 0)
        return status;

    return err(status);
}

int sys_dup(int oldfd)
{
    process_t *p = fast_cur_process();
    
    int id = p->fd_to_id(oldfd);
    
    int newfd = p->ids.desc_alloc.alloc();
    
    if (file_ref_filetab(id)) {
        p->ids.ids[newfd] = id;
        return newfd;
    }
    
    p->ids.desc_alloc.free(newfd);
    
    return badf_err();
}

int sys_dup3(int oldfd, int newfd, int flags)
{
    process_t *p = fast_cur_process();
    
    int id = p->fd_to_id(oldfd);
    
    int newid = p->fd_to_id(newfd);
    
    if (newid >= 0)
        file_close(newid);
    else if (!p->ids.desc_alloc.take(newfd))
        return err(errno_t::EMFILE);
    
    
    if (likely(file_ref_filetab(id))) {
        p->ids.ids[newfd].set(id, flags);
        return newfd;
    }
    
    p->ids.desc_alloc.free(newfd);
    
    return badf_err();
}

int sys_dup2(int oldfd, int newfd)
{
    return sys_dup3(oldfd, newfd, 0);
}

int sys_rename(const char *old_path, const char *new_path)
{
    int status = file_rename(old_path, new_path);
    if (likely(status >= 0))
        return status;

    return err(status);    
}

int sys_mkdir(const char *path, mode_t mode)
{
    int status = file_mkdir(path, mode);
    if (status >= 0)
        return status;

    return err(status);
}

int sys_rmdir(const char *path)
{
    int status = file_rmdir(path);
    if (likely(status >= 0))
        return status;

    return err(status);
}

int sys_unlink(const char *path)
{
    int status = file_unlink(path);
    if (likely(status >= 0))
        return status;

    return err(status);
}
