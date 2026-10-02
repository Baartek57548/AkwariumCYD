#ifndef UI_HOST_HEAP_H
#define UI_HOST_HEAP_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
void* ui_host_malloc(size_t size);
void* ui_host_realloc(void* pointer,size_t size);
void ui_host_free(void* pointer);
size_t ui_host_heap_used(void);
size_t ui_host_heap_peak(void);
#ifdef __cplusplus
}
#endif
#endif
