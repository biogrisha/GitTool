#pragma once

#include <memory>
#include <string_view>
#include <vector>

struct Term
{
	std::string_view content;
	std::string_view label;
	std::vector<std::unique_ptr<Term>> children;
};

class CodeParser
{
public:
	CodeParser();
	~CodeParser();

	CodeParser(const CodeParser&) = delete;
	CodeParser& operator=(const CodeParser&) = delete;
	CodeParser(CodeParser&&) noexcept;
	CodeParser& operator=(CodeParser&&) noexcept;

	// The returned content and labels refer to code, so code must outlive the tree.
	std::unique_ptr<Term> parse(std::string_view code);

private:
	class Impl;
	std::unique_ptr<Impl> impl_;
};
