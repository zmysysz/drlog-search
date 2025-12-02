#include "boolean_searcher.hpp"
#include <stdexcept>
#include <cctype>
#include <functional>
#include <spdlog/spdlog.h>

namespace drlog {
    // Constructor
    boolean_searcher::boolean_searcher() : base_searcher(SearchType::BOOL) {
    }

    // Destructor
    boolean_searcher::~boolean_searcher() {
        pattern_.reset();
    }

    // Parse the pattern string and build the boolean_node structure
    bool boolean_searcher::build_pattern(const std::string& pattern) {
        // Trim leading and trailing spaces
        auto trim = [](const std::string& s) -> std::string {
            size_t start = s.find_first_not_of(" \t\r\n");
            if (start == std::string::npos) return "";
            size_t end = s.find_last_not_of(" \t\r\n");
            return s.substr(start, end - start + 1);
        };

        // Token structure for parsing
        struct Token {
            enum Type { WORD, AND, OR, NOT, LPAREN, RPAREN } type;
            std::string value;
        };

        // Tokenize the input pattern string
        auto tokenize = [&](const std::string& src, std::vector<Token>& tokens) -> bool {
            size_t i = 0, n = src.size();
            while (i < n) {
                if (isspace(src[i])) { ++i; continue; }
                if (src[i] == '(') { tokens.push_back({Token::LPAREN, "("}); ++i; continue; }
                if (src[i] == ')') { tokens.push_back({Token::RPAREN, ")"}); ++i; continue; }
                // Handle quoted words (single or double quote)
                if (src[i] == '\'' || src[i] == '"') {
                    char quote = src[i];
                    std::string word;
                    ++i;
                    size_t content_start = i;
                    while (i < n) {
                        if (src[i] == '\\' && i + 1 < n) {
                            word += src[i+1];
                            i += 2;
                        } else if (src[i] == quote) {
                            ++i;
                            break;
                        } else {
                            word += src[i++];
                        }
                    }
                    if (i > n || (i == n && src[n-1] != quote)) {
                        throw std::runtime_error("Unmatched right quote in pattern");
                    }
                    if (!word.empty()) {
                        if (!tokens.empty() && tokens.back().type == Token::WORD) {
                            throw std::runtime_error("Multiple adjacent quoted words are not allowed. Use AND OR NOT for logic.");
                        }
                        tokens.push_back({Token::WORD, word});
                    }
                    continue;
                }
                // Handle logic operators
                if ((i+2 < n) && (src.substr(i,3) == "AND") && (i+3==n || isspace(src[i+3]) || src[i+3]=='(' || src[i+3]==')')) {
                    tokens.push_back({Token::AND, "AND"}); i+=3; continue;
                }
                if ((i+1 < n) && (src.substr(i,2) == "OR") && (i+2==n || isspace(src[i+2]) || src[i+2]=='(' || src[i+2]==')')) {
                    tokens.push_back({Token::OR, "OR"}); i+=2; continue;
                }
                if ((i+2 < n) && (src.substr(i,3) == "NOT") && (i+3==n || isspace(src[i+3]) || src[i+3]=='(' || src[i+3]==')')) {
                    tokens.push_back({Token::NOT, "NOT"}); i+=3; continue;
                }
                // Handle unquoted word (single word only, no spaces allowed)
                std::string word;
                while (i < n && !isspace(src[i]) && src[i]!='(' && src[i]!=')' && src[i]!='\'' && src[i]!='"') {
                    if (src[i] == '\\' && i+1 < n) {
                        word += src[i+1];
                        i += 2;
                    } else {
                        word += src[i++];
                    }
                }
                if (!word.empty()) {
                    if (!tokens.empty() && tokens.back().type == Token::WORD) {
                        throw std::runtime_error("Multiple adjacent unquoted words are not allowed. Use quotes for multi-word phrases.");
                    }
                    tokens.push_back({Token::WORD, word});
                }
            }
            return true;
        };

        // Recursive descent parser for boolean_node
        std::function<std::shared_ptr<boolean_node>(std::vector<Token>&, size_t&)> parse_expr;
        parse_expr = [&](std::vector<Token>& tokens, size_t& pos) -> std::shared_ptr<boolean_node> {
            std::vector<std::shared_ptr<boolean_node>> nodes;
            std::vector<boolean_node::Type> ops;
            while (pos < tokens.size()) {
                if (tokens[pos].type == Token::WORD) {
                    nodes.push_back(std::make_shared<boolean_node>(tokens[pos].value));
                    ++pos;
                } else if (tokens[pos].type == Token::NOT) {
                    ++pos;
                    if (pos >= tokens.size()) throw std::runtime_error("NOT must be followed by a word or expression");
                    std::shared_ptr<boolean_node> child;
                    if (tokens[pos].type == Token::WORD) {
                        child = std::make_shared<boolean_node>(tokens[pos].value);
                        ++pos;
                    } else if (tokens[pos].type == Token::LPAREN) {
                        ++pos;
                        child = parse_expr(tokens, pos);
                        if (pos >= tokens.size() || tokens[pos].type != Token::RPAREN)
                            throw std::runtime_error("Parenthesis mismatch after NOT");
                        ++pos;
                    } else {
                        throw std::runtime_error("NOT must be followed by a word or expression");
                    }
                    auto not_node = std::make_shared<boolean_node>(boolean_node::Type::NOT);
                    not_node->children.push_back(child);
                    nodes.push_back(not_node);
                } else if (tokens[pos].type == Token::AND || tokens[pos].type == Token::OR) {
                    boolean_node::Type op = (tokens[pos].type == Token::AND) ? boolean_node::Type::AND : boolean_node::Type::OR;
                    ops.push_back(op);
                    ++pos;
                } else if (tokens[pos].type == Token::LPAREN) {
                    ++pos;
                    auto sub = parse_expr(tokens, pos);
                    if (pos >= tokens.size() || tokens[pos].type != Token::RPAREN)
                        throw std::runtime_error("Parenthesis mismatch");
                    ++pos;
                    nodes.push_back(sub);
                } else if (tokens[pos].type == Token::RPAREN) {
                    break;
                } else {
                    throw std::runtime_error("Unknown token");
                }
            }
            // Combine nodes and ops into a tree
            if (nodes.empty()) return nullptr;
            // If only one node, return it directly (do not wrap with AND)
            if (nodes.size() == 1) return nodes[0];
            boolean_node::Type top_op = boolean_node::Type::AND;
            if (!ops.empty()) {
                for (size_t i = 1; i < ops.size(); ++i) {
                    if (ops[i] != ops[0]) throw std::runtime_error("Logic operators must be the same at the same level, use parentheses for different logic");
                }
                top_op = ops[0];
            }
            auto op_node = std::make_shared<boolean_node>(top_op);
            op_node->children = nodes;
            return op_node;
        };

        try {
            std::string src = trim(pattern);
            std::vector<Token> tokens;
            if (!tokenize(src, tokens)) throw std::runtime_error("Tokenize failed");
            size_t pos = 0;
            auto root = parse_expr(tokens, pos);
            if (pos != tokens.size()) throw std::runtime_error("Parenthesis mismatch or extra content");
            print_search_pattern(pattern,root, 0);
            pattern_ = root;
        } catch (const std::exception& e) {
            pattern_.reset();
            throw std::runtime_error("Failed to build pattern: " + std::string(e.what()));
            return false;
        }
        return true;
    }

