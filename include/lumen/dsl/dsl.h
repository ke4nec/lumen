#pragma once

// C++ declarative API surface (Stage 3) and text `.lumen` DSL (Stage 4).
// Stage 1 intentionally leaves this empty; the module exists so CMake
// targets and include paths stay stable across stages.
namespace lumen::dsl {

// Stage identifier for the DSL module contract (Stage 1 baseline).
[[nodiscard]] const char* dslStageName();

}  // namespace lumen::dsl
