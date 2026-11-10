#pragma once
#include <memory>
#include <cstdlib>
#include <new>
#ifdef ESP_PLATFORM
#include <esp_heap_caps.h>
#endif
namespace lunaredu::pet {
// Large semantic objects live outside scarce internal RAM. The three fixed
// frame slots are allocated once per activation; control blocks stay small.
template<class T,class... Args> std::shared_ptr<T> make_scene_object(Args &&...args){
#ifdef ESP_PLATFORM
    auto *p=heap_caps_malloc(sizeof(T),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
#else
    auto *p=std::malloc(sizeof(T));
#endif
    if(!p)return {};
    auto *value=new(p) T(std::forward<Args>(args)...);
    return std::shared_ptr<T>(value,[](T *object){
    object->~T();
#ifdef ESP_PLATFORM
    heap_caps_free(object);
#else
    std::free(object);
#endif
    });
}
}
