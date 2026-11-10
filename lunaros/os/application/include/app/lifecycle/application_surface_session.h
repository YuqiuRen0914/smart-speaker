#pragma once
#include <string>
namespace lunaredu::app {
// A host-owned presentation/session adapter on the foreground owner. App
// lifecycle does not know a desktop shell. False retirement retains this
// adapter and its resources for a later step; destruction never fakes a stop.
class ApplicationSurfaceSession {
public:
  virtual ~ApplicationSurfaceSession()=default;
  virtual bool enter(std::string &error){error.clear();return true;}
  virtual bool retire(std::string &error)=0;
  virtual void finish(){}
  virtual void rollback()=0; // Declaration failed, before enter/resource use.
};
}
