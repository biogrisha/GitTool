#include "TreeDiff.h"

#include <algorithm>
#include <cstddef>
#include <functional>
#include <limits>
#include <unordered_map>
#include <utility>

namespace {

	enum class StepKind
	{
		Match,
		Delete,
		Insert
	};

	struct Step
	{
		StepKind kind;
		const Term* oldTerm;
		const Term* currentTerm;
	};

	struct Plan
	{
		std::size_t cost = 0;
		std::vector<Step> steps;
	};

	struct TermPair
	{
		const Term* oldTerm;
		const Term* currentTerm;

		bool operator==(const TermPair& other) const
		{
			return oldTerm == other.oldTerm && currentTerm == other.currentTerm;
		}
	};

	struct TermPairHash
	{
		std::size_t operator()(const TermPair& pair) const
		{
			const auto first = std::hash<const Term*>{}(pair.oldTerm);
			const auto second = std::hash<const Term*>{}(pair.currentTerm);
			return first ^ (second + 0x9e3779b9U + (first << 6U) + (first >> 2U));
		}
	};

	class DiffBuilder
	{
	public:
		DiffInformation build(const Term& oldTree, const Term& currentTree)
		{
			plan(oldTree, currentTree);

			DiffInformation information;
			information.root = makeMatched(oldTree, currentTree);
			countChanges(*information.root, information);
			return information;
		}

	private:
		std::size_t termCount(const Term& term)
		{
			const auto existing = termCounts_.find(&term);
			if (existing != termCounts_.end()) {
				return existing->second;
			}

			std::size_t count = 1;
			for (const auto& child : term.children) {
				count += termCount(*child);
			}
			termCounts_.emplace(&term, count);
			return count;
		}

		const Plan& plan(const Term& oldTerm, const Term& currentTerm)
		{
			const TermPair key{&oldTerm, &currentTerm};
			const auto existing = plans_.find(key);
			if (existing != plans_.end()) {
				return existing->second;
			}

			const std::size_t oldSize = oldTerm.children.size();
			const std::size_t currentSize = currentTerm.children.size();
			const std::size_t columns = currentSize + 1;
			std::vector<std::size_t> costs((oldSize + 1) * columns);
			std::vector<StepKind> choices((oldSize + 1) * columns);

			for (std::size_t oldIndex = 1; oldIndex <= oldSize; ++oldIndex) {
				const std::size_t position = oldIndex * columns;
				costs[position] = costs[position - columns]
					+ termCount(*oldTerm.children[oldIndex - 1]);
				choices[position] = StepKind::Delete;
			}
			for (std::size_t currentIndex = 1;
				currentIndex <= currentSize;
				++currentIndex) {
				costs[currentIndex] = costs[currentIndex - 1]
					+ termCount(*currentTerm.children[currentIndex - 1]);
				choices[currentIndex] = StepKind::Insert;
			}

			for (std::size_t oldIndex = 1; oldIndex <= oldSize; ++oldIndex) {
				for (std::size_t currentIndex = 1;
					currentIndex <= currentSize;
					++currentIndex) {
					const Term& oldChild = *oldTerm.children[oldIndex - 1];
					const Term& currentChild =
						*currentTerm.children[currentIndex - 1];
					const std::size_t position = oldIndex * columns + currentIndex;
					const std::size_t deletion = costs[position - columns]
						+ termCount(oldChild);
					const std::size_t insertion = costs[position - 1]
						+ termCount(currentChild);

					const bool bothLeaves = oldChild.children.empty()
						&& currentChild.children.empty();
					const bool bothBranches = !oldChild.children.empty()
						&& !currentChild.children.empty();
					std::size_t matching = std::numeric_limits<std::size_t>::max();
					if (bothLeaves || bothBranches) {
						matching = costs[position - columns - 1]
							+ plan(oldChild, currentChild).cost;
					}

					costs[position] = std::min({matching, deletion, insertion});
					if (matching == costs[position]) {
						choices[position] = StepKind::Match;
					}
					else if (deletion == costs[position]) {
						choices[position] = StepKind::Delete;
					}
					else {
						choices[position] = StepKind::Insert;
					}
				}
			}

			Plan result;
			result.cost = costs.back() + labelCost(oldTerm, currentTerm);
			std::size_t oldIndex = oldSize;
			std::size_t currentIndex = currentSize;
			while (oldIndex != 0 || currentIndex != 0) {
				const StepKind choice = choices[oldIndex * columns + currentIndex];
				if (choice == StepKind::Match) {
					result.steps.push_back({choice,
						oldTerm.children[oldIndex - 1].get(),
						currentTerm.children[currentIndex - 1].get()});
					--oldIndex;
					--currentIndex;
				}
				else if (choice == StepKind::Delete) {
					result.steps.push_back({choice,
						oldTerm.children[oldIndex - 1].get(), nullptr});
					--oldIndex;
				}
				else {
					result.steps.push_back({choice, nullptr,
						currentTerm.children[currentIndex - 1].get()});
					--currentIndex;
				}
			}
			std::reverse(result.steps.begin(), result.steps.end());

			return plans_.emplace(key, std::move(result)).first->second;
		}

