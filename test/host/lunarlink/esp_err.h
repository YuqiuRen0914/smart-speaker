#pragma once
#include <cstdio>
using esp_err_t=int;
constexpr int ESP_OK=0;
inline void esp_err_to_name_r(int e,char *out,size_t n){std::snprintf(out,n,"error_%d",e);}
