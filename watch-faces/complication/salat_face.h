#ifndef SALAT_FACE_H_
#define SALAT_FACE_H_

#include "movement.h"

typedef struct {
	char times[6][5];
	int16_t cached_day;
	uint32_t cached_location;
	int32_t cached_timezone;
	uint8_t current_prayer;
	bool cache_valid;
} salat_state_t;

void salat_face_setup(uint8_t watch_face_index, void **context_ptr);
void salat_face_activate(void *context);
bool salat_face_loop(movement_event_t event, void *context);
void salat_face_resign(void *context);

#define salat_face ((const watch_face_t){ \
	salat_face_setup, \
	salat_face_activate, \
	salat_face_loop, \
	salat_face_resign, \
	NULL, \
})

#endif
