// 仅供本地定位原生越界；正式引擎不编译此文件，也不改变正式分配器。
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

namespace {
constexpr uint64_t magic = 0x62756e6465643236ULL;
struct alignas(16) Header {
    void *base;
    size_t bytes, padded;
    uint64_t check;
    bool virtualMemory;
};
void *allocate(size_t bytes) {
    size_t padded = (bytes+15)&~size_t(15);
    if (!padded) padded = 16;
    bool vm = bytes >= 4096;
    void *base;
    char *data;
    if (vm) {
        size_t pages = (padded+sizeof(Header)+4095)&~size_t(4095);
        base = VirtualAlloc(nullptr,pages+4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
        if (!base) throw std::bad_alloc();
        DWORD old;
        VirtualProtect(static_cast<char*>(base)+pages,4096,PAGE_NOACCESS,&old);
        data = static_cast<char*>(base)+pages-padded;
    } else {
        base = std::malloc(padded+sizeof(Header)+15);
        if (!base) throw std::bad_alloc();
        auto address = (reinterpret_cast<uintptr_t>(base)+sizeof(Header)+15)&~uintptr_t(15);
        data = reinterpret_cast<char*>(address);
    }
    auto *head = reinterpret_cast<Header*>(data)-1;
    *head = Header{base,bytes,padded,magic,vm};
    std::memset(data+bytes,0xFD,padded-bytes);
    return data;
}
void release(void *pointer) noexcept {
    if (!pointer) return;
    auto *head = static_cast<Header*>(pointer)-1;
    if (head->check != magic) {
        std::fprintf(stderr,"allocation header corrupted at %p\\n",pointer);
        DebugBreak();
        std::abort();
    }
    for (size_t i = head->bytes; i < head->padded; ++i)
        if (static_cast<unsigned char*>(pointer)[i] != 0xFD) {
            std::fprintf(stderr,"allocation tail corrupted at %p size %zu offset %zu\\n",pointer,head->bytes,i);
            DebugBreak();
            std::abort();
        }
    if (head->virtualMemory)
        VirtualFree(head->base,0,MEM_RELEASE);
    else
        std::free(head->base);
}
}
void *operator new(size_t bytes) { return allocate(bytes); }
void *operator new[](size_t bytes) { return allocate(bytes); }
void operator delete(void *p) noexcept { release(p); }
void operator delete[](void *p) noexcept { release(p); }
void operator delete(void *p,size_t) noexcept { release(p); }
void operator delete[](void *p,size_t) noexcept { release(p); }
