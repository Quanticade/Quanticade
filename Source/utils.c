#include "utils.h"
#include "structs.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#ifdef WIN64
#include <windows.h>
#else
#include <sys/time.h>
#endif

// Misc functions. Some of them from VICE by Richard Allbert

uint64_t get_time_ms() {
#ifdef WIN64
  return GetTickCount();
#else
  struct timeval time_value;
  gettimeofday(&time_value, NULL);
  return time_value.tv_sec * 1000 + time_value.tv_usec / 1000;
#endif
}
