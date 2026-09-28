#include "Transfer.h"

#include <Arduino.h>
#include <SD.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_mac.h>
#include <esp_random.h>
#include <strings.h>

#include <new>

#include "Wallpaper.h"
#include "WebPage.h"

namespace transfer {

namespace {

constexpr const char* BOOKS_DIR = "/books";

enum class Kind { Book, Wallpaper };
constexpr uint64_t FREE_SPACE_MARGIN = 1024 * 1024;
constexpr uint32_t SETTLE_MS = 1500;

WebServer* server = nullptr;
File upload;
bool uploading = false;
bool uploadOk = false;
Kind uploadKind = Kind::Book;
char uploadTarget[160];
char uploadTemp[48];
char uploadError[64];
uint32_t uploadBytes = 0;

int received = 0;
bool changed = false;
bool wallpaperDirty = false;
bool pendingChange = false;
uint32_t lastEventMs = 0;
char message[96] = "Waiting for your phone\xE2\x80\xA6";

bool hasExt(const char* name, const char* ext) {
  const size_t n = strlen(name), e = strlen(ext);
  return n > e && strcasecmp(name + n - e, ext) == 0;
}

bool allowedExt(const char* name) { return hasExt(name, ".epub") || hasExt(name, ".txt"); }
bool imageExt(const char* name) { return hasExt(name, ".jpg") || hasExt(name, ".jpeg"); }

const char* dirFor(Kind k) { return k == Kind::Book ? BOOKS_DIR : wallpaper::DIR; }

// Browser file name -> "<dir>/<safe name>"; false if unusable for this kind.
bool targetPath(Kind kind, const String& raw, char* out, size_t cap) {
  const char* name = raw.c_str();
  if (const char* slash = strrchr(name, '/')) name = slash + 1;
  if (const char* bslash = strrchr(name, '\\')) name = bslash + 1;
  while (*name == '.' || *name == ' ') name++;
  char clean[100];
  size_t o = 0;
  for (const char* p = name; *p && o + 1 < sizeof(clean); p++) {
    const char c = *p;
    clean[o++] = (static_cast<uint8_t>(c) < 0x20 || strchr("<>:\"|?*", c)) ? '_' : c;
  }
  clean[o] = '\0';
  if (kind == Kind::Book ? !allowedExt(clean) : !imageExt(clean)) return false;
  snprintf(out, cap, "%s/%s", dirFor(kind), clean);
  return true;
}

// A book file at the top of / or /books.
bool isBookPath(const String& path) {
  const char* p = path.c_str();
  if (path.indexOf("..") >= 0 || !allowedExt(p)) return false;
  const char* rest = strncmp(p, "/books/", 7) == 0 ? p + 7 : (*p == '/' ? p + 1 : nullptr);
  return rest && *rest && !strchr(rest, '/');
}

void jsonEscaped(String& out, const char* s) {
  for (; *s; s++) {
    const char c = *s;
    if (c == '"' || c == '\\') {
      out += '\\';
      out += c;
    } else if (static_cast<uint8_t>(c) < 0x20) {
      out += ' ';
    } else {
      out += c;
    }
  }
}

void listDir(const char* dirPath, String& json, bool& first) {
  File dir = SD.open(dirPath);
  if (!dir || !dir.isDirectory()) return;
  for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
    if (f.isDirectory() || f.name()[0] == '.' || !allowedExt(f.name())) continue;
    json += first ? "{\"path\":\"" : ",{\"path\":\"";
    first = false;
    jsonEscaped(json, f.path());
    json += "\",\"size\":";
    json += static_cast<uint32_t>(f.size());
    json += '}';
  }
}

void noteChange(const char* msg) {
  snprintf(message, sizeof(message), "%s", msg);
  changed = true;
  pendingChange = true;
  lastEventMs = millis();
}

void handleBooks() {
  String json;
  json.reserve(1024);
  char head[48];
  snprintf(head, sizeof(head), "{\"free\":%llu,\"books\":[", SD.totalBytes() - SD.usedBytes());
  json += head;
  bool first = true;
  listDir(BOOKS_DIR, json, first);
  listDir("/", json, first);
  json += "]}";
  server->send(200, "application/json", json);
}

void handleWallpapers() {
  char cur[160];
  wallpaper::current(cur, sizeof(cur));
  String json;
  json.reserve(512);
  json += "{\"current\":\"";
  jsonEscaped(json, SD.exists(cur) ? cur : "");
  json += "\",\"items\":[";
  bool first = true;
  auto add = [&](const char* path, uint32_t size) {
    json += first ? "{\"path\":\"" : ",{\"path\":\"";
    first = false;
    jsonEscaped(json, path);
    json += "\",\"size\":";
    json += size;
    json += '}';
  };
  if (File bg = SD.open(wallpaper::DEFAULT_PATH)) add(wallpaper::DEFAULT_PATH, bg.size());
  File dir = SD.open(wallpaper::DIR);
  if (dir && dir.isDirectory()) {
    for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
      if (!f.isDirectory() && wallpaper::isValidPath(f.path())) add(f.path(), f.size());
    }
  }
  json += "]}";
  server->send(200, "application/json", json);
}