    void boolean_searcher::print_search_pattern(const std::string &pattern, std::shared_ptr<boolean_node> node, int depth) {
        if (!node) {
            spdlog::debug("print_search_pattern [{}] null", depth);
            return;
        }

        if(depth == 0) {
            spdlog::debug("print_search_pattern beginning for pattern: {}", pattern);
        }
        
        std::string indent(depth * 2, ' ');
        switch (node->type) {
            case boolean_node::Type::WORD:
                spdlog::debug("print_search_pattern [{}]{}WORD: '{}'", depth, indent, node->word);
                break;
            case boolean_node::Type::NOT:
                spdlog::debug("print_search_pattern [{}]{}NOT:", depth, indent);
                for (const auto& child : node->children) {
                    print_search_pattern(pattern, child, depth + 1);
                }
                break;
            case boolean_node::Type::AND:
                spdlog::debug("print_search_pattern [{}]{}AND:", depth, indent);
                for (const auto& child : node->children) {
                    print_search_pattern(pattern, child, depth + 1);
                }
                break;
            case boolean_node::Type::OR:
                spdlog::debug("print_search_pattern [{}]{}OR:", depth, indent);
                for (const auto& child : node->children) {
                    print_search_pattern(pattern, child, depth + 1);
                }
                break;
            default:
                spdlog::debug("print_search_pattern [{}]{}UNKNOWN", depth, indent);
                break;
        }

        if(depth == 0) {
            spdlog::debug("print_search_pattern ending...");
        }
    }

    bool boolean_searcher::search_line(const std::string& line, std::vector<matched_word>& matched, bool with_res) {
        if(with_res) {
            std::vector<matched_word>().swap(matched); // Clear matched if with_res is true
        }
        // Recursive lambda for evaluating the boolean_node tree
        std::function<bool(std::shared_ptr<boolean_node>, std::vector<matched_word>&)> match_node;
        match_node = [&](std::shared_ptr<boolean_node> node, std::vector<matched_word>& out) -> bool {
            if (!node) return false;
            switch (node->type) {
                case boolean_node::Type::WORD: {
                    size_t pos = line.find(node->word);
                    if (pos != std::string::npos) {
                        if(with_res) {
                            out.emplace_back(node->word, pos);
                        }
                        return true;
                    }
                    return false;
                }
                case boolean_node::Type::NOT: {
                    if (node->children.empty()) return false;
                    // NOT only has one child
                    std::vector<matched_word> tmp;
                    return !match_node(node->children[0], tmp);
                }
                case boolean_node::Type::AND: {
                    std::vector<matched_word> tmp;
                    for (const auto& child : node->children) {
                        std::vector<matched_word> child_matched;
                        if (!match_node(child, child_matched)) {
                            return false;
                        }
                        tmp.insert(tmp.end(), child_matched.begin(), child_matched.end());
                    }
                    out.insert(out.end(), tmp.begin(), tmp.end());
                    return true;
                }
                case boolean_node::Type::OR: {
                    for (const auto& child : node->children) {
                        if (match_node(child, out)) {
                            return true;
                        }
                    }
                    return false;
                }
                default:
                    return false;
            }
        };

        return match_node(pattern_, matched);
    }
} // namespace drlog