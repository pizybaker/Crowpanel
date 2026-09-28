#pragma once

#include <cstdint>

// Wi-Fi transfer: the reader runs its own WPA2 hotspot and a small web server;
// a phone joins it to upload books into /books and wallpapers into /wallpapers
// and to choose the home-screen wallpaper.
namespace transfer {

struct Info {
  char ssid[24];
  char password[12];  // random per session
  char url[32];
};

bool start(Info& info);
void stop();

// Serves pending HTTP requests; call often while the transfer screen is up.
void poll();

// True while a file is being received (don't block on a screen refresh then).
bool busy();

// Returns true once per batch of changes (uploads finished / files deleted)
// after the phone has been quiet for a moment, so the screen refreshes once.
bool takeSettledChange();

int receivedCount();
const char* lastMessage();  // e.g. "Received “Dune” (1.2 MB)"
bool libraryChanged();      // anything added or deleted since start()
bool wallpaperChanged();    // wallpaper added, deleted or chosen since start()

}  // namespace transfer
