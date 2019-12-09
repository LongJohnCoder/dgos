#include "contig_alloc.h"
#include "printk.h"

#include "inttypes.h"
#include "cpu/atomic.h"

#define DEBUG_LINEAR_SANITY     0
#define DEBUG_ADDR_ALLOC        0
#define DEBUG_ADDR_EARLY        0

//
// Linear address allocator

static void dump_addr_tree(contiguous_allocator_t::tree_t const *tree,
                           char const *key_name, char const *val_name)
{
    for (auto const& item : *tree) {
        dbgout << key_name << '=' << hex << item.first <<
                  ", " << val_name << '=' << hex << item.second << '\n';
    }
}

void contiguous_allocator_t::set_early_base(linaddr_t *base_ptr)
{
    linear_base_ptr = base_ptr;
}

uintptr_t contiguous_allocator_t::early_init(size_t size, char const *name)
{
    this->name = name;

//    linaddr_t initial_addr = *linear_base_ptr;

//    // Account for space taken creating trees

//    size_t size_adj = *linear_base_ptr - initial_addr;
//    size -= size_adj;

    uintptr_t aligned_base;

    for (;;) {
        linaddr_t prev_base = *linear_base_ptr;

        // and align to 1GB boundary to make pointers more readable
        aligned_base = prev_base;


        // Page align
        aligned_base = (aligned_base + PAGE_SIZE - 1) & -PAGE_SIZE;

        // 1GB align
//        aligned_base += (1 << 30) - 1;
//        aligned_base &= -(1 << 30);

        std::pair<tree_t::iterator, bool> ins_by_size =
                free_addr_by_size.insert({size, aligned_base});

        free_addr_by_addr.share_allocator(free_addr_by_size);

        std::pair<tree_t::iterator, bool> ins_by_addr =
            free_addr_by_addr.insert({aligned_base, size});

        if (likely(*linear_base_ptr == prev_base))
            break;

        // Whoa, memory allocation disturbed that pair, undo
        free_addr_by_size.erase(ins_by_size.first);
        free_addr_by_addr.erase(ins_by_addr.first);

        // retry...
    }

    validate();

    return aligned_base;
}

void contiguous_allocator_t::init(
        linaddr_t addr, size_t size, char const *name)
{
    this->name = name;

//    free_addr_by_addr.init(contiguous_allocator_cmp_key, nullptr);
//    free_addr_by_size.init(contiguous_allocator_cmp_both, nullptr);

    if (size) {
        free_addr_by_size.insert({size, addr});
        free_addr_by_addr.share_allocator(free_addr_by_size);
        free_addr_by_addr.insert({addr, size});
    }

    validate();

    //dump("After init\n");
    ready = true;
}

uintptr_t contiguous_allocator_t::alloc_linear(size_t size)
{
    linaddr_t addr;

    if (likely(ready)) {
        scoped_lock lock(free_addr_lock);

#if DEBUG_ADDR_ALLOC
        dump("Before Alloc %#" PRIx64 "\n", size);
#endif

        // Find the lowest address item that is big enough
        tree_t::iterator place = free_addr_by_size.lower_bound({size, 0});

        if (unlikely(place == free_addr_by_size.end()))
            return 0;

        // Copy the sufficiently-sized block description
        tree_t::value_type by_size = *place;

        assert(by_size.first >= size);

        // Erase the pair of entries
        free_addr_by_size.erase(place);

        bool did_del = free_addr_by_addr.erase({by_size.second,
                                                by_size.first}) > 0;
        assert(did_del);

        if (by_size.first > size) {
            // Insert remainder by size
            free_addr_by_size.insert({by_size.first - size,
                                      by_size.second + size});

            // Insert remainder by address
            free_addr_by_addr.insert({by_size.second + size,
                                      by_size.first - size});
        }

        addr = by_size.second;

        validate();

#if DEBUG_ADDR_ALLOC
        dump("after alloc_linear sz=%#zx addr=%#zx\n", size, addr);
#endif

#if DEBUG_LINEAR_SANITY
        sanity_check_by_size(free_addr_by_size);
        sanity_check_by_addr(free_addr_by_addr);
#endif

#if DEBUG_ADDR_ALLOC
        printdbg("Took address space @ %#" PRIx64
                 ", size=%#" PRIx64 "\n", addr, size);
#endif
    } else {
        addr = atomic_xadd(linear_base_ptr, size);

#if DEBUG_ADDR_EARLY
        printdbg("Took early address space @ %#" PRIx64
                 ", size=%#" PRIx64 ""
                 ", new linear_base=%#" PRIx64 "\n",
                 addr, size, *linear_base_ptr);
#endif
    }

    return addr;
}

