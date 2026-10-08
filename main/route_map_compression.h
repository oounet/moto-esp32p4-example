#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
size_t route_map_expanded_size(const uint8_t *,size_t,size_t limit);
bool route_map_inflate(const uint8_t *,size_t,uint8_t *,size_t);
