#pragma once
template<class... T> void test_log(T...) {}
#define LOGE(...) test_log(__VA_ARGS__)
#define LOGW(...) test_log(__VA_ARGS__)
#define LOGI(...) test_log(__VA_ARGS__)
