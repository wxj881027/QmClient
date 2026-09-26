#!/usr/bin/env python3
"""Generate a review ledger for an upstream synchronization range."""

import argparse
import csv
import subprocess
from pathlib import Path


def run_git(*args: str) -> str:
	return subprocess.run(
		["git", *args],
		check=True,
		stdout=subprocess.PIPE,
		stderr=subprocess.PIPE,
		text=True,
	).stdout


def verify_revision(revision: str) -> str:
	return run_git("rev-parse", "--verify", f"{revision}^{{commit}}").strip()


def patch_equivalence(local: str, upstream: str, base: str) -> dict[str, str]:
	result: dict[str, str] = {}
	for line in run_git("cherry", "-v", local, upstream, base).splitlines():
		if len(line) < 43 or line[0] not in "+-" or line[1] != " ":
			raise RuntimeError(f"Unexpected git cherry output: {line}")
		result[line[2:42]] = "exact-patch-equivalent" if line[0] == "-" else "candidate"
	return result


def upstream_commits(base: str, upstream: str) -> list[tuple[str, str, str, str]]:
	raw = run_git("log", "--reverse", "--format=%H%x00%ad%x00%P%x00%s%x00", "--date=short", f"{base}..{upstream}")
	raw = raw.removesuffix("\n")
	fields = raw.split("\0")
	if fields[-1] == "":
		fields.pop()
	if len(fields) % 4 != 0:
		raise RuntimeError("Unexpected git log output")
	return [(fields[index].lstrip("\n"), fields[index + 1], fields[index + 2], fields[index + 3]) for index in range(0, len(fields), 4)]


def existing_reviews(path: Path) -> dict[str, list[str]]:
	if not path.exists():
		return {}
	with path.open("r", encoding="utf-8", newline="") as input_file:
		rows = csv.reader(input_file, delimiter="\t")
		next(rows, None)
		next(rows, None)
		reviews: dict[str, list[str]] = {}
		for line_number, row in enumerate(rows, start=3):
			if len(row) != 9:
				raise RuntimeError(f"Malformed review ledger row {line_number}: expected 9 columns, got {len(row)}")
			reviews[row[0]] = row[4:9]
		return reviews


def main() -> None:
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--local", default="HEAD", help="Local QmClient revision to compare")
	parser.add_argument("--upstream", default="ddnet-official/master", help="Upstream revision to compare")
	parser.add_argument("--base", required=True, help="Exclusive upstream range base, such as 20.0")
	parser.add_argument("--output", required=True, type=Path, help="Versioned TSV ledger path")
	args = parser.parse_args()

	local = verify_revision(args.local)
	upstream = verify_revision(args.upstream)
	base = verify_revision(args.base)
	equivalence = patch_equivalence(local, upstream, base)
	commits = upstream_commits(base, upstream)
	non_merge_commits = sum(len(parents.split()) <= 1 for _, _, parents, _ in commits)
	if non_merge_commits != len(equivalence):
		raise RuntimeError("git cherry and the non-merge upstream commits disagree about the selected range")
	reviews = existing_reviews(args.output)

	args.output.parent.mkdir(parents=True, exist_ok=True)
	with args.output.open("w", encoding="utf-8", newline="") as output:
		writer = csv.writer(output, delimiter="\t", lineterminator="\n")
		writer.writerow(["upstream_base", base, "upstream_head", upstream, "local_head", local])
		writer.writerow(["commit", "date", "subject", "patch_equivalence", "disposition", "module", "dependencies", "notes", "validation"])
		for commit, date, parents, subject in commits:
			status = equivalence.get(commit)
			if status is None:
				status = "merge-commit" if len(parents.split()) > 1 else "unclassified"
			writer.writerow([commit, date, subject, status, *reviews.get(commit, ["unreviewed", "", "", "", ""])])


if __name__ == "__main__":
	main()
