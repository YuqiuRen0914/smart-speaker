#pragma once

// Shared mapping for buffered, binary and streamed Device API responses.
namespace lunaredu::lunaros::internal {

inline const char *http_status_text(int status_code) {
  switch (status_code) {
  case 200:
    return "200 OK";
  case 201:
    return "201 Created";
  case 202:
    return "202 Accepted";
  case 204:
    return "204 No Content";
  case 302:
    return "302 Found";
  case 400:
    return "400 Bad Request";
  case 401:
    return "401 Unauthorized";
  case 403:
    return "403 Forbidden";
  case 404:
    return "404 Not Found";
  case 405:
    return "405 Method Not Allowed";
  case 408:
    return "408 Request Timeout";
  case 409:
    return "409 Conflict";
  case 413:
    return "413 Payload Too Large";
  case 423:
    return "423 Locked";
  case 429:
    return "429 Too Many Requests";
  case 500:
    return "500 Internal Server Error";
  case 503:
    return "503 Service Unavailable";
  default:
    return "500 Internal Server Error";
  }
}

} // namespace lunaredu::lunaros::internal
