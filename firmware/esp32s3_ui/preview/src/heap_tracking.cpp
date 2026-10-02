#include "ui_host_heap.h"
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <limits>
namespace {
struct alignas(std::max_align_t) Header {
    size_t size;
};
size_t used = 0, peak = 0;
} // namespace
extern "C" void *ui_host_malloc(size_t size) {
    if (size > std::numeric_limits<size_t>::max() - sizeof(Header))
        return nullptr;
    Header *block = static_cast<Header *>(std::malloc(size + sizeof(Header)));
    if (!block)
        return nullptr;
    block->size = size;
    used += size;
    if (used > peak)
        peak = used;
    return block + 1;
}
extern "C" void ui_host_free(void *pointer) {
    if (!pointer)
        return;
    Header *block = static_cast<Header *>(pointer) - 1;
    used -= block->size;
    std::free(block);
}
extern "C" void *ui_host_realloc(void *pointer, size_t size) {
    if (!pointer)
        return ui_host_malloc(size);
    if (!size) {
        ui_host_free(pointer);
        return nullptr;
    }
    Header *previous = static_cast<Header *>(pointer) - 1;
    void *next = ui_host_malloc(size);
    if (!next)
        return nullptr;
    std::memcpy(next, pointer, previous->size < size ? previous->size : size);
    ui_host_free(pointer);
    return next;
}
extern "C" size_t ui_host_heap_used(void) { return used; }
extern "C" size_t ui_host_heap_peak(void) { return peak; }
