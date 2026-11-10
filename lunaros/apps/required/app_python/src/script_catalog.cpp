#include "script_catalog.h"
#include "script_name.h"
#include "lunaros_storage.h"
#include "platform/text.h"
#include <cstring>
using namespace lunaredu::lunaros;
namespace lunaredu::python::scripts {
namespace {
std::string normalize_script_name(const std::string &name) {
  return lunaredu::python::scripts::valid_script_name(name) ? name : std::string();
}

bool has_python_extension(const std::string &path) {
  return path.length() >= 3 && path.compare(path.length() - 3, 3, ".py") == 0;
}

std::string script_path_for_name(const std::string &name) {
  std::string path = kScriptRoot;
  path += "/";
  path += name;
  if (!has_python_extension(path)) {
    path += ".py";
  }
  return path;
}

bool is_script_run_path(const std::string &path) {
  const std::string root_prefix = std::string(kScriptRoot) + "/";
  return path.length() > root_prefix.length() &&
         path.compare(0, root_prefix.length(), root_prefix) == 0 &&
         has_python_extension(path);
}

std::string resolve_script_run_path(const std::string &path,
                                    std::string &error) {
  error.clear();
  const std::string trimmed = lunaredu::lunaros::text::trim_copy(path);
  if (trimmed.empty()) {
    error = "empty_path";
    return "";
  }

  if (trimmed[0] != '/') {
    const std::string normalized_name = normalize_script_name(trimmed);
    if (normalized_name.empty()) {
      error = "invalid_script_name";
      return "";
    }
    return script_path_for_name(normalized_name);
  }

  const std::string normalized_path =
      storage_normalize_path(trimmed.c_str());
  if (normalized_path.empty()) {
    error = "invalid_script_path";
    return "";
  }
  if (!is_script_run_path(normalized_path)) {
    error = "script_path_outside_root";
    return "";
  }

  return normalized_path;
}

bool ensure_script_dir() {
  if (!storage_ready()) {
    return false;
  }
  if (storage_path_exists(kScriptRoot)) {
    return true;
  }
  return storage_ensure_directory(kScriptRoot);
}

bool write_autorun(const std::string &name, std::string &error) {
  if (!storage_ready()) { error = "storage_not_ready"; return false; }
  if (!storage_write_text_file(kAutorunPath, name.c_str())) {
    error = "autorun_write_failed"; return false;
  }
  error.clear(); return true;
}
}
bool autorun(std::string &name, std::string &error) {
  name.clear(); error.clear();
  if (!storage_ready()) { error="storage_not_ready"; return false; }
  if (!storage_path_exists(kAutorunPath)) return true;
  std::string content;
  if (!storage_read_text_file(kAutorunPath,content)) { error="autorun_read_failed"; return false; }
  name=content.rfind(kProjectAutorunPrefix,0)==0 && content.size()>9 && content.size()<=105
      ? content : normalize_script_name(content);
  if (!content.empty() && name.empty()) { error="invalid_autorun_script"; return false; }
  return true;
}
bool set_project_autorun(const std::string &project_id,std::string &error) {
  return write_autorun(project_id.empty()?std::string():std::string(kProjectAutorunPrefix)+project_id,error);
}
bool restore_autorun_selection(const std::string &selection,std::string &error) {
  if(!selection.empty()&&!valid_script_name(selection)&&
      !(selection.rfind(kProjectAutorunPrefix,0)==0&&selection.size()>9&&selection.size()<=105)) {
    error="invalid_autorun_selection";return false;
  }
  return write_autorun(selection,error);
}
bool load_source(const std::string &name, std::string &source, std::string &path, std::string &error) {
  source.clear();
  path=resolve_script_run_path(name,error);
  if(path.empty()) return false;
  if(!storage_ready()) { error="storage_not_ready"; return false; }
  if(!storage_path_exists(path.c_str())) { error="script_not_found"; return false; }
  if(!storage_read_text_file(path.c_str(),source)) { error="script_read_failed"; return false; }
  error.clear(); return true;
}
bool write(const std::string &name, const std::string &content,
                              bool set_autorun, std::string &error) {
  if (!ensure_script_dir()) {
    error = "storage_not_ready";
    return false;
  }

  const std::string normalized = normalize_script_name(name);
  if (normalized.empty()) {
    error = "invalid_script_name";
    return false;
  }

  const std::string path = script_path_for_name(normalized);
  if (!storage_write_text_file(path.c_str(), content.c_str())) {
    error = "script_write_failed";
    return false;
  }

  if (set_autorun && !write_autorun(normalized, error)) {
    return false;
  }

  error.clear();
  return true;
}

bool read(const std::string &name, std::string &content,
                             std::string &error) {
  content.clear(); error.clear();
  const std::string normalized = normalize_script_name(name);
  if (normalized.empty()) {
    error = "invalid_script_name";
    return false;
  }

  const std::string path = script_path_for_name(normalized);
  if (!storage_path_exists(path.c_str())) {
    error = "script_not_found";
    return false;
  }

  if (!storage_read_text_file(path.c_str(), content)) {
    error = "script_read_failed";
    return false;
  }

  return true;
}

bool remove(const std::string &name, std::string &error) {
  const std::string normalized = normalize_script_name(name);
  if (normalized.empty()) {
    error = "invalid_script_name";
    return false;
  }

  const std::string path = script_path_for_name(normalized);
  if (!storage_path_exists(path.c_str())) {
    error = "script_not_found";
    return false;
  }

  std::string selected;
  if (!autorun(selected,error)) return false;
  if (selected == normalized && !write_autorun("",error)) return false;
  if (!storage_delete_file(path.c_str())) {
    error = "script_delete_failed";
    return false;
  }

  error.clear();
  return true;
}

bool set_autorun(const std::string &name, std::string &error) {
  if (name.empty()) return write_autorun("",error);
  const std::string normalized = normalize_script_name(name);
  if (normalized.empty()) {
    error = "invalid_script_name";
    return false;
  }

  const std::string path = script_path_for_name(normalized);
  if (!storage_path_exists(path.c_str())) {
    error = "script_not_found";
    return false;
  }

  return write_autorun(normalized, error);
}

bool list(JsonObject payload, std::string &error) {
  if (!ensure_script_dir()) {
    error = "storage_not_ready";
    return false;
  }

  std::string selected;
  if (!autorun(selected,error)) return false;
  payload["ok"] = true;
  payload["autorun"] = selected;
  JsonArray scripts = payload["scripts"].to<JsonArray>();

  struct Context {
    JsonArray *scripts = nullptr;
    const std::string *autorun = nullptr;
  } context;
  context.scripts = &scripts;
  context.autorun = &selected;

  if (!storage_list_entries(
          kScriptRoot,
          [](const StorageListEntry &entry, void *raw) {
            if (entry.is_directory || raw == nullptr) {
              return true;
            }
            Context *ctx = static_cast<Context *>(raw);
            if (ctx->scripts == nullptr || ctx->autorun == nullptr) {
              return false;
            }

            std::string full_path = entry.path;
            const std::string::size_type slash = full_path.find_last_of('/');
            std::string name = slash == std::string::npos
                                   ? full_path
                                   : full_path.substr(slash + 1);
            if (!has_python_extension(name)) {
              return true;
            }
            name = name.substr(0, name.length() - 3);

            JsonObject item = ctx->scripts->add<JsonObject>();
            item["name"] = name;
            item["size"] = entry.size_bytes;
            item["autorun"] = name == *ctx->autorun;
            return true;
          },
          &context)) {
    error = "script_list_failed";
    return false;
  }

  return true;
}

}
