#pragma once

#include <string>
#include <vector>

#include "snailtrail/sql/ast.hpp"

namespace snailtrail::sql {

struct AstNode {
    std::string label;
    std::vector<AstNode> children;
};

AstNode describe(const Statement& statement);
AstNode describe(const Expr& expr);

std::string render_tree(const AstNode& root);

std::string dump_ast(const Statement& statement);
std::string dump_ast(const Expr& expr);

}
