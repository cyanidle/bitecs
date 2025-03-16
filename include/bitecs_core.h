#pragma once

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct bitecs_registry bitecs_registry;

bitecs_registry* bitecs_new_registry();
void bitecs_delete_registry(bitecs_registry* reg);


#ifdef __cplusplus
}
#endif
