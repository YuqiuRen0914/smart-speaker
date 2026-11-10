#pragma once
#include <string>

namespace lunaredu::protocol {
// Pure JSON adapter; operation execution, status selection and authority belong to the caller.
template<class Object> void write_operation_error(Object out,const std::string &code) {
  out["ok"]=false;
  out["error"]["code"]=code.empty()?"operation_failed":code;
}
template<class Object> void write_operation_ok(Object out,bool ok) { out["ok"]=ok; }
}
