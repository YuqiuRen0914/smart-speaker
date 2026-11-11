#pragma once
#include <string>
#include <vector>
#include <utility>
namespace lunaredu::lunaros {
struct NetworkFetchRequest {
 std::string method,url,body;
 int timeout_ms=0;
 size_t maximum_response_bytes=0;
 std::vector<std::pair<std::string,std::string>> headers;
};
struct NetworkResponse {int status_code=200,last_error=0;std::string body,error_detail;};
bool network_fetch_resource(NetworkFetchRequest,NetworkResponse*);
}
