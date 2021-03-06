#include "daScript/misc/platform.h"

#include "daScript/misc/memory_model.h"
#include "daScript/misc/debug_break.h"

namespace das {

#if DAS_TRACK_ALLOCATIONS
    uint64_t    g_tracker = 0;
    uint64_t    g_breakpoint= -1ul;

    void das_track_breakpoint ( uint64_t id ) {
        g_breakpoint = id;
    }
#endif

    void Book::reset() {
        totalSize = totalFree = pageSize * totalPages;
        freePageIndex = 0;
        memset ( pages, 0, sizeof(Page)*totalPages );
    }

    void Book::moveBook ( Book & b ) {
        pageSize = b.pageSize;
        totalPages = b.totalPages;
        totalSize = b.totalSize;
        totalFree = b.totalFree;
        freePageIndex = b.freePageIndex;
        data = b.data; b.data = nullptr;
        pages = b.pages; b.pages = nullptr;
    }

    Book::~Book() {
        if ( pages ) {
            delete [] pages;
            pages = nullptr;
        }
        if ( data ) {
            das_aligned_free16(data);
            data = nullptr;
        }
    }

    MemoryModel::MemoryModel ( uint32_t ps ) {
        DAS_ASSERTF(!(ps & 15), "page size must be 16 bytes aligned");
        pageSize = ps;
        alignMask = 15;
        totalAllocated = 0;
        maxAllocated = 0;
    }

    MemoryModel::~MemoryModel() {
        shelf.clear();
        for ( auto & itb : bigStuff ) {
            das_aligned_free16(itb.first);
        }
        bigStuff.clear();
#if DAS_SANITIZER
        for ( auto & itb : deletedBigStuff ) {
            das_aligned_free16(itb.first);
        }
        deletedBigStuff.clear();
#endif
    }

    void MemoryModel::setInitialSize ( uint32_t size ) {
        if ( size && shelf.empty() ) {
            uint32_t tp = (size+pageSize-1) / pageSize;
            shelf.emplace_back(pageSize, das::max(tp,1u));
            initialSize = size;
        }
    }

    char * MemoryModel::allocate ( uint32_t size ) {
        if ( !size ) return nullptr;
        size = (size + alignMask) & ~alignMask;
        totalAllocated += size;
        maxAllocated = das::max(maxAllocated, totalAllocated);
        if ( size > pageSize ) {
            char * ptr = (char *) das_aligned_alloc16(size);
            bigStuff[ptr] = size;
#if DAS_TRACK_ALLOCATIONS
            if ( g_tracker==g_breakpoint ) os_debug_break();
            bigStuffId[ptr] = g_tracker ++;
#endif
            return ptr;
        } else {
            for ( auto & book : shelf ) {
                if ( char * ptr = book.allocate(size) ) {
                    return ptr;
                }
            }
            uint32_t npc = shelf.empty() ? initial_page_count : growPages(shelf.back().totalPages);
            shelf.emplace_back(pageSize, npc);
            return shelf.back().allocate(size);
        }
    }

    bool MemoryModel::free ( char * ptr, uint32_t size ) {
        if ( !size ) return true;
        size = (size + alignMask) & ~alignMask;
        for ( auto & book : shelf ) {
            if ( book.isOwnPtr(ptr) ) {
                book.free(ptr, size);
                totalAllocated -= size;
#if DAS_SANITIZER
                memset(ptr, 0xcd, size);
#endif
                return true;
            }
        }
#if DAS_SANITIZER
        auto itd = deletedBigStuff.find(ptr);
        if ( itd!= deletedBigStuff.end() ) {
            os_debug_break();
        }
#endif
        auto itb = bigStuff.find(ptr);
        if ( itb!=bigStuff.end() ) {
            DAS_ASSERTF(itb->second==size, "free size mismatch, %u allocated vs %u freed", itb->second, size );
#if DAS_SANITIZER
            deletedBigStuff[itb->first] = itb->second;
#else
            das_aligned_free16(itb->first);
#endif
            bigStuff.erase(itb);
            totalAllocated -= size;
#if DAS_TRACK_ALLOCATIONS
            bigStuffId.erase(ptr);
            bigStuffAt.erase(ptr);
            bigStuffComment.erase(ptr);
#endif
            return true;
        }
        DAS_ASSERTF(0, "we are trying to delete pointer, which we did not allocate");
        return false;
    }

