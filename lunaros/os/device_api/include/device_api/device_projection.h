#pragma once

#include "device_api/device_operation_sequence.h"
#include <string_view>

// Device-owned metadata and projection lifecycle, sealed during composition.
namespace lunaredu::lunaros {
class KernelRuntime;
struct StandardProjectionContribution;

using DeviceStatusProvider = void (*)(DeviceApiContext &device, JsonObject payload);
using DeviceInfoProvider = void (*)(DeviceApiContext &device, JsonObject payload);

struct DeviceMetadataProjection {
  const char *id = nullptr;
  const char *status_section = nullptr;
  DeviceStatusProvider status_provider = nullptr;
  DeviceInfoProvider info_provider = nullptr;
};

struct DeviceModuleProjectionMetadata {
  const char *id = nullptr;
  const char *kind = nullptr;
  void (*fill_contract)(JsonObject contract) = nullptr;
  const char *label = nullptr;
  // Stable machine-readable reason surfaced by LunarLink when the module is
  // deliberately declared but unavailable on this product.
  const char *unavailable_reason = nullptr;
  // Immutable Workbench UI v1 JSON. References this module's operation schemas;
  // never contains credentials, live state or transport addresses.
  const char *workbench_ui = nullptr;
};

struct DeviceModuleProjection {
  const char *capability_id = nullptr;
  const DeviceModuleProjectionMetadata *module = nullptr;
  DeviceOperationSequence operations{};

  constexpr DeviceModuleProjection() = default;
  constexpr DeviceModuleProjection(const char *capability,
      const DeviceModuleProjectionMetadata *metadata,
      const DeviceOperationDefinition *definitions, size_t count)
      : capability_id(capability), module(metadata), operations(definitions, count) {}
  constexpr DeviceModuleProjection(const char *capability,
      const DeviceModuleProjectionMetadata *metadata, DeviceOperationSequence sequence)
      : capability_id(capability), module(metadata), operations(sequence) {}
};

using DeviceModuleProjectionVisitor =
    void (*)(const DeviceModuleProjection &descriptor, void *context);

struct DeviceProjectionContribution {
  const DeviceCapabilityDescriptor *capability = nullptr;
  const DeviceModuleProjection *module = nullptr;
  const DeviceMetadataProjection *metadata = nullptr;
  bool capability_active = true;
};

struct DeviceRuntimeContext;

// Device owns projection/model state. Composition seals it before serving.
class DeviceProjectionRegistry {
public:
  // A second registry remains unbound and cannot mutate the Device. Check
  // owns_device() during composition; no existing owner is replaced.
  explicit DeviceProjectionRegistry(DeviceApiContext &device);
  ~DeviceProjectionRegistry();
  DeviceProjectionRegistry(const DeviceProjectionRegistry &) = delete;
  DeviceProjectionRegistry &operator=(const DeviceProjectionRegistry &) = delete;

  bool register_projection(const DeviceProjectionContribution &contribution,
                           std::string *error = nullptr);
  bool owns_device() const;
  bool sealed() const;
  // Changes only when a different composed schema is published. Runtime
  // availability and access are evaluated separately from immutable schema.
  uint32_t schema_revision() const;
  // Firmware-content identity plus immutable composition fingerprint. Stable
  // across reconnects/reboots; never includes a boot nonce or runtime state.
  std::string declaration_revision() const;
  KernelRuntime &runtime() const;
  DeviceApiContext &device() const;
  DeviceRuntimeContext &context();
  const DeviceRuntimeContext &context() const;
  void fill_status_sections(JsonObject payload) const;
  void fill_info_payload(JsonObject payload) const;
  void visit_modules(DeviceModuleProjectionVisitor visitor, void *context) const;
  const DeviceOperationProjection *find_operation(const char *module_id,
                                                  const char *operation_id) const;

private:
  friend class ProductRuntimeAccess;
  friend bool install_standard_device_projections(const StandardProjectionContribution *const *,
      size_t, DeviceProjectionRegistry &, std::string &);
  struct Impl;
  bool reset(std::string &error);
  bool seal(std::string *error = nullptr);
  Impl *impl_ = nullptr;
};

// Component-local lifecycle binding. A product declares the contribution
// during composition; the driver then toggles only that declared capability
// when hardware starts or stops. A successful declaration is immutable for
// this binding lifetime; failed attempts preserve its previous owner.
// This keeps peripheral modules reusable
// without restoring process-wide projection state.
class DeviceProjectionBinding {
public:
  bool declare(DeviceProjectionRegistry &registry,
               const DeviceProjectionContribution &contribution,
               std::string &error) {
    if (!contribution.capability ||
        (context_ && (context_ != &registry.context() || capability_ != contribution.capability))) {
      error = "projection_binding_owner_conflict"; return false;
    }
    DeviceProjectionContribution declaration = contribution;
    declaration.capability_active = false;
    if (!registry.register_projection(declaration, &error)) return false;
    context_ = &registry.context(); capability_ = contribution.capability;
    return true;
  }

  bool activate(const DeviceCapabilityDescriptor &capability,
                std::string *error = nullptr) {
    if (context_ == nullptr || capability_ != &capability) {
      if (error != nullptr) *error = "projection_not_declared";
      return false;
    }
    return context_->capabilities.activate(capability, error);
  }

  void deactivate(const char *capability_id) {
    if (context_ != nullptr && capability_ && capability_id &&
        std::string_view(capability_->id) == capability_id)
      (void)context_->capabilities.deactivate(capability_id);
  }

private:
  DeviceRuntimeContext *context_ = nullptr;
  const DeviceCapabilityDescriptor *capability_ = nullptr;
};

} // namespace lunaredu::lunaros
