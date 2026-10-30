// SPDX-License-Identifier: GPL-3.0-or-later
#include "net/replicated_fields.h"

namespace coney::net {

// Stubs for the tests' commit (fails-first); the next commit implements them.
void writeField(BitWriter& /*writer*/, const FieldSpec& /*field*/, const FieldValue& /*value*/) {}
std::expected<FieldValue, Error> readField(BitReader& /*reader*/, const FieldSpec& /*field*/) {
    return fail(ErrorCode::Invalid, "stub");
}
float fieldStep(const FieldSpec& /*field*/) { return 0.0F; }
std::expected<void, Error> validateFields(std::span<const FieldSpec> /*fields*/) { return {}; }
std::size_t fieldsBits(std::span<const FieldSpec> /*fields*/) { return 0; }
std::string_view replicatedKindName(ReplicatedKind /*kind*/) { return {}; }
std::span<const FieldSpec> fieldTable(ReplicatedKind /*kind*/) { return {}; }

} // namespace coney::net
