#include "CodeParser.h"

#include <tree_sitter/api.h>

#include <cstdint>
#include <limits>
#include <stdexcept>
#include <utility>

extern "C" const TSLanguage* tree_sitter_cpp();

namespace {

	std::unique_ptr<Term> makeTerm(TSNode node, std::string_view code)
	{
		const std::uint32_t start = ts_node_start_byte(node);
		const std::uint32_t end = ts_node_end_byte(node);

		auto term = std::make_unique<Term>();
		term->content = code.substr(start, end - start);

		const std::uint32_t childCount = ts_node_child_count(node);
		if (childCount == 0) {
			term->label = term->content;
		}

		term->children.reserve(childCount);
		for (std::uint32_t index = 0; index < childCount; ++index) {
			term->children.push_back(
				makeTerm(ts_node_child(node, index), code));
		}

		return term;
	}

} // namespace

class CodeParser::Impl
{
public:
	Impl()
		: parser_(ts_parser_new())
	{
		if (parser_ == nullptr) {
			throw std::runtime_error("Cannot create Tree-sitter parser");
		}
		if (!ts_parser_set_language(parser_, tree_sitter_cpp())) {
			ts_parser_delete(parser_);
			parser_ = nullptr;
			throw std::runtime_error("Cannot load the Tree-sitter C++ grammar");
		}
	}

	~Impl()
	{
		ts_parser_delete(parser_);
	}

	std::unique_ptr<Term> parse(std::string_view code)
	{
		if (code.size() > std::numeric_limits<std::uint32_t>::max()) {
			throw std::length_error("Source code is too large for Tree-sitter");
		}

		TSTree* tree = ts_parser_parse_string(
			parser_, nullptr, code.data(), static_cast<std::uint32_t>(code.size()));
		if (tree == nullptr) {
			throw std::runtime_error("Tree-sitter could not parse the source code");
		}

		try {
			auto root = makeTerm(ts_tree_root_node(tree), code);
			ts_tree_delete(tree);
			return root;
		}
		catch (...) {
			ts_tree_delete(tree);
			throw;
		}
	}

private:
	TSParser* parser_;
};

CodeParser::CodeParser()
	: impl_(std::make_unique<Impl>())
{
}

CodeParser::~CodeParser() = default;
CodeParser::CodeParser(CodeParser&&) noexcept = default;
CodeParser& CodeParser::operator=(CodeParser&&) noexcept = default;

std::unique_ptr<Term> CodeParser::parse(std::string_view code)
{
	return impl_->parse(code);
}
