#include "expression_config.h"
#include <atomic>
#include <mutex>
namespace lunaredu::pet::character {
namespace {
std::mutex mutex;
ExpressionTable expressions;
ExpressionOverrides overridden;
std::atomic<uint32_t> generation{0},rendered{0};
}
void publish_expression_configs(const ExpressionTable &table,const ExpressionOverrides &mask){
  std::lock_guard lock(mutex);expressions=table;overridden=mask;
  generation.fetch_add(1,std::memory_order_release);
}
ExpressionConfig expression_config(size_t state,const ExpressionConfig &builtin){
  std::lock_guard lock(mutex);
  return state<kExpressionCount&&overridden[state]?expressions[state]:builtin;
}
uint32_t active_generation(){return generation.load(std::memory_order_acquire);}
uint32_t rendered_generation(){return rendered.load(std::memory_order_acquire);}
void mark_rendered(uint32_t value){rendered.store(value,std::memory_order_release);}
}
