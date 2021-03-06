#pragma once

namespace das {

#if DAS_TRACK_ALLOCATIONS
    extern uint64_t    g_tracker;
    extern uint64_t    g_breakpoint;
    void das_track_breakpoint ( uint64_t id );
#endif

    #define DAS_PAGE_GC_MASK    0x80000000

    struct LineInfo;

    struct Page {
        __forceinline uint32_t allocate ( uint32_t size, uint32_t pageSize ) {
            if ( offset + size > pageSize ) return -1u;
            offset += size;
            total += size;
            // DAS_ASSERT(total <= pageSize );
            return offset - size;
        }
        __forceinline void free ( uint32_t loc, uint32_t size ) {
            if ( loc + size == offset ) offset -= size;
            // DAS_ASSERT(total >= size);
            total -= size;
            if ( total==0 ) offset = 0;
        }
        __forceinline bool reallocate ( uint32_t loc, uint32_t size, uint32_t nsize, uint32_t pageSize ) {
            if ( loc + size != offset ) return false;
            if ( loc + nsize > pageSize ) return false;
            offset = offset - size + nsize;
            total = total - size + nsize;
            // DAS_ASSERT(total <= pageSize );
            if ( total==0 ) offset = 0;
            return true;
        }
        uint32_t    offset = 0;
        uint32_t    total  = 0;
    };

    struct Book {
        Book () = delete;
        Book (const Book &) = delete;
        Book (Book && b) { moveBook(b); }
        Book & operator = (const Book &) = delete;
        Book & operator = (Book && b) { moveBook(b); return * this; };
        void moveBook ( Book & b );
        Book ( uint32_t ps, uint32_t tp ) : pageSize(ps), totalPages(tp) {
            totalSize = totalFree = pageSize * totalPages;
            freePageIndex = 0;
            data = (char *) das_aligned_alloc16(totalSize);
            pages = new Page[totalPages];
        }
        ~Book();
        void reset();
        __forceinline bool isOwnPtr ( char * ptr ) const {
            return (ptr>=data) && (ptr<(data + totalSize));
        }
        __forceinline char * allocate ( uint32_t size ) {
            if ( size > pageSize ) return nullptr;
            if ( size > totalFree ) return nullptr;
            for ( uint32_t i=0; i!=totalPages; ++i ) {
                uint32_t ofs = pages[freePageIndex].allocate(size,pageSize);
                if ( ofs!=-1u ) {
                    totalFree -= size;
                    return data + freePageIndex*pageSize + ofs;
                }
                freePageIndex ++;
                if ( freePageIndex >= totalPages ) freePageIndex = 0;
            }
            return nullptr;
        }
        __forceinline void free ( char * ptr, uint32_t size ) {
            uint32_t gofs = uint32_t(ptr - data);
            uint32_t idx = gofs / pageSize;
            uint32_t ofs = gofs % pageSize;
            pages[idx].free(ofs, size);
            totalFree += size;
        }
        __forceinline char * reallocate ( char * ptr, uint32_t size, uint32_t nsize ) {
            if ( !ptr ) return allocate(size);
            if ( size==nsize ) return ptr;
            uint32_t gofs = uint32_t(ptr - data);
            uint32_t idx = gofs / pageSize;
            uint32_t ofs = gofs % pageSize;
            if ( pages[idx].reallocate(ofs,size,nsize,pageSize) ) {
                totalFree = totalFree + size - nsize;
                return ptr;
            }
            return nullptr;
        }
        __forceinline void mark ( char * ptr ) {
            uint32_t gofs = uint32_t(ptr - data);
            uint32_t idx = gofs / pageSize;
            pages[idx].total |= DAS_PAGE_GC_MASK;
        }
        uint32_t    pageSize;
        uint32_t    totalPages;
        uint32_t    totalSize;
        uint32_t    totalFree;
        uint32_t    freePageIndex;
        char *      data;
        Page *      pages;
    };

    struct MemoryModel : ptr_ref_count {
        MemoryModel() = delete;
        MemoryModel(const MemoryModel &) = delete;
        MemoryModel & operator = (const MemoryModel &) = delete;
        MemoryModel ( uint32_t ps );
        virtual ~MemoryModel ();
        virtual void reset();
        void setInitialSize ( uint32_t size );
        virtual uint32_t growPages(uint32_t pages) const { return pages * 2; }
        virtual void sweep();
        char * allocate ( uint32_t size );
        bool free ( char * ptr, uint32_t size );
        char * reallocate ( char * ptr, uint32_t size, uint32_t nsize );
        __forceinline bool isOwnPtr( char * ptr ) const {
            for ( auto & book : shelf ) {
                if ( book.isOwnPtr(ptr) ) {
                    return true;
                }
            }
            return bigStuff.find(ptr) != bigStuff.end();
        }
        __forceinline bool isOwnPtrQnD(char* ptr) const {
            return !shelf.empty() ? shelf.front().isOwnPtr(ptr) : false;
        }
        uint32_t bytesAllocated() const { return totalAllocated; }
        uint32_t maxBytesAllocated() const { return maxAllocated; }
        uint32_t pagesAllocated() const;
        uint32_t pagesTotal() const;
        uint64_t totalAlignedMemoryAllocated() const;
        uint32_t                alignMask;
        uint32_t                pageSize;
        uint32_t                totalAllocated;
        uint32_t                maxAllocated;
        uint32_t                initial_page_count = 16;
        uint32_t                initialSize = 0;
        vector<Book>            shelf;
        das_hash_map<void *,uint32_t> bigStuff;  // note: can't use char *, some stl implementations try hashing it as string
#if DAS_SANITIZER
        das_hash_map<void *,uint32_t> deletedBigStuff;
#endif
#if DAS_TRACK_ALLOCATIONS
        das_hash_map<void *,uint64_t> bigStuffId;
        das_hash_map<void *,LineInfo *> bigStuffAt;
        das_hash_map<void *,const char *> bigStuffComment;
        __forceinline void mark_location ( void * ptr, LineInfo * at ) { bigStuffAt[ptr] = at; }
        __forceinline void mark_comment ( void * ptr, const char * what ) { bigStuffComment[ptr] = what; }
#else
        __forceinline void mark_location ( void *, LineInfo * ) {}
        __forceinline void mark_comment ( void *, const char * ) {}
#endif
    };
}