    char * MemoryModel::reallocate ( char * ptr, uint32_t size, uint32_t nsize ) {
        if ( !ptr ) return allocate(nsize);
        size = (size + alignMask) & ~alignMask;
        nsize = (nsize + alignMask) & ~alignMask;
        if ( size<pageSize && nsize<pageSize ) {
            for ( auto & book : shelf ) {
                if ( book.isOwnPtr(ptr) ) {
                    if ( auto nptr = book.reallocate(ptr, size, nsize) ) {
                        totalAllocated = totalAllocated - size + nsize;
                        maxAllocated = das::max(maxAllocated, totalAllocated);
                        return nptr;
                    }
                }
            }
        }
        char * nptr = allocate(nsize);
        memcpy ( nptr, ptr, das::min(size,nsize) );
#if DAS_TRACK_ALLOCATIONS
        auto pAt = bigStuffAt.find(ptr);
        if ( pAt != bigStuffAt.end() ) {
            bigStuffAt[nptr] = pAt->second;
        }
        auto pCm = bigStuffComment.find(ptr);
        if ( pCm != bigStuffComment.end() ) {
            bigStuffComment[nptr] = pCm->second;
        }
#endif
        free(ptr, size);
        return nptr;
    }

    void MemoryModel::reset() {
        for ( auto & itb : bigStuff ) {
#if DAS_SANITIZER
            deletedBigStuff[itb.first] = itb.second;
#else
            das_aligned_free16(itb.first);
#endif
        }
        bigStuff.clear();
#if DAS_TRACK_ALLOCATIONS
        bigStuffId.clear();
        bigStuffAt.clear();
        bigStuffComment.clear();
#endif
        if ( shelf.size()!=1 ) {
            uint32_t pages = pagesTotal();
            if ( pages ) {
                shelf.clear();
                shelf.emplace_back(pageSize, pages);
            }
        } else {
            shelf[0].reset();
        }
    }

    uint32_t MemoryModel::pagesTotal() const {
        uint32_t total = 0;
        for ( const auto & book : shelf ) {
            total += book.totalPages;
        }
        return total;
    }

    uint32_t MemoryModel::pagesAllocated() const {
        uint32_t total = 0;
        for ( const auto & book : shelf ) {
            for ( uint32_t i=0; i!=book.totalPages; ++i ) {
                const auto & page = book.pages[i];
                if ( page.offset ) total ++;
            }
        }
        return total;
    }

    uint64_t MemoryModel::totalAlignedMemoryAllocated() const {
        uint64_t mem = 0;
        for (const auto & book : shelf) {
            mem += book.totalSize;
        }
        for (const auto & it : bigStuff) {
            mem += it.second;
        }
        return mem;
    }

    void MemoryModel::sweep() {
        totalAllocated = 0;
        for ( auto & book : shelf ) {
            for ( uint32_t i=0; i!=book.totalPages; ++i ) {
                auto & page = book.pages[i];
                if ( page.total & DAS_PAGE_GC_MASK ) {
                    page.total &= ~DAS_PAGE_GC_MASK;
                    totalAllocated += page.total;
                } else {
                    page.offset = 0;
                    page.total = 0;
                }
            }
        }
        for ( auto it = bigStuff.begin(); it!=bigStuff.end() ; ) {
            if ( it->second & DAS_PAGE_GC_MASK ) {
                it->second &= ~DAS_PAGE_GC_MASK;
                totalAllocated += it->second;
                ++ it;
            } else {
                das_aligned_free16(it->first);
                it = bigStuff.erase(it);
            }
        }
    }
}

