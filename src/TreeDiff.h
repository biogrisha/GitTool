#pragma once

#include "CodeParser.h"

#include <cstddef>
#include <memory>
#include <vector>

enum class TermChange
{
	Unchanged,
	Added,
	Deleted,
	Modified
};

struct TermDiff
{
	TermChange change = TermChange::Unchanged;
	// These pointers refer to the input trees passed to TreeDiff::compare.
	const Term* oldTerm = nullptr;
	const Term* currentTerm = nullptr;
	std::vector<std::unique_ptr<TermDiff>> children;
};

struct DiffInformation
{
	std::unique_ptr<TermDiff> root;
	std::size_t added = 0;
	std::size_t deleted = 0;
	std::size_t modified = 0;
	std::size_t unchanged = 0;
};

class TreeDiff
{
public:
	// The input trees must outlive the TermDiff tree in the returned value.
	DiffInformation compare(const Term& oldTree, const Term& currentTree) const;
};
