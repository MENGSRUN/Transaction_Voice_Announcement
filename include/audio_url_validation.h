#pragma once

#include <stddef.h>
#include <string.h>

inline bool isAllowedMp3HttpsUrl(const char* url, const char* allowedHost) {
  if (!url || !allowedHost || !allowedHost[0]) return false;

  if (strncmp(url, "https://", 8) != 0) return false;
  const char* hostStart = url + 8;
  const char* pathStart = strchr(hostStart, '/');
  if (!pathStart) return false;

  size_t hostLen = (size_t)(pathStart - hostStart);
  if (hostLen != strlen(allowedHost) || strncmp(hostStart, allowedHost, hostLen) != 0) {
    return false;
  }

  const char* pathEnd = strpbrk(pathStart, "?#");
  size_t pathLen = pathEnd ? (size_t)(pathEnd - pathStart) : strlen(pathStart);
  return pathLen >= 4 && strncmp(pathStart + pathLen - 4, ".mp3", 4) == 0;
}
