#pragma once
#include "threadsync.h"
#include "cpu/atomic.h"
#include "utility.h"

// Meets BasicLockable requirements
class mutex {
public:
    typedef mutex_t mutex_type;

    mutex()
    {
        mutex_init(&m);
    }

    ~mutex()
    {
        mutex_destroy(&m);
    }

    mutex(mutex const& r) = delete;

    void lock()
    {
        mutex_lock(&m);
    }

    bool try_lock()
    {
        return mutex_try_lock(&m);
    }

    void unlock()
    {
        mutex_unlock(&m);
    }

    mutex_type& native_handle()
    {
        return m;
    }

private:
    mutex_t m;
};

class alignas(64) padded_mutex : public mutex {
};

// Meets SharedMutex requirements
class shared_mutex {
public:
    typedef rwlock_t mutex_type;

    shared_mutex()
    {
        rwlock_init(&m);
    }

    ~shared_mutex()
    {
        rwlock_destroy(&m);
    }

    shared_mutex(mutex_type const& r) = delete;

    void lock()
    {
        rwlock_ex_lock(&m);
    }

    bool try_lock()
    {
        return rwlock_ex_try_lock(&m);
    }

    void unlock()
    {
        rwlock_ex_unlock(&m);
    }

    void lock_shared()
    {
        rwlock_sh_lock(&m);
    }

    void try_lock_shared()
    {
        rwlock_sh_try_lock(&m);
    }

    void unlock_shared()
    {
        rwlock_sh_unlock(&m);
    }

    void upgrade_lock()
    {
        rwlock_upgrade(&m);
    }

    mutex_type& native_handle()
    {
        return m;
    }

private:
    mutex_type m;
};

class alignas(64) padded_shared_mutex : public shared_mutex {
};

class shared_spinlock {
public:
    typedef rwspinlock_t mutex_type;

    shared_spinlock()
        : m(0)
    {
    }


    void lock()
    {
        rwspinlock_ex_lock(&m);
    }

    bool try_lock()
    {
        return rwspinlock_ex_try_lock(&m);
    }

    void unlock()
    {
        rwspinlock_ex_unlock(&m);
    }

    void lock_shared()
    {
        rwspinlock_sh_lock(&m);
    }

    void try_lock_shared()
    {
        rwspinlock_sh_try_lock(&m);
    }

    void unlock_shared()
    {
        rwspinlock_sh_unlock(&m);
    }

    void upgrade_lock()
    {
        rwspinlock_upgrade(&m);
    }

    mutex_type& native_handle()
    {
        return m;
    }

private:
    rwspinlock_t m;
};

// Meets BasicLockable requirements
class spinlock {
public:
    typedef spinlock_t mutex_type;

    spinlock()
        : m(0)
    {
    }

    ~spinlock()
    {
        while (m != 0)
            pause();
        assert(m == 0);
    }

    spinlock(spinlock const& r) = delete;

    void lock()
    {
        spinlock_lock_noirq(&m);
    }

    bool try_lock()
    {
        return spinlock_try_lock_noirq(&m);
    }

    void unlock()
    {
        spinlock_unlock_noirq(&m);
    }

    spinlock_t& native_handle()
    {
        return m;
    }

private:
    spinlock_t m;
};

struct alignas(64) padded_spinlock : public spinlock {
};

class ticket_lock {
public:
    typedef ticketlock_t mutex_type;

    ticket_lock()
        : m{}
    {
    }

    void lock() {
        ticketlock_lock(&m);
    }

    bool try_lock() {
        return ticketlock_try_lock(&m);
    }

    void unlock() {
        ticketlock_unlock(&m);
    }

private:
    ticketlock_t m;
};

struct alignas(64) padded_ticket_lock : public ticket_lock {
};

struct defer_lock_t {
};

template<typename T>
class unique_lock
{
public:
    unique_lock(T& m)
        : m(&m)
        , locked(false)
    {
        lock();
    }

    unique_lock(T& lock, defer_lock_t)
        : m(&lock)
        , locked(false)
    {
    }

    ~unique_lock()
    {
        unlock();
    }

    void lock()
    {
        assert(!locked);
        m->lock();
        locked = true;
    }

    void unlock()
    {
        if (locked) {
            locked = false;
            m->unlock();
        }
    }

    void release()
    {
        locked = false;
        m = nullptr;
    }

    void swap(unique_lock& rhs)
    {
        ::swap(rhs.m, m);
        ::swap(rhs.locked, locked);
    }

    typename T::mutex_type& native_handle()
    {
        return m->native_handle();
    }

private:
    T* m;
    bool locked;
};

template<typename T>
class shared_lock
{
public:
    shared_lock(T& m)
        : m(&m)
        , locked(false)
    {
        lock();
    }

    shared_lock(T& lock, defer_lock_t)
        : m(&lock)
        , locked(false)
    {
    }

    ~shared_lock()
    {
        unlock();
    }

    void lock()
    {
        assert(!locked);
        m->lock_shared();
        locked = true;
    }

    void unlock()
    {
        if (locked) {
            locked = false;
            m->unlock_shared();
        }
    }

    typename T::mutex_type& native_handle()
    {
        return m->native_handle();
    }

    void release()
    {
        locked = false;
        m = nullptr;
    }

    void swap(unique_lock<T>& rhs)
    {
        ::swap(rhs.m, m);
        ::swap(rhs.locked, locked);
    }

private:
    T* m;
    bool locked;
};

class condition_variable
{
public:
    condition_variable()
    {
        condvar_init(&m);
    }

    ~condition_variable()
    {
        condvar_destroy(&m);
    }

    void notify_one()
    {
        condvar_wake_one(&m);
    }

    void notify_all()
    {
        condvar_wake_all(&m);
    }

    void wait(unique_lock<mutex>& lock)
    {
        condvar_wait(&m, &lock.native_handle());
    }

    void wait(unique_lock<spinlock>& lock)
    {
        condvar_wait_spinlock(&m, &lock.native_handle());
    }

private:
    condition_var_t m;
};

struct alignas(64) padded_condition_variable : public condition_variable {
};
