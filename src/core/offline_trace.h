#pragma once

namespace offline_trace {

#if defined(DEBUG_OFFLINE_TRACE) || defined(STICKS3_SIMULATOR)
void begin();
void poll();
void record(const char* event, int value = 0, int detail = 0);
#else
inline void begin() {}
inline void poll() {}
inline void record(const char*, int = 0, int = 0) {}
#endif

}  // namespace offline_trace
