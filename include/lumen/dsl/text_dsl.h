#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>

#include "lumen/core/widget.h"

namespace lumen::dsl {

// 1-based source position used by diagnostics (plan §6.2: errors must carry
// filename, line number, column number and the expected token).
struct SourcePos {
    std::size_t line{1};
    std::size_t column{1};

    [[nodiscard]] bool operator==(const SourcePos& other) const = default;
};

struct DslError {
    std::string file{};
    SourcePos pos{};
    std::string message{};
    std::string expected{};
    std::string found{};

    // "file:line:column: message (expected <expected>, found <found>)".
    [[nodiscard]] std::string format() const;
};

struct DslParseResult {
    core::Widget root{};
    std::optional<DslError> error{};

    [[nodiscard]] bool ok() const { return !error.has_value(); }
};

// Parses a `.lumen` document (plan §6.2): hand-written lexer +
// recursive-descent parser producing the same Widget descriptions the C++
// builders emit. No scripts, variables, conditionals or loops.
//
// Grammar:
//   document := "page" Ident "{" node "}" end
//   node     := Ident attrs? children?
//   attrs    := "(" (value ",")? (attr ("," attr)* ","?)? ")"
//   attr     := Ident ":" value
//   value    := Number | String | Color | "true" | "false" | Ident
//   children := "{" node* "}"
//
// The optional leading positional value is the content of Text/Button.
// Color literals are #RRGGBB or #RRGGBBAA. `//` comments to end of line.
[[nodiscard]] DslParseResult parseLumen(const std::string& source,
                                        std::string filename = "<memory>");

// Reads and parses a file; I/O failure yields error pos 1:1 with message
// "cannot read file".
[[nodiscard]] DslParseResult parseLumenFile(const std::string& path);

// Content-hash keyed parse cache (plan 阶段6: DSL 编译缓存). Hot reload
// re-reads the file every poll but only re-parses when the bytes changed;
// hits return a copy of the cached Widget tree. Parse errors are not
// cached, so a fixed document parses again immediately.
class DslCache {
  public:
    struct Stats {
        std::size_t hits{0};
        std::size_t misses{0};
    };

    // Equivalent to parseLumen(source, filename), memoized by source hash.
    [[nodiscard]] DslParseResult parse(const std::string& source,
                                       std::string filename = "<memory>");

    [[nodiscard]] const Stats& stats() const { return stats_; }
    void clear();

  private:
    std::map<std::uint64_t, core::Widget> entries_{};
    Stats stats_{};
};

}  // namespace lumen::dsl
