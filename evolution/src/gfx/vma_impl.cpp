// The single translation unit that compiles the VMA implementation. volk comes first so VMA sees the
// function pointers it loads instead of loader prototypes.
#include <volk.h>

#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>
