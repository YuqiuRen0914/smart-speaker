#pragma once
#include "lunaros_micropython_runtime_config.h"
#include "kernel/service.h"
namespace lunaredu::micropython {
bool runtime_maintenance_step(void *,bool entering,uint32_t,std::string &error);
lunaros::ServiceNode runtime_lifecycle_node(lunaros::DeviceApiContext &,RuntimeConfig &,const lunaros::ServiceDependency *,size_t,lunaros::ServiceCriticality=lunaros::ServiceCriticality::Required);
}
