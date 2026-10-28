#include "SourceDiff.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {

	struct OldAnnotation
	{
		std::size_t start;
		std::size_t end;
	};

	struct SourceRange
	{
		std::size_t oldStart;
		std::size_t oldEnd;
		std::size_t currentStart;
		std::size_t currentEnd;
		std::vector<OldAnnotation> annotations;
	};

	std::size_t viewOffset(std::string_view source, std::string_view view)
	{
		const std::uintptr_t sourceStart =
			reinterpret_cast<std::uintptr_t>(source.data());
		const std::uintptr_t sourceEnd = sourceStart + source.size();
		const std::uintptr_t viewStart =
			reinterpret_cast<std::uintptr_t>(view.data());
		if (viewStart < sourceStart
			|| viewStart > sourceEnd
			|| view.size() > sourceEnd - viewStart) {
			throw std::invalid_argument("Term content does not refer to its source");
		}
		return static_cast<std::size_t>(viewStart - sourceStart);
	}

	std::pair<std::size_t, std::size_t> termRange(
		std::string_view source,
		const Term& term)
	{
		const std::size_t start = viewOffset(source, term.content);
		return {start, start + term.content.size()};
	}

	std::size_t lineStart(std::string_view source, std::size_t position)
	{
		if (position == 0) {
			return 0;
		}
		const std::size_t newline = source.rfind('\n', position - 1);
		return newline == std::string_view::npos ? 0 : newline + 1;
	}

	std::size_t lineEnd(std::string_view source, std::size_t position)
	{
		if (position == 0 || source.empty()) {
			return position;
		}
		const std::size_t newline = source.find('\n', position - 1);
		return newline == std::string_view::npos ? source.size() : newline + 1;
	}

	std::pair<std::size_t, std::size_t> completeLines(
		std::string_view source,
		std::size_t start,
		std::size_t end)
	{
		if (start == end) {
			const std::size_t anchor = lineStart(source, start);
			return {anchor, anchor};
		}
		return {lineStart(source, start), lineEnd(source, end)};
	}

	const Term* nextOldTerm(
		const TermDiff& parent,
		std::size_t childIndex)
	{
		for (std::size_t index = childIndex + 1;
			index < parent.children.size();
			++index) {
			if (parent.children[index]->oldTerm != nullptr) {
				return parent.children[index]->oldTerm;
			}
		}
		return nullptr;
	}

	const Term* nextCurrentTerm(
		const TermDiff& parent,
		std::size_t childIndex)
	{
		for (std::size_t index = childIndex + 1;
			index < parent.children.size();
			++index) {
			if (parent.children[index]->currentTerm != nullptr) {
				return parent.children[index]->currentTerm;
			}
		}
		return nullptr;
	}

	class RangeCollector
	{
	public:
		RangeCollector(std::string_view oldSource, std::string_view currentSource)
			: oldSource_(oldSource), currentSource_(currentSource)
		{
		}

		std::vector<SourceRange> collect(const TermDiff& root)
		{
			collect(root, 0, 0);
			for (SourceRange& range : ranges_) {
				const auto oldLines = completeLines(
					oldSource_, range.oldStart, range.oldEnd);
				const auto currentLines = completeLines(
					currentSource_, range.currentStart, range.currentEnd);
				range.oldStart = oldLines.first;
				range.oldEnd = oldLines.second;
				range.currentStart = currentLines.first;
				range.currentEnd = currentLines.second;
			}

			std::stable_sort(
				ranges_.begin(),
				ranges_.end(),
				[](const SourceRange& left, const SourceRange& right) {
					return std::pair(left.currentStart, left.oldStart)
						< std::pair(right.currentStart, right.oldStart);
				});

			std::vector<SourceRange> merged;
			for (const SourceRange& range : ranges_) {
				if (!merged.empty()
					&& (range.currentStart <= merged.back().currentEnd
						|| range.oldStart <= merged.back().oldEnd)) {
					merged.back().oldEnd = std::max(merged.back().oldEnd, range.oldEnd);
					merged.back().currentEnd =
						std::max(merged.back().currentEnd, range.currentEnd);
					merged.back().annotations.insert(
						merged.back().annotations.end(),
						range.annotations.begin(),
						range.annotations.end());
				}
				else {
					merged.push_back(range);
				}
			}
			for (SourceRange& range : merged) {
				addSimilarities(root, range);
			}
			return merged;
		}

	private:
		void collect(
			const TermDiff& term,
			std::size_t oldAnchor,
			std::size_t currentAnchor)
		{
			if (term.change == TermChange::Unchanged) {
				return;
			}
			if (term.change == TermChange::Added) {
				const auto current = termRange(currentSource_, *term.currentTerm);
				ranges_.push_back(
					{oldAnchor, oldAnchor, current.first, current.second, {}});
				return;
			}
			if (term.change == TermChange::Deleted) {
				const auto old = termRange(oldSource_, *term.oldTerm);
				SourceRange range{
					old.first, old.second, currentAnchor, currentAnchor, {}};
				ranges_.push_back(std::move(range));
				return;
			}
			if (term.children.empty()) {
				const auto old = termRange(oldSource_, *term.oldTerm);
				const auto current = termRange(currentSource_, *term.currentTerm);
				SourceRange range{
					old.first, old.second, current.first, current.second, {}};
				ranges_.push_back(std::move(range));
				return;
			}

			const auto oldParent = termRange(oldSource_, *term.oldTerm);
			const auto currentParent = termRange(currentSource_, *term.currentTerm);
			for (std::size_t index = 0; index < term.children.size(); ++index) {
				const TermDiff& child = *term.children[index];
				std::size_t childOldAnchor = oldParent.second;
				if (const Term* next = nextOldTerm(term, index)) {
					childOldAnchor = termRange(oldSource_, *next).first;
				}
				std::size_t childCurrentAnchor = currentParent.second;
				if (const Term* next = nextCurrentTerm(term, index)) {
					childCurrentAnchor = termRange(currentSource_, *next).first;
				}
				collect(child, childOldAnchor, childCurrentAnchor);
			}
		}

		void collectTerminals(
			const TermDiff& diff,
			bool useOldTerm,
			std::size_t rangeStart,
			std::size_t rangeEnd,
			std::vector<const Term*>& terminals) const
		{
			const Term* term = useOldTerm ? diff.oldTerm : diff.currentTerm;
			if (term != nullptr && term->children.empty() && !term->label.empty()) {
				const auto range = termRange(
					useOldTerm ? oldSource_ : currentSource_, *term);
				if (range.first < rangeEnd && range.second > rangeStart) {
					terminals.push_back(term);
				}
				return;
			}
			for (const auto& child : diff.children) {
				collectTerminals(
					*child, useOldTerm, rangeStart, rangeEnd, terminals);
			}
		}

		void addSimilarities(const TermDiff& root, SourceRange& range) const
		{
			std::vector<const Term*> oldTerms;
			std::vector<const Term*> currentTerms;
			collectTerminals(
				root, true, range.oldStart, range.oldEnd, oldTerms);
			collectTerminals(
				root,
				false,
				range.currentStart,
				range.currentEnd,
				currentTerms);

			const std::size_t columns = currentTerms.size() + 1;
			std::vector<std::size_t> lengths(
				(oldTerms.size() + 1) * columns);
			for (std::size_t oldIndex = 1;
				oldIndex <= oldTerms.size();
				++oldIndex) {
				for (std::size_t currentIndex = 1;
					currentIndex <= currentTerms.size();
					++currentIndex) {
					const std::size_t position = oldIndex * columns + currentIndex;
					if (oldTerms[oldIndex - 1]->label
						== currentTerms[currentIndex - 1]->label) {
						lengths[position] =
							lengths[position - columns - 1] + 1;
					}
					else {
						lengths[position] = std::max(
							lengths[position - columns], lengths[position - 1]);
					}
				}
			}

			std::size_t oldIndex = oldTerms.size();
			std::size_t currentIndex = currentTerms.size();
			while (oldIndex != 0 && currentIndex != 0) {
				if (oldTerms[oldIndex - 1]->label
					== currentTerms[currentIndex - 1]->label) {
					const auto oldRange =
						termRange(oldSource_, *oldTerms[oldIndex - 1]);
					range.annotations.push_back({oldRange.first, oldRange.second});
					--oldIndex;
					--currentIndex;
				}
				else if (lengths[(oldIndex - 1) * columns + currentIndex]
					>= lengths[oldIndex * columns + currentIndex - 1]) {
					--oldIndex;
				}
				else {
					--currentIndex;
				}
			}
			std::sort(
				range.annotations.begin(),
				range.annotations.end(),
				[](const OldAnnotation& left, const OldAnnotation& right) {
					return left.start < right.start;
				});
		}

		std::string_view oldSource_;
		std::string_view currentSource_;
		std::vector<SourceRange> ranges_;
	};

	void appendCommented(
		std::string& output,
		std::string_view source,
		std::size_t start,
		std::size_t rangeEnd,
		const std::vector<OldAnnotation>& annotations)
	{
		std::size_t position = start;
		while (position < rangeEnd) {
			const std::size_t newline = source.find('\n', position);
			const std::size_t sourceLineEnd = newline == std::string_view::npos
				? source.size()
				: newline + 1;
			const std::size_t lineEnd = std::min(rangeEnd, sourceLineEnd);
			output += "//";
			output.append(source.substr(position, lineEnd - position));
			if (lineEnd == 0 || source[lineEnd - 1] != '\n') {
				output += '\n';
			}

			const std::size_t contentEnd =
				lineEnd > position && source[lineEnd - 1] == '\n'
				? lineEnd - 1
				: lineEnd;
			std::string underline;
			underline.reserve(contentEnd - position);
			for (std::size_t index = position; index < contentEnd; ++index) {
				underline += source[index] == '\t' ? '\t' : ' ';
			}
			for (const OldAnnotation& annotation : annotations) {
				const std::size_t annotationStart =
					std::max(position, annotation.start);
				const std::size_t annotationEnd =
					std::min(contentEnd, annotation.end);
				if (annotationStart >= annotationEnd) {
					continue;
				}
				for (std::size_t index = annotationStart;
					index < annotationEnd;
					++index) {
					underline[index - position] = '^';
				}
			}
			const std::size_t lastCaret = underline.rfind('^');
			if (lastCaret != std::string::npos) {
				output += "//";
				output.append(underline, 0, lastCaret + 1);
				output += '\n';
			}
			position = lineEnd;
		}
	}

	std::string normalizeLineEndings(std::string_view source)
	{
		std::string result;
		result.reserve(source.size());
		for (std::size_t index = 0; index < source.size(); ++index) {
			if (source[index] == '\r'
				&& index + 1 < source.size()
				&& source[index + 1] == '\n') {
				continue;
			}
			result += source[index];
		}
		return result;
	}

	bool usesCrlf(std::string_view source)
	{
		return source.find("\r\n") != std::string_view::npos;
	}

	std::string useLineEndings(std::string_view source, bool crlf)
	{
		std::string normalized = normalizeLineEndings(source);
		if (!crlf) {
			return normalized;
		}

		std::string result;
		result.reserve(normalized.size() + normalized.size() / 20);
		for (const char character : normalized) {
			if (character == '\n') {
				result += '\r';
			}
			result += character;
		}
		return result;
	}

} // namespace