/*
 * 9 scenarios                                                         *
 *                                                                     *
 *   A-----B  X: The range we are taking                               *
 *   C-----D  Y: The existing range                                    *
 *
 * Query finds [ ranges.lower_bound(A), ranges.upper_bound(B) )
 *
 * For each one use this table to determine outcome against the first
 *
 *  +-------+-------+-------------+-------+--------------------------------+
 *  | A<=>C | B<=>D |             | Count |                                |
 *  +-------+-------+-------------+-------+--------------------------------+
 *  |       |       |             |       |                                |
 *  |  -1   |  -1   | <--->       |  +1   | No overlap, do nothing, done   |
 *  |       |       |       <---> |       |                                |
 *  |       |       |             |       |                                |
 *  |  -1   |   0   | <---------> |   0   | Replace obstacle, done         |
 *  |       |       |       <xxx> |       |                                |
 *  |       |       |             |       |                                |
 *  |  -1   |   1   | <---------> |   0   | Replace obstacle, continue     |
 *  |       |       |    <xxx>    |       |                                |
 *  |       |       |             |       |                                |
 *  |   0   |  -1   | <--->       |   1   | Clip obstacle start, done      |
 *  |       |       | <xxx------> |       |                                |
 *  |       |       |             |       |                                |
 *  |   0   |   0   | <--->       |   0   | Replace obstacle, done         |
 *  |       |       | <xxx>       |       |                                |
 *  |       |       |             |       |                                |
 *  |   0   |   1   | <---------> |   0   | Replace obstacle, continue     |
 *  |       |       | <xxx>       |       |                                |
 *  |       |       |             |       |                                |
 *  |   1   |  -1   |   <--->     |   2   | Duplicate obstacle, clip end   |
 *  |       |       | <-->x<-->   |       | of original, clip start of     |
 *  |       |       |             |       | duplicate, done                |
 *  |       |       |             |       |                                |
 *  |   1   |   0   |   <--->     |   1   | Clip obstacle end, done        |
 *  |       |       | <--xxx>     |       |                                |
 *  |       |       |             |       |                                |
 *  |   1   |   1   |     <-----> |   1   | Clip obstacle end, continue    |
 *  |       |       |   <--xx>    |       |                                |
 *  |       |       |             |       |                                |
 *  +-------+-------+-------------+-------+--------------------------------+
 *
 * "done" means, there is no point in continuing to iterate forward in the
 * range query results, there is no way it could overlap any more items.
 * "continue" means, there may be more blocks that overlap the range,
 * continue with the next block, which may be another relevant obstacle.
 *
 */

