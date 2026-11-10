#include "lunaredu_lunaros/device_web/portal.h"
#include "device_web/portal.h"
#include "lunaredu_lunaros/persistence/storage.h"
#include <cassert>
#include <iostream>
#include <string>
asm(".data\n.global _binary_portal_html_start\n_binary_portal_html_start:\n.asciz \"standard\"\n.global _binary_portal_html_end\n_binary_portal_html_end:\n");
namespace { int reads = 0; std::string file = "filesystem"; bool missing = false; }
namespace lunaredu::lunaros {
bool storage_is_safe_path(const char *p) { return std::string(p).find("..") == std::string::npos; }
bool storage_stream_file(const char *, StorageFileChunkWriter writer, void *context, std::string *error) {
  ++reads; if (missing) { *error = "missing"; return false; }
  for (size_t at = 0; at < file.size(); at += 256)
    if (!writer(reinterpret_cast<const uint8_t *>(file.data()+at), std::min(size_t{256},file.size()-at),context)) return false;
  return true;
}
}
std::string content(lunaredu::lunaros::device_web::Page p, bool recovery = false) {
  const auto view = lunaredu::lunaros::device_web::page(p,recovery);
  return {reinterpret_cast<const char *>(view.data),view.size};
}
int main(int argc, char **argv) {
 using namespace lunaredu::lunaros; using device_web::Page;
 assert(argc == 2); std::string mode = argv[1], error;
 static const uint8_t custom[] = "product";
 if (mode == "default") { assert(content(Page::Home)=="standard"); assert(content(Page::Network)=="standard"); assert(reads==0); }
 else if (mode == "override") {
  DeviceWebConfig invalid; invalid.home.size = 2;
  assert(!configure_device_web(invalid,error));
  invalid.home = {}; invalid.network.data = custom; invalid.network.size = 5000;
  assert(!configure_device_web(invalid,error));
  assert(!configure_device_web({.home={.storage_path="/../bad"}},error));
  assert(configure_device_web({.product_name="Pet <test>",.home={custom,7},.binding={custom,7}},error));
  assert(content(Page::Home)=="product"); assert(content(Page::Binding)=="product");
  assert(content(Page::Network)=="standard"); assert(content(Page::Maintenance)=="standard");
  assert(content(Page::Home,true)=="standard"); assert(reads==0);
 } else if (mode=="all") {
  const DeviceWebPage p{custom,7}; assert(configure_device_web({"Pet",p,p,p,p},error));
  for (auto page:{Page::Home,Page::Network,Page::Binding,Page::Maintenance}) { assert(content(page)=="product"); assert(content(page,true)=="standard"); }
 } else {
  if(mode=="missing")missing=true;
  if(mode=="oversize")file=std::string(5000,'x');
  assert(configure_device_web({.home={.storage_path="/product.html"}},error));
  assert(content(Page::Home)==(mode=="file" ? file : "standard"));
  assert(content(Page::Home)==(mode=="file" ? file : "standard")); assert(reads==1);
 }
 assert(!configure_device_web({},error));
 std::cout << "Device Web " << mode << " passed\n";
}