void handleWallpaperImage() {
  const String path = server->arg("path");
  File f = wallpaper::isValidPath(path.c_str()) ? SD.open(path.c_str(), FILE_READ) : File();
  if (!f) {
    server->send(404, "text/plain", "Not found");
    return;
  }
  server->sendHeader("Cache-Control", "max-age=60");
  server->streamFile(f, "image/jpeg");
}

void handleSelectWallpaper() {
  const String path = server->arg("path");
  if (!wallpaper::select(path.c_str())) {
    server->send(400, "text/plain", "Cannot use that image");
    return;
  }
  char msg[96];
  snprintf(msg, sizeof(msg), "Wallpaper set to %s", strrchr(path.c_str(), '/') + 1);
  Serial.printf("[wifi] %s\n", msg);
  wallpaperDirty = true;
  noteChange(msg);
  server->send(200, "text/plain", "OK");
}

void handleDelete() {
  const String path = server->arg("path");
  const bool isWallpaper = wallpaper::isValidPath(path.c_str());
  if ((!isBookPath(path) && !isWallpaper) || !SD.remove(path.c_str())) {
    server->send(400, "text/plain", "Cannot delete that file");
    return;
  }
  char msg[96];
  const char* name = strrchr(path.c_str(), '/') + 1;
  snprintf(msg, sizeof(msg), "Deleted %s", name);
  Serial.printf("[wifi] %s\n", msg);
  if (isWallpaper) wallpaperDirty = true;
  noteChange(msg);
  server->send(200, "text/plain", "OK");
}

// Streams to "<dir>/.upload.part" and renames only once complete, so a
// dropped connection never leaves a partial file where the library sees it.
void handleUploadChunk(Kind kind) {
  HTTPUpload& up = server->upload();
  switch (up.status) {
    case UPLOAD_FILE_START: {
      uploading = true;
      uploadOk = false;
      uploadKind = kind;
      uploadBytes = 0;
      uploadError[0] = '\0';
      snprintf(uploadTemp, sizeof(uploadTemp), "%s/.upload.part", dirFor(kind));
      const uint64_t need = strtoull(server->arg("size").c_str(), nullptr, 10) + FREE_SPACE_MARGIN;
      if (!targetPath(kind, up.filename, uploadTarget, sizeof(uploadTarget))) {
        snprintf(uploadError, sizeof(uploadError), "%s",
                 kind == Kind::Book ? "Only .epub and .txt files are accepted" : "Only JPEG images are accepted");
      } else if (SD.totalBytes() - SD.usedBytes() < need) {
        snprintf(uploadError, sizeof(uploadError), "Not enough space on the SD card");
      } else {
        SD.mkdir(dirFor(kind));
        SD.remove(uploadTemp);
        upload = SD.open(uploadTemp, FILE_WRITE);
        if (!upload) snprintf(uploadError, sizeof(uploadError), "Cannot write to the SD card");
      }
      Serial.printf("[wifi] upload start %s%s%s\n", up.filename.c_str(), uploadError[0] ? ": " : "", uploadError);
      break;
    }
    case UPLOAD_FILE_WRITE:
      if (upload && !uploadError[0]) {
        if (upload.write(up.buf, up.currentSize) != up.currentSize) {
          snprintf(uploadError, sizeof(uploadError), "SD card write failed (card full?)");
        }
        uploadBytes += up.currentSize;
      }
      break;
    case UPLOAD_FILE_END:
      if (upload) upload.close();  // must close before rename/remove
      if (!uploadError[0]) {
        SD.remove(uploadTarget);  // replacing a file with the same name
        if (SD.rename(uploadTemp, uploadTarget)) uploadOk = true;
        else snprintf(uploadError, sizeof(uploadError), "Could not save the file");
      }
      if (!uploadOk) SD.remove(uploadTemp);
      uploading = false;
      break;
    case UPLOAD_FILE_ABORTED:
      if (upload) upload.close();
      SD.remove(uploadTemp);
      snprintf(uploadError, sizeof(uploadError), "Upload interrupted");
      uploading = false;
      break;
  }
}