		static std::size_t labelCost(const Term& oldTerm, const Term& currentTerm)
		{
			return oldTerm.label == currentTerm.label ? 0U : 1U;
		}

		std::unique_ptr<TermDiff> makeMatched(
			const Term& oldTerm,
			const Term& currentTerm)
		{
			auto result = std::make_unique<TermDiff>();
			result->oldTerm = &oldTerm;
			result->currentTerm = &currentTerm;

			for (const Step& step : plan(oldTerm, currentTerm).steps) {
				switch (step.kind) {
				case StepKind::Match:
					result->children.push_back(
						makeMatched(*step.oldTerm, *step.currentTerm));
					break;
				case StepKind::Delete:
					result->children.push_back(makeDeleted(*step.oldTerm));
					break;
				case StepKind::Insert:
					result->children.push_back(makeAdded(*step.currentTerm));
					break;
				}
			}

			result->change = labelCost(oldTerm, currentTerm) == 0
				&& std::all_of(
					result->children.begin(),
					result->children.end(),
					[](const auto& child) {
						return child->change == TermChange::Unchanged;
					})
				? TermChange::Unchanged
				: TermChange::Modified;
			return result;
		}

		static std::unique_ptr<TermDiff> makeDeleted(const Term& term)
		{
			auto result = std::make_unique<TermDiff>();
			result->change = TermChange::Deleted;
			result->oldTerm = &term;
			for (const auto& child : term.children) {
				result->children.push_back(makeDeleted(*child));
			}
			return result;
		}

		static std::unique_ptr<TermDiff> makeAdded(const Term& term)
		{
			auto result = std::make_unique<TermDiff>();
			result->change = TermChange::Added;
			result->currentTerm = &term;
			for (const auto& child : term.children) {
				result->children.push_back(makeAdded(*child));
			}
			return result;
		}

		static void countChanges(
			const TermDiff& term,
			DiffInformation& information)
		{
			switch (term.change) {
			case TermChange::Added:
				++information.added;
				break;
			case TermChange::Deleted:
				++information.deleted;
				break;
			case TermChange::Modified:
				++information.modified;
				break;
			case TermChange::Unchanged:
				++information.unchanged;
				break;
			}
			for (const auto& child : term.children) {
				countChanges(*child, information);
			}
		}

		std::unordered_map<TermPair, Plan, TermPairHash> plans_;
		std::unordered_map<const Term*, std::size_t> termCounts_;
	};

} // namespace

DiffInformation TreeDiff::compare(
	const Term& oldTree,
	const Term& currentTree) const
{
	return DiffBuilder().build(oldTree, currentTree);
}
