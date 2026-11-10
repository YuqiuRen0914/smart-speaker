#pragma once
#include "app/package/application_package_policy.h"
#include <vector>
namespace lunaredu::lunaros::apps {
bool load_installed_applications(std::vector<Application> &);
bool commit_installed_applications(const std::vector<Application> &);
// Only unreferenced .lapp files belonging to this ID are collected.
bool collect_application_packages(const std::string &id, const std::string &keep = {});
bool collect_unreferenced_application_packages(const std::vector<Application> &);
// Finish one persisted uninstall intent before reinstall; installed means the
// catalog still references the app, so an aborted intent only removes its marker.
bool finish_application_data_deletion(const std::string &, bool installed);
bool remove_installed_application(std::vector<Application> &, const std::string &id,
                                  bool delete_data, std::string &error);
}
