#pragma once

#include <cstddef>
#include <memory>
#include <ostream>
#include <span>
#include <string_view>
#include <vector>

#include "snailtrail/advisor/finding.hpp"
#include "snailtrail/analysis/report.hpp"

namespace snailtrail::report {

struct RenderOptions {
    bool color = false;
    std::size_t width = 100;
    std::size_t detail_limit = 10;
    std::size_t sample_lines = 12;
};

class Reporter {
public:
    Reporter() = default;
    Reporter(const Reporter&) = delete;
    Reporter& operator=(const Reporter&) = delete;
    Reporter(Reporter&&) = delete;
    Reporter& operator=(Reporter&&) = delete;
    virtual ~Reporter() = default;

    [[nodiscard]] virtual std::string_view format_name() const noexcept = 0;
    virtual void render(const analysis::Report& report, std::ostream& out) const = 0;
};

class TextReporter final : public Reporter {
public:
    explicit TextReporter(RenderOptions options = {}) : options_(options) {}
    [[nodiscard]] std::string_view format_name() const noexcept override { return "text"; }
    void render(const analysis::Report& report, std::ostream& out) const override;

private:
    RenderOptions options_;
};

class JsonReporter final : public Reporter {
public:
    explicit JsonReporter(int indent = 2) : indent_(indent) {}
    [[nodiscard]] std::string_view format_name() const noexcept override { return "json"; }
    void render(const analysis::Report& report, std::ostream& out) const override;

private:
    int indent_;
};

class MarkdownReporter final : public Reporter {
public:
    explicit MarkdownReporter(RenderOptions options = {}) : options_(options) {}
    [[nodiscard]] std::string_view format_name() const noexcept override { return "markdown"; }
    void render(const analysis::Report& report, std::ostream& out) const override;

private:
    RenderOptions options_;
};

std::unique_ptr<Reporter> make_reporter(std::string_view format, RenderOptions options = {});
std::vector<std::string_view> reporter_formats();

void render_findings(std::span<const advisor::Finding> findings, std::ostream& out,
                     const RenderOptions& options, std::size_t indent = 2);

void write_findings_json(std::span<const advisor::Finding> findings, std::ostream& out, int indent = 2);

}
