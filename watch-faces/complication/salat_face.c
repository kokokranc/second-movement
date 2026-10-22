#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "salat_face.h"
#include "filesystem.h"
#include "sunriset.h"
#include "watch.h"

#define SALAT_PI 3.14159265358979323846

static const char salat_tags[6][4] = {"FJ", "RI", "DH", "AS", "1N", "1S"};
static const uint8_t salat_days_before_month[] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};

static int16_t salat_day_of_year(watch_date_time_t date_time) {
	int16_t day = salat_days_before_month[date_time.unit.month - 1] + date_time.unit.day;
	uint16_t year = date_time.unit.year + WATCH_RTC_REFERENCE_YEAR;
	if (date_time.unit.month > 2 && (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0))) day++;
	return day;
}

static double salat_hour_angle(double angle, bool morning, double dhuhr, double sin_lat, double cos_lat, double sin_dec, double cos_dec) {
	double altitude = -angle * SALAT_PI / 180.0;
	double cos_hour_angle = (sin(altitude) - sin_lat * sin_dec) / (cos_lat * cos_dec);
	if (cos_hour_angle > 1.0 || cos_hour_angle < -1.0) return dhuhr + (morning ? -1.5 : 6.0);
	double hour_angle = acos(cos_hour_angle) * 180.0 / SALAT_PI / 15.0;
	return morning ? dhuhr - hour_angle : dhuhr + hour_angle;
}

static bool salat_compute_times(salat_state_t *state, watch_date_time_t date_time) {
	movement_location_t location = {0};
	filesystem_read_file("location.u32", (char *)&location.reg, sizeof(location.reg));
	int16_t latitude_centi = (int16_t)location.bit.latitude;
	int16_t longitude_centi = (int16_t)location.bit.longitude;
	double latitude = (double)latitude_centi / 100.0;
	double longitude = (double)longitude_centi / 100.0;
	int32_t timezone = movement_get_timezone_offset_for_date(date_time);
	int16_t day = salat_day_of_year(date_time);

	if (state->cache_valid && state->cached_day == day && state->cached_location == location.reg && state->cached_timezone == timezone) return false;

	state->cached_day = day;
	state->cached_location = location.reg;
	state->cached_timezone = timezone;
	state->cache_valid = true;

	bool southern = latitude < 0.0;
	double abs_latitude = fabs(latitude);
	double fajr_angle = 18.0;
	double isha_angle = 18.0;
	uint8_t isha_interval_minutes = 0;
	double asr_shadow_multiplier = 1.0;

	if (abs_latitude > 0.0 && abs_latitude < 8.0 && longitude > 99.0 && longitude < 120.0) {
		fajr_angle = 18.0;
		isha_angle = 18.0;
	} else if (latitude > 24.0 && latitude < 49.0 && longitude > -125.0 && longitude < -66.0) {
		fajr_angle = 15.0;
		isha_angle = 15.0;
	} else if (latitude > 20.0 && latitude < 32.0 && longitude > 34.0 && longitude < 50.0) {
		fajr_angle = 18.5;
		isha_interval_minutes = 90;
	} else if (southern && abs_latitude > 10.0 && abs_latitude < 39.0 && longitude > 113.0 && longitude < 154.0) {
		fajr_angle = 18.0;
		isha_angle = 17.0;
	}

	double latitude_rad = abs_latitude * SALAT_PI / 180.0;
	double cos_lat = cos(latitude_rad);
	double sin_lat = sin(latitude_rad);
	double b = (2.0 * SALAT_PI * (day - 81)) / 365.0;
	double equation_of_time = (9.87 * sin(2.0 * b) - 7.53 * cos(b) - 1.5 * sin(b)) / 60.0;
	int16_t solar_day = southern ? day + 182 : day;
	double declination = 23.45 * sin((360.0 / 365.0) * (solar_day - 80) * SALAT_PI / 180.0) * SALAT_PI / 180.0;
	double sin_dec = sin(declination);
	double cos_dec = cos(declination);
	double dhuhr = 12.0 + (double)timezone / 3600.0 - longitude / 15.0 - equation_of_time;
	double timezone_hours = (double)timezone / 3600.0;
	double fajr_utc, sunrise_utc, sunset_utc, isha_utc, unused_set;
	int year = date_time.unit.year + WATCH_RTC_REFERENCE_YEAR;
	int month = date_time.unit.month;
	int day_of_month = date_time.unit.day;
	int fajr_result = __sunriset__(year, month, day_of_month, longitude, latitude, -fajr_angle, 0, &fajr_utc, &unused_set);
	int sunrise_result = sun_rise_set(year, month, day_of_month, longitude, latitude, &sunrise_utc, &sunset_utc);
	int isha_result = __sunriset__(year, month, day_of_month, longitude, latitude, -isha_angle, 0, &isha_utc, &unused_set);
	double solar_noon = sunrise_result == 0 ? (sunrise_utc + sunset_utc) / 2.0 + timezone_hours : dhuhr;

	for (uint8_t index = 0; index < 6; index++) {
		double time;
		switch (index) {
			case 0:
				time = fajr_result == 0 ? fajr_utc + timezone_hours : salat_hour_angle(fajr_angle, true, dhuhr, sin_lat, cos_lat, sin_dec, cos_dec);
				break;
			case 1:
				time = sunrise_result == 0 ? sunrise_utc + timezone_hours : salat_hour_angle(0.833, true, dhuhr, sin_lat, cos_lat, sin_dec, cos_dec);
				break;
			case 2:
				time = solar_noon + 0.0166;
				break;
			case 3: {
				double shadow_difference = asr_shadow_multiplier + fabs(tan(latitude_rad - declination));
				double asr_altitude = atan(1.0 / shadow_difference);
				double cos_hour_angle = (sin(asr_altitude) - sin_lat * sin_dec) / (cos_lat * cos_dec);
				double asr_hour_angle = (cos_hour_angle >= -1.0 && cos_hour_angle <= 1.0) ? acos(cos_hour_angle) * 180.0 / SALAT_PI / 15.0 : 3.0;
				time = solar_noon + asr_hour_angle;
				break;
			}
			case 4:
				time = sunrise_result == 0 ? sunset_utc + timezone_hours : salat_hour_angle(0.833, false, dhuhr, sin_lat, cos_lat, sin_dec, cos_dec);
				break;
			default:
				time = isha_interval_minutes > 0 ? (sunrise_result == 0 ? sunset_utc + timezone_hours : salat_hour_angle(0.833, false, dhuhr, sin_lat, cos_lat, sin_dec, cos_dec)) + (double)isha_interval_minutes / 60.0 : (isha_result == 0 ? isha_utc + timezone_hours : salat_hour_angle(isha_angle, false, dhuhr, sin_lat, cos_lat, sin_dec, cos_dec));
				break;
		}

		time = fmod(time + 24.0, 24.0);
		int hours = (int)floor(time);
		int minutes = (int)round((time - hours) * 60.0);
		if (minutes == 60) {
			minutes = 0;
			hours = (hours + 1) % 24;
		}
		snprintf(state->times[index], sizeof(state->times[index]), "%02d%02d", hours, minutes);
	}

	return true;
}