void handleUploadDone() {
  const bool ok = uploadOk;
  uploadOk = false;
  if (!ok) {
    Serial.printf("[wifi] upload failed: %s\n", uploadError[0] ? uploadError : "no file");
    server->send(400, "text/plain", uploadError[0] ? uploadError : "No file received");
    return;
  }
  char name[64];
  const char* base = strrchr(uploadTarget, '/') + 1;
  const char* dot = strrchr(base, '.');
  snprintf(name, sizeof(name), "%.*s", static_cast<int>(dot ? dot - base : strlen(base)), base);
  char msg[96];
  if (uploadKind == Kind::Book) {
    received++;
    snprintf(msg, sizeof(msg), "Received \xE2\x80\x9C%s\xE2\x80\x9D (%.1f MB)", name, uploadBytes / 1048576.0);
  } else {
    wallpaperDirty = true;
    snprintf(msg, sizeof(msg), "Added wallpaper \xE2\x80\x9C%s\xE2\x80\x9D", name);
  }
  Serial.printf("[wifi] %s -> %s\n", msg, uploadTarget);
  noteChange(msg);
  server->send(200, "text/plain", "OK");
}

}  // namespace

bool start(Info& info) {
  WiFi.mode(WIFI_AP);
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);  // from eFuse; WiFi.softAPmacAddress() read back zeros here
  snprintf(info.ssid, sizeof(info.ssid), "CrowPanel-%02X%02X", mac[4], mac[5]);
  snprintf(info.password, sizeof(info.password), "%08lu", static_cast<unsigned long>(esp_random() % 100000000UL));
  if (!WiFi.softAP(info.ssid, info.password)) {
    Serial.println("[wifi] softAP failed");
    WiFi.mode(WIFI_OFF);
    return false;
  }
  snprintf(info.url, sizeof(info.url), "http://%s", WiFi.softAPIP().toString().c_str());

  server = new (std::nothrow) WebServer(80);
  if (!server) {
    stop();
    return false;
  }
  server->on("/", HTTP_GET, [] { server->send(200, "text/html", WEB_PAGE); });
  server->on("/api/books", HTTP_GET, handleBooks);
  server->on("/api/delete", HTTP_POST, handleDelete);
  server->on("/upload", HTTP_POST, handleUploadDone, [] { handleUploadChunk(Kind::Book); });
  server->on("/upload-wallpaper", HTTP_POST, handleUploadDone, [] { handleUploadChunk(Kind::Wallpaper); });
  server->on("/api/wallpapers", HTTP_GET, handleWallpapers);
  server->on("/api/wallpaper", HTTP_POST, handleSelectWallpaper);
  server->on("/wallpaper", HTTP_GET, handleWallpaperImage);
  server->onNotFound([] {
    server->sendHeader("Location", "/");
    server->send(302, "text/plain", "");
  });
  server->begin();

  received = 0;
  changed = pendingChange = uploading = wallpaperDirty = false;
  snprintf(message, sizeof(message), "Waiting for your phone\xE2\x80\xA6");
  Serial.printf("[wifi] AP %s up at %s\n", info.ssid, info.url);
  return true;
}

void stop() {
  if (server) {
    server->stop();
    delete server;
    server = nullptr;
  }
  if (upload) upload.close();
  if (uploading) SD.remove(uploadTemp);
  uploading = false;
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  Serial.println("[wifi] stopped");
}

void poll() {
  if (server) server->handleClient();
}

bool busy() { return uploading; }

bool takeSettledChange() {
  if (!pendingChange || uploading || millis() - lastEventMs < SETTLE_MS) return false;
  pendingChange = false;
  return true;
}

int receivedCount() { return received; }
const char* lastMessage() { return message; }
bool libraryChanged() { return changed; }
bool wallpaperChanged() { return wallpaperDirty; }

}  // namespace transfer