bool contiguous_allocator_t::take_linear(linaddr_t addr, size_t size,
                                         bool require_free)
{
    assert(!free_addr_by_addr.empty());
    assert(!free_addr_by_size.empty());

    scoped_lock lock(free_addr_lock);

    linaddr_t end = addr + size;

    // Find the last free range that begins before or at the address
    tree_t::iterator by_addr_place = free_addr_by_addr.lower_bound({addr, 0});

    tree_t::iterator next_place;

    tree_t::value_type new_before;
    tree_t::value_type new_after;

    for (; by_addr_place != free_addr_by_addr.end();
         by_addr_place = next_place) {
        tree_t::value_type by_addr = *by_addr_place;

        // Check for sane virtual address (48 bit signed)
        assert((by_addr.first + (UINT64_C(1) << 47)) <
               (UINT64_C(1) << 48));
        // Check for sane size
        assert(by_addr.second < (UINT64_C(1) << 48));

        if (by_addr.first <= addr && (by_addr.first + by_addr.second) >= end) {
            // The free range entry begins before or at the addr,
            // and the block ends at or after addr+size
            // Therefore, need to punch a hole in this free block

            next_place = by_addr_place + 1;

            // Delete the size entry
            free_addr_by_size.erase({by_addr.second, by_addr.first});

            // Delete the address entry
            free_addr_by_addr.erase(by_addr_place);

            // Free space up to beginning of hole
            new_before = {
                // addr
                by_addr.first,
                // size
                addr - by_addr.first
            };

            // Free space after end of hole
            new_after = {
                // addr
                end,
                // size
                (by_addr.first + by_addr.second) - end
            };

            // Insert the by-address free entry before the range if not null
            if (new_before.second > 0)
                free_addr_by_addr.insert(new_before);

            // Insert the by-address free entry after the range if not null
            if (new_after.second > 0)
                free_addr_by_addr.insert(new_after);

            // Insert the by-size free entry before the range if not null
            if (new_before.second > 0)
                free_addr_by_size.insert({new_before.second, new_before.first});

            // Insert the by-size free entry after the range if not null
            if (new_after.second > 0)
                free_addr_by_size.insert({new_after.second, new_after.first});

            validate();

            // Easiest case, whole thing in one range, all done
            return true;
        } else if (require_free) {
            // At this point, the range didn't lie entirely within a free
            // range, and they want to fail if it isn't all free, so fail
            return false;
        } else if (by_addr.first >= end) {
            // Ran off the end of relevant range, therefore done
//            dump("Nothing to do, allocating addr=%#zx, size=%#zx\n",
//                 addr, size);
            return true;
        } else if (by_addr.first < addr &&
                   by_addr.first + by_addr.second > addr) {
            //
            // The found free block is before the range and overlaps it

            next_place = by_addr_place + 1;

            // Delete the size entry
            free_addr_by_size.erase({by_addr.second, by_addr.first});

            // Delete the address entry
            free_addr_by_addr.erase(by_addr_place);

            // Create a smaller block that does not overlap taken range
            // Chop off size so that range ends at addr
            by_addr.second = addr - by_addr.first;

            // Insert smaller range by address
            free_addr_by_addr.insert(by_addr);

            // Insert smaller range by size
            free_addr_by_size.insert({by_addr.second, by_addr.first});

            validate();

            // Keep going...
            continue;
        } else if (by_addr.first >= addr && by_addr.first + by_addr.second <= end) {
            //
            // Range completely covers block, delete block

            next_place = by_addr_place + 1;

            free_addr_by_size.erase({by_addr.second, by_addr.first});

            free_addr_by_addr.erase(by_addr_place);

            validate();

            // Keep going...
            continue;
        } else if (by_addr.first > addr && by_addr.first < end) {
            //
            // Range cut off some of beginning of block

            next_place = by_addr_place + 1;

            free_addr_by_size.erase({by_addr.second, by_addr.first});
            free_addr_by_addr.erase(by_addr_place);

            size_t removed = end - by_addr.second;

            by_addr.first += removed;
            by_addr.second -= removed;

            free_addr_by_addr.insert({by_addr.first, by_addr.second});
            free_addr_by_size.insert({by_addr.second, by_addr.first});

            validate();

            // Keep going...
            continue;
        } else if (by_addr.first + by_addr.second <= addr) {
            // We went past relevant range, done
            return true;
        } else {
            assert(!"What now?");
        }
    }

    return true;
}

