#include <git2.h>

#include "CodeParser.h"
#include "SourceDiff.h"
#include "TreeDiff.h"

#include <cstddef>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

namespace {

	template <typename T, void (*Free)(T*)>
	using GitPtr = std::unique_ptr<T, decltype(Free)>;

	using Repository = GitPtr<git_repository, git_repository_free>;
	using Object = GitPtr<git_object, git_object_free>;
	using Commit = GitPtr<git_commit, git_commit_free>;
	using Tree = GitPtr<git_tree, git_tree_free>;
	using Diff = GitPtr<git_diff, git_diff_free>;
	using Blob = GitPtr<git_blob, git_blob_free>;

	class GitLibrary {
	public:
		GitLibrary() { git_libgit2_init(); }
		~GitLibrary() { git_libgit2_shutdown(); }
	};

	std::runtime_error gitError(const std::string& operation)
	{
		const git_error* error = git_error_last();
		return std::runtime_error(
			operation + (error != nullptr && error->message != nullptr
				? ": " + std::string(error->message)
				: ""));
	}

	Commit resolveCommit(git_repository* repository, const std::string& revision)
	{
		git_object* rawObject = nullptr;
		if (git_revparse_single(&rawObject, repository, revision.c_str()) < 0) {
			throw gitError("Cannot resolve " + revision);
		}
		Object object(rawObject, git_object_free);

		git_object* rawCommit = nullptr;
		if (git_object_peel(&rawCommit, object.get(), GIT_OBJECT_COMMIT) < 0) {
			throw gitError("Revision is not a commit");
		}
		return Commit(reinterpret_cast<git_commit*>(rawCommit), git_commit_free);
	}

	Tree getTree(git_commit* commit)
	{
		git_tree* rawTree = nullptr;
		if (git_commit_tree(&rawTree, commit) < 0) {
			throw gitError("Cannot read commit tree");
		}
		return Tree(rawTree, git_tree_free);
	}

	std::string readFile(git_repository* repository, git_tree* tree, const char* path)
	{
		if (path == nullptr) {
			return {};
		}

		git_tree_entry* rawEntry = nullptr;
		const int entryResult = git_tree_entry_bypath(&rawEntry, tree, path);
		if (entryResult == GIT_ENOTFOUND) {
			return {}; // The file does not exist in this commit.
		}
		if (entryResult < 0) {
			throw gitError("Cannot find " + std::string(path));
		}
		GitPtr<git_tree_entry, git_tree_entry_free> entry(
			rawEntry, git_tree_entry_free);

		if (git_tree_entry_type(entry.get()) != GIT_OBJECT_BLOB) {
			return {}; // Ignore directories and submodules.
		}

		git_blob* rawBlob = nullptr;
		if (git_blob_lookup(
			&rawBlob, repository, git_tree_entry_id(entry.get())) < 0) {
			throw gitError("Cannot read " + std::string(path));
		}
		Blob blob(rawBlob, git_blob_free);

		const std::size_t size = static_cast<std::size_t>(git_blob_rawsize(blob.get()));
		if (size == 0) {
			return {};
		}
		const auto* data = static_cast<const char*>(git_blob_rawcontent(blob.get()));
		return std::string(data, size);
	}

} // namespace

int main(int argc, char* argv[])
{
	if (argc != 2) {
		std::cerr << "Usage: gittool.exe <older-commit>\n";
		return 2;
	}
	const std::string commit = argv[1];

	GitLibrary gitLibrary;

	try {
		git_repository* rawRepository = nullptr;
		if (git_repository_open_ext(&rawRepository, ".", 0, nullptr) < 0) {
			throw gitError("Cannot find a repository from the current directory");
		}
		Repository repository(rawRepository, git_repository_free);
		const char* workDirectory = git_repository_workdir(repository.get());
		if (workDirectory == nullptr) {
			throw std::runtime_error("Cannot modify files in a bare repository");
		}
		const std::filesystem::path repositoryPath = workDirectory;

		Commit oldCommit = resolveCommit(repository.get(), commit);
		Commit currentCommit = resolveCommit(repository.get(), "HEAD");
		Tree oldTree = getTree(oldCommit.get());
		Tree currentTree = getTree(currentCommit.get());

		git_diff* rawDiff = nullptr;
		if (git_diff_tree_to_tree(
			&rawDiff,
			repository.get(),
			oldTree.get(),
			currentTree.get(),
			nullptr) < 0) {
			throw gitError("Cannot calculate diff");
		}
		Diff diff(rawDiff, git_diff_free);

		// Make renamed files appear as one changed file instead of delete + add.
		if (git_diff_find_similar(diff.get(), nullptr) < 0) {
			throw gitError("Cannot detect renamed files");
		}

		CodeParser codeParser;
		SourceDiff sourceDiff;
		TreeDiff treeDiff;

		for (std::size_t index = 0;
			index < git_diff_num_deltas(diff.get());
			++index) {
			const git_diff_delta* changedFile =
				git_diff_get_delta(diff.get(), index);

			const char* oldPath = changedFile->old_file.path;
			const char* currentPath = changedFile->new_file.path;
			const char* filePath =
				changedFile->status == GIT_DELTA_DELETED ? oldPath : currentPath;

			// These contain the complete file contents, not merely diff hunks.
			const std::string old =
				readFile(repository.get(), oldTree.get(), oldPath);
			const std::string current =
				readFile(repository.get(), currentTree.get(), currentPath);

			std::cout << filePath << '\n';
			std::cout << "  old bytes: " << old.size() << '\n';
			std::cout << "  current bytes: " << current.size() << '\n';

			const std::unique_ptr<Term> oldCodeTree = codeParser.parse(old);
			const std::unique_ptr<Term> currentCodeTree = codeParser.parse(current);
			const DiffInformation codeDiff =
				treeDiff.compare(*oldCodeTree, *currentCodeTree);

			std::cout << "  old top-level terms: "
				<< oldCodeTree->children.size() << '\n';
			std::cout << "  current top-level terms: "
				<< currentCodeTree->children.size() << '\n';
			std::cout << "  added terms: " << codeDiff.added << '\n';
			std::cout << "  deleted terms: " << codeDiff.deleted << '\n';
			std::cout << "  modified terms: " << codeDiff.modified << '\n';

			if (changedFile->status != GIT_DELTA_DELETED) {
				sourceDiff.applyToFile(
					repositoryPath / currentPath,
					old,
					current,
					codeDiff);
			}
		}

		return 0;
	}
	catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
