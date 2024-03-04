#pragma once

#include <stddef.h>

void settimezone(const char *timezone);

void get_time(const char *format, char *strftime_buf, size_t maxsize);

void check_and_update_ntp_time(void);