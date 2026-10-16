#pragma once
#include "moto/sdk.h"
bool route_vector_store_init(void);
/* SDK owner only. First call takes *bytes on success/AGAIN; repeats poll the job. */
int route_vector_store_commit(uint8_t **bytes, const moto_map_vector_identity *, uint32_t wire_size);
void route_vector_store_reset(bool all);
/* UI task only. Draw holds a snapshot reference, never a storage mutex. */
bool route_vector_store_draw(uint16_t *, unsigned, unsigned, const moto_position *, bool fresh, bool night, bool *complete);
bool route_vector_store_available(void);
bool route_vector_store_changed(void);
