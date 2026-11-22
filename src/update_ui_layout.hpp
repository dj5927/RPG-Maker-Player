#pragma once

#include <SDL.h>

// These rectangles share the launcher's 1280x720 logical coordinate system.
// Draw and pointer hit-testing intentionally use the same geometry.
inline SDL_Rect updateModalRect() { return SDL_Rect{342, 202, 596, 314}; }
inline SDL_Rect updateYesRect() { return SDL_Rect{442, 365, 176, 58}; }
inline SDL_Rect updateNoRect() { return SDL_Rect{662, 365, 176, 58}; }
inline SDL_Rect updateProgressTrackRect() { return SDL_Rect{399, 368, 482, 27}; }

inline bool updateRectContains(const SDL_Rect& outer, const SDL_Rect& inner) {
  return inner.x >= outer.x && inner.y >= outer.y &&
         inner.x + inner.w <= outer.x + outer.w &&
         inner.y + inner.h <= outer.y + outer.h;
}
