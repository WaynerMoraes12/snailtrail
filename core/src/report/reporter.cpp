#include "snailtrail/report/reporter.hpp"

#include <stdexcept>
#include <string>

#include "snailtrail/util/strings.hpp"

namespace snailtrail::report {

std::unique_ptr<Reporter> make_reporter(std::string_view format, RenderOptions options) {
    if (util::iequals(format, "text")) return std::make_unique<TextReporter>(options);
    if (util::iequals(format, "json")) return std::make_unique<JsonReporter>();
    if (util::iequals(format, "markdown") || util::iequals(format, "md")) {
        return std::make_unique<MarkdownReporter>(options);
    }
    throw std::invalid_argument("unknown report format '" + std::string(format) +
                                "' (expected text, json or markdown)");
}

std::vector<std::string_view> reporter_formats() { return {"text", "json", "markdown"}; }

}
