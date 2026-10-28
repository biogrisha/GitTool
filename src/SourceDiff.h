#pragma once

#include "TreeDiff.h"

#include <filesystem>
#include <string>
#include <string_view>

class SourceDiff
{
public:
	std::string apply(
		std::string_view oldSource,
		std::string_view currentSource,
		const DiffInformation& diff) const;

	// Replaces path only when its contents still equal currentSource.
	void applyToFile(
		const std::filesystem::path& path,
		std::string_view oldSource,
		std::string_view currentSource,
		const DiffInformation& diff) const;
};