void contiguous_allocator_t::release_linear(uintptr_t addr, size_t size)
{
    linaddr_t end = addr + size;

    scoped_lock lock(free_addr_lock);

#if DEBUG_ADDR_ALLOC
    dump("---- Free %#" PRIx64 " @ %#" PRIx64 "\n", size, addr);
#endif

    // Find the nearest free block before the freed range
    tree_t::iterator pred_it = free_addr_by_addr.lower_bound({addr, 0});

    tree_t::key_type pred{};

    if (pred_it != free_addr_by_addr.end())
        pred = *pred_it;

    uint64_t pred_end = pred.first + pred.second;

    uintptr_t freed_end = addr + size;

    // See if we landed inside an already free range,
    // do nothing if so
    if (unlikely(pred.first < addr && pred_end >= freed_end))
        return;

    // Find the nearest free block after the freed range
    tree_t::iterator succ_it = free_addr_by_addr.lower_bound({end, ~0UL});

    tree_t::value_type succ{};

    if (succ_it != free_addr_by_addr.end() && succ_it != pred_it)
        succ = *succ_it;
    else if (succ_it != free_addr_by_addr.end())
        succ = pred;

    int coalesce_pred = ((pred.first + pred.second) == addr);
    int coalesce_succ = (succ.first == end);

    if (coalesce_pred) {
        addr -= pred.second;
        size += pred.second;
        free_addr_by_addr.erase(pred_it);

        free_addr_by_size.erase({pred.second, pred.first});
    }

    if (coalesce_succ) {
        size += succ.second;
        free_addr_by_addr.erase(succ_it);

        free_addr_by_size.erase({succ.second, succ.first});
    }

    free_addr_by_size.insert({size, addr});
    free_addr_by_addr.insert({addr, size});

#if DEBUG_ADDR_ALLOC
    dump("Addr map by addr (after free)");
#endif
}

void contiguous_allocator_t::dump(char const *format, ...) const
{
    va_list ap;
    va_start(ap, format);
    vprintdbg(format, ap);
    va_end(ap);

    printdbg("By addr\n");
    for (tree_t::const_iterator st = free_addr_by_addr.begin(),
         en = free_addr_by_addr.end(), it = st, prev; it != en; ++it) {
        if (it != st && prev->first + prev->second < it->first) {
            printdbg("gap, addr=%#zx, size=%#zx\n",
                     prev->first + prev->second, it->first -
                     (prev->first + prev->second));
        }

        printdbg("ent, addr=%#zx, size=%#zx\n", it->first, it->second);

        prev = it;
    }

    printdbg("By size\n");
    dump_addr_tree(&free_addr_by_size, "size", "addr");
}

bool contiguous_allocator_t::validate() const
{
    // Every by-addr entry matches a corresponding by-size entry
    for (tree_t::const_iterator it = free_addr_by_addr.begin(), en =
         free_addr_by_addr.end(); it != en; ++it) {
        tree_t::const_iterator other =
                free_addr_by_size.find({it->second, it->first});
        if (other == free_addr_by_size.end())
            return validation_failed();
    }

    // Every by-size entry matches a corresponding by-address entry
    for (tree_t::const_iterator it = free_addr_by_size.begin(),
         en = free_addr_by_size.end(); it != en; ++it) {
        tree_t::const_iterator other =
                free_addr_by_addr.find({it->second, it->first});
        if (unlikely(other == free_addr_by_addr.end())) {
            return validation_failed();
        }
    }

    // No overlap
    tree_t::const_iterator prev;
    for (tree_t::const_iterator st = free_addr_by_addr.begin(),
         en = free_addr_by_addr.end(), it = st; it != en; ++it) {
        if (it != st) {
            uintptr_t end = prev->first + prev->second;

            if (unlikely(end > it->first))
                return validation_failed();
        }

        prev = it;
    }

    return true;
}

bool contiguous_allocator_t::validation_failed() const
{
    dump("contiguous allocator validation failed\n");

    return false;
}
