#include "fcntl.h"
#include "unistd.h"
#include "assert.h"
#include <SDL.h>

static int eventfd = 0; 
static int blocking; // fd opened as blocking or not?

int SDL_PushEvent(SDL_Event *ev) {
  return 0;
}

// Poll for currently pending events.
// Returns 1 if there is a pending event or 0 if there are none available.
// since the kernel buffers kb events, we dont buffer it here
int SDL_PollEvent(SDL_Event *ev) {
  
  if (eventfd && blocking==1) {
    close(eventfd); eventfd = 0; 
  }
    
  if (!eventfd) {
    eventfd = open("/dev/events", O_RDONLY | O_NONBLOCK); assert(eventfd>0); 
    blocking = 0; 
  }

  int evtype; unsigned scancode; 

  if (read_kb_event(eventfd, &evtype, &scancode) == 0) {
    if (evtype == KEYDOWN) 
      ev->type = SDL_KEYDOWN; 
    else if (evtype == KEYUP)
      ev->type = SDL_KEYUP;  
    
    ev->key.scancode = scancode; 
    ev->key.keysym.sym = SDL_GetKeyFromScancode(scancode); 
    // printf("SDL_PollEvent: scancode %d, sdl code %d\n", scancode, ev->key.keysym.sym);

    // TODO: SDL_Quit -- Ctrl+Q?
    return 1; 
  } else {
    // printf("%s: no ev\n", __func__); 
    return 0;
  }
}

// Wait indefinitely for the next available event.
// Returns 1 on success or 0 if there was an error while waiting for events
int SDL_WaitEvent(SDL_Event *event) {
  if (eventfd && !blocking) {
    close(eventfd); eventfd = 0; 
  }
  if (!eventfd) {
    eventfd = open("/dev/events", O_RDONLY); assert(eventfd>0); 
    blocking = 1; 
  }  

  return 1;
}

int SDL_PeepEvents(SDL_Event *ev, int numevents, int action, uint32_t mask) {
  return 0;
}

uint8_t* SDL_GetKeyState(int *numkeys) {
  return NULL;
}