static void salat_display(salat_state_t *state) {
	char display[7];
	watch_clear_display();
	watch_set_colon();
	snprintf(display, sizeof(display), "%.4s%s", state->times[state->current_prayer], salat_tags[state->current_prayer]);
	watch_display_text(WATCH_POSITION_BOTTOM, display);
}

void salat_face_setup(uint8_t watch_face_index, void **context_ptr) {
	(void)watch_face_index;
	if (*context_ptr == NULL) {
		*context_ptr = malloc(sizeof(salat_state_t));
		memset(*context_ptr, 0, sizeof(salat_state_t));
	}
}

void salat_face_activate(void *context) {
	salat_state_t *state = (salat_state_t *)context;
	salat_compute_times(state, movement_get_local_date_time());
	salat_display(state);
}

bool salat_face_loop(movement_event_t event, void *context) {
	salat_state_t *state = (salat_state_t *)context;
	switch (event.event_type) {
		case EVENT_TICK: {
			watch_date_time_t date_time = movement_get_local_date_time();
			if (date_time.unit.minute == 0 && date_time.unit.second == 0 && salat_compute_times(state, date_time)) salat_display(state);
			break;
		}
		case EVENT_LOW_ENERGY_UPDATE: {
			watch_date_time_t date_time = movement_get_local_date_time();
			if (salat_compute_times(state, date_time)) salat_display(state);
			break;
		}
		case EVENT_ALARM_BUTTON_UP:
			state->current_prayer = (state->current_prayer + 1) % 6;
			if (movement_button_should_sound()) movement_play_note(BUZZER_NOTE_C7, 50);
			salat_display(state);
			break;
		case EVENT_LIGHT_BUTTON_UP:
			state->current_prayer = 0;
			salat_display(state);
			break;
		case EVENT_MODE_BUTTON_UP:
			movement_move_to_next_page();
			break;
		default:
			break;
	}
	return true;
}

void salat_face_resign(void *context) {
	(void)context;
}
