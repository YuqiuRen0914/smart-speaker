#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string>

struct StaticPageView {
  const uint8_t *data = nullptr;
  size_t size = 0;
};

struct StaticPageOptions {
  const char *route = "/";
  const char *content_type = "text/html; charset=utf-8";
  const char *not_found_redirect_location = "/";
  bool (*read_page_view)(StaticPageView &page) = nullptr;
  bool (*read_page)(std::string &body) = nullptr;
  bool (*redirect_not_found)() = nullptr;
};

// 注册一个通用静态页面入口；页面内容由调用方提供，HTTP 实现只负责页面承载。
void setup_static_page(const StaticPageOptions &options);
