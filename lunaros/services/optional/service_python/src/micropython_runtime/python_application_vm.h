#pragma once
#include "app/execution/application_session.h"
#include <chrono>
namespace lunaredu::micropython {
// All methods, including destruction, belong to the existing Python VM owner.
class PythonApplicationVm {
public:
  bool open(const uint8_t *, size_t, lunaros::apps::SessionStorage, uint32_t, std::string &);
  bool event(int,int,int,int,std::string &);
  bool checkpoint(std::string &);
  bool validate_save(lunaros::apps::SessionStorage,std::string &);
  void close();
  bool active() const { return active_; }
  std::shared_ptr<const lunaros::apps::DrawList> frame() const { return latest_; }
  bool budget_abort();
  bool credit(unsigned &counter,unsigned maximum);
  lunaros::apps::SessionStorage storage_{};
  std::shared_ptr<const lunaros::apps::DrawList> pending_;
  unsigned submissions_=0,reads_=0,writes_=0,logs_=0;
private:
  bool call(const char *,size_t,const intptr_t *,bool,std::string &);
  void begin();
  bool finish_error(void *,std::string &);
  bool active_=false,calling_=false,expired_=false;
  unsigned branches_=0;
  std::chrono::steady_clock::time_point deadline_;
  std::shared_ptr<const lunaros::apps::DrawList> latest_;
};
bool python_application_budget_abort();
}
