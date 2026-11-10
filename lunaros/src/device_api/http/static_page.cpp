#include "device_api/http/static_page.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>

#include "lunaredu_lunaros/kernel/log.h"
#include "device_api/http/http_request_api.h"

namespace {

StaticPageOptions g_options;
constexpr const char *kDefaultStaticPageFallback =
    "<!doctype html><html lang=\"zh-CN\"><head><meta charset=\"utf-8\">"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
    "<title>Lunar Hardware</title></head><body style=\"font-family:"
    "sans-serif;padding:24px\">"
    "<h1>页面资源缺失</h1><p>设备文件系统中没有找到页面资源，请检查 data/ "
    "目录。</p></body></html>";

void send_static_page_response(int status_code,
                               const StaticPageView &page);

void send_static_page_response(int status_code, const std::string &body) {
  send_static_page_response(
      status_code,
      StaticPageView{
          reinterpret_cast<const uint8_t *>(body.data()),
          body.size(),
      });
}

void send_static_page_response(int status_code,
                               const StaticPageView &page) {
  if (page.data == nullptr || page.size == 0) {
    send_http_response(status_code, g_options.content_type, "");
    return;
  }
  send_http_binary_response(status_code, g_options.content_type, page.data,
                            page.size);
}

void handle_static_page() {
  add_response_header("Cache-Control", "no-store, no-cache, must-revalidate");
  add_response_header("Pragma", "no-cache");
  if (g_options.read_page_view != nullptr) {
    StaticPageView page;
    if (g_options.read_page_view(page)) {
      send_static_page_response(200, page);
      return;
    }
  }

  std::string body;
  if (g_options.read_page == nullptr || !g_options.read_page(body)) {
    send_http_response(200, g_options.content_type, kDefaultStaticPageFallback);
    return;
  }

  send_static_page_response(200, body);
}

void redirect_to_static_page() {
  add_response_header("Location", g_options.not_found_redirect_location);
  send_http_response(302, "text/plain", "");
}

void handle_static_page_not_found() {
  if (g_options.redirect_not_found != nullptr && g_options.redirect_not_found()) {
    redirect_to_static_page();
    return;
  }
  send_http_response(404, "text/plain; charset=utf-8", "not_found");
}

void handle_captive_portal_probe() {
  if (g_options.redirect_not_found != nullptr && g_options.redirect_not_found()) {
    redirect_to_static_page();
    return;
  }
  send_http_response(204, "text/plain", "");
}

void handle_favicon() { send_http_response(204, "image/x-icon", ""); }

} // namespace

void setup_static_page(const StaticPageOptions &options) {
  g_options = options;
  if (!register_http_route(g_options.route, HTTP_GET, handle_static_page)) {
    LOGE("device_api_http", "static page route registration failed uri=%s",
         g_options.route ? g_options.route : "(null)");
  }
  if (!register_http_route(g_options.route, HTTP_OPTIONS, handle_options)) {
    LOGE("device_api_http", "static page options route registration failed uri=%s",
         g_options.route ? g_options.route : "(null)");
  }
  register_http_route("/generate_204", HTTP_GET, handle_captive_portal_probe);
  register_http_route("/gen_204", HTTP_GET, handle_captive_portal_probe);
  register_http_route("/hotspot-detect.html", HTTP_GET,
                      handle_captive_portal_probe);
  register_http_route("/favicon.ico", HTTP_GET, handle_favicon);
  register_http_not_found_handler(handle_static_page_not_found);
}
