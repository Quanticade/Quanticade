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

uint64_t get_time_ms(void) {
#ifdef WIN64
  return GetTickCount();
#else
  struct timeval time_value;
  gettimeofday(&time_value, NULL);
  return time_value.tv_sec * 1000 + time_value.tv_usec / 1000;
#endif
}

int input_waiting(void) {
#ifndef WIN32
  fd_set readfds;
  struct timeval tv;
  FD_ZERO(&readfds);
  FD_SET(fileno(stdin), &readfds);
  tv.tv_sec = 0;
  tv.tv_usec = 0;
  select(16, &readfds, 0, 0, &tv);

  return (FD_ISSET(fileno(stdin), &readfds));
#else
  static int init = 0, pipe;
  static HANDLE inh;
  DWORD dw;

  if (!init) {
    init = 1;
    inh = GetStdHandle(STD_INPUT_HANDLE);
    pipe = !GetConsoleMode(inh, &dw);
    if (!pipe) {
      SetConsoleMode(inh, dw & ~(ENABLE_MOUSE_INPUT | ENABLE_WINDOW_INPUT));
      FlushConsoleInputBuffer(inh);
    }
  }

  if (pipe) {
    if (!PeekNamedPipe(inh, NULL, 0, NULL, &dw, NULL))
      return 1;
    return dw;
  }

  else {
    GetNumberOfConsoleInputEvents(inh, &dw);
    return dw <= 1 ? 0 : dw;
  }

#endif
}

// read GUI/user input
void read_input(thread_t *thread) {
  // bytes-to-read holder
  int bytes;

  // GUI/user input
  char input[256] = "", *endc;

  // "listen" to STDIN
  if (input_waiting()) {
    // tell engine to stop calculating
    thread->stopped = 1;

    // loop to read bytes from STDIN
    do {
      // read bytes from STDIN
      bytes = read(fileno(stdin), input, 256);
    }

    // until bytes available
    while (bytes < 0);

    // searches for the first occurrence of '\n'
    endc = strchr(input, '\n');

    // if found new-line set value at pointer to 0
    if (endc)
      *endc = 0;

    // if input is available
    if (strlen(input) > 0) {
      // match UCI "quit" command
      if (!strncmp(input, "quit", 4))
        // tell engine to terminate execution
        thread->quit = 1;

      // // match UCI "stop" command
      else if (!strncmp(input, "stop", 4))
        // tell engine to terminate execution
        thread->quit = 1;
    }
  }
}