std::string SourceDiff::apply(
	std::string_view oldSource,
	std::string_view currentSource,
	const DiffInformation& diff) const
{
	if (diff.root == nullptr) {
		throw std::invalid_argument("Diff information has no root");
	}

	const std::vector<SourceRange> ranges =
		RangeCollector(oldSource, currentSource).collect(*diff.root);
	std::string output;
	output.reserve(currentSource.size());
	std::size_t currentPosition = 0;
	for (const SourceRange& range : ranges) {
		output.append(currentSource.substr(
			currentPosition, range.currentStart - currentPosition));

		const bool hasOld = range.oldStart != range.oldEnd;
		const bool hasCurrent = range.currentStart != range.currentEnd;
		if (hasOld && hasCurrent) {
			output += "//<<<replaced\n";
			appendCommented(
				output,
				oldSource,
				range.oldStart,
				range.oldEnd,
				range.annotations);
			output += "//<<<\n";
		}
		else if (hasOld) {
			output += "//<<<deleted\n";
			appendCommented(
				output,
				oldSource,
				range.oldStart,
				range.oldEnd,
				range.annotations);
		}
		else {
			output += "//<<<added\n";
		}

		const std::string_view currentPart = currentSource.substr(
			range.currentStart, range.currentEnd - range.currentStart);
		output.append(currentPart);
		if (!currentPart.empty() && currentPart.back() != '\n') {
			output += '\n';
		}
		output += "//<<<\n";
		currentPosition = range.currentEnd;
	}
	output.append(currentSource.substr(currentPosition));
	return output;
}

void SourceDiff::applyToFile(
	const std::filesystem::path& path,
	std::string_view oldSource,
	std::string_view currentSource,
	const DiffInformation& diff) const
{
	std::ifstream input(path, std::ios::binary);
	if (!input) {
		throw std::runtime_error("Cannot open source file: " + path.string());
	}
	const std::string fileContents{
		std::istreambuf_iterator<char>(input),
		std::istreambuf_iterator<char>()};
	const bool exactMatch = std::string_view(fileContents) == currentSource;
	if (!exactMatch
		&& normalizeLineEndings(fileContents)
			!= normalizeLineEndings(currentSource)) {
		throw std::runtime_error(
			"Source file changed after parsing: " + path.string());
	}
	input.close();

	std::string result = apply(oldSource, currentSource, diff);
	if (!exactMatch) {
		result = useLineEndings(result, usesCrlf(fileContents));
	}
	std::ofstream output(path, std::ios::binary | std::ios::trunc);
	if (!output || !output.write(result.data(), result.size())) {
		throw std::runtime_error("Cannot write source file: " + path.string());
	}
}
