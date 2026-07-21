#!/usr/bin/env python3
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile


SCHEMA = "nuedc.deployment.v1"
FIELDS = {
    "schema",
    "ground_commit",
    "airborne_commit",
    "protocol_sha256",
    "model_sha256",
    "created_at_utc",
}
COMMIT_PATTERN = re.compile(r"[0-9a-f]{40}\Z")
SHA256_PATTERN = re.compile(r"[0-9a-f]{64}\Z")


def sha256_file(path):
    digest = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def git_output(repo, *arguments):
    try:
        return subprocess.check_output(
            ["git", "-C", str(repo), *arguments], stderr=subprocess.STDOUT, text=True
        ).strip()
    except (OSError, subprocess.CalledProcessError) as error:
        raise ValueError(f"git command failed for repository {repo}") from error


def repository_commit(repo):
    commit = git_output(repo, "rev-parse", "HEAD")
    if not COMMIT_PATTERN.fullmatch(commit):
        raise ValueError(f"invalid commit for repository {repo}")
    return commit


def require_clean_repository(repo):
    if git_output(repo, "status", "--porcelain"):
        raise ValueError(f"dirty repository rejected: {repo}")


def require_no_tracked_changes(repo):
    for arguments in (("diff", "--quiet"), ("diff", "--cached", "--quiet")):
        try:
            subprocess.run(
                ["git", "-C", str(repo), *arguments],
                check=True,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
            )
        except OSError as error:
            raise ValueError(f"git command failed for repository {repo}") from error
        except subprocess.CalledProcessError as error:
            if error.returncode == 1:
                raise ValueError(f"tracked changes rejected: {repo}") from error
            raise ValueError(f"git command failed for repository {repo}") from error


def validate_document(document):
    if not isinstance(document, dict):
        raise ValueError("manifest JSON must be an object")
    missing = FIELDS - document.keys()
    extra = document.keys() - FIELDS
    if missing:
        raise ValueError(f"missing manifest keys: {', '.join(sorted(missing))}")
    if extra:
        raise ValueError(f"unexpected manifest keys: {', '.join(sorted(extra))}")
    if document["schema"] != SCHEMA:
        raise ValueError("invalid deployment manifest schema")
    for key in ("ground_commit", "airborne_commit"):
        if not isinstance(document[key], str) or not COMMIT_PATTERN.fullmatch(document[key]):
            raise ValueError(f"invalid commit field: {key}")
    for key in ("protocol_sha256", "model_sha256"):
        if not isinstance(document[key], str) or not SHA256_PATTERN.fullmatch(document[key]):
            raise ValueError(f"invalid SHA256 field: {key}")
    if not isinstance(document["created_at_utc"], str):
        raise ValueError("created_at_utc must be a string")


def read_manifest(path):
    try:
        document = json.loads(Path(path).read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise ValueError(f"invalid manifest JSON: {error}") from error
    validate_document(document)
    return document


def atomic_write_json(path, document):
    output = Path(path)
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary_path = None
    try:
        with tempfile.NamedTemporaryFile(
            mode="w", encoding="utf-8", dir=output.parent, prefix=f".{output.name}.", delete=False
        ) as stream:
            temporary_path = Path(stream.name)
            json.dump(document, stream, indent=2, sort_keys=True)
            stream.write("\n")
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary_path, output)
        temporary_path = None
        directory_fd = os.open(output.parent, os.O_RDONLY)
        try:
            os.fsync(directory_fd)
        finally:
            os.close(directory_fd)
    finally:
        if temporary_path is not None:
            temporary_path.unlink(missing_ok=True)


def create_manifest(ground_repo, airborne_repo, model, output, created_at_utc=None):
    ground_repo = Path(ground_repo)
    airborne_repo = Path(airborne_repo)
    require_clean_repository(ground_repo)
    require_clean_repository(airborne_repo)
    ground_protocol = sha256_file(ground_repo / "shared/proto/messages.proto")
    airborne_protocol = sha256_file(airborne_repo / "shared/proto/messages.proto")
    if ground_protocol != airborne_protocol:
        raise ValueError("protocol files do not match")
    document = {
        "schema": SCHEMA,
        "ground_commit": repository_commit(ground_repo),
        "airborne_commit": repository_commit(airborne_repo),
        "protocol_sha256": ground_protocol,
        "model_sha256": sha256_file(model),
        "created_at_utc": created_at_utc
        or datetime.now(timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z"),
    }
    validate_document(document)
    atomic_write_json(output, document)
    return document


def verify_manifest(manifest, repo, role, proto):
    document = read_manifest(manifest)
    require_no_tracked_changes(repo)
    expected_commit = document[f"{role}_commit"]
    if repository_commit(repo) != expected_commit:
        raise ValueError(f"local {role} commit does not match deployment manifest")
    if sha256_file(proto) != document["protocol_sha256"]:
        raise ValueError("local protocol does not match deployment manifest")


def parse_args():
    parser = argparse.ArgumentParser()
    subparsers = parser.add_subparsers(dest="command", required=True)
    create = subparsers.add_parser("create")
    create.add_argument("--ground-repo", required=True)
    create.add_argument("--airborne-repo", required=True)
    create.add_argument("--model", required=True)
    create.add_argument("--output", required=True)
    verify = subparsers.add_parser("verify")
    verify.add_argument("--manifest", required=True)
    verify.add_argument("--repo", required=True)
    verify.add_argument("--role", choices=("ground", "airborne"), required=True)
    verify.add_argument("--proto", required=True)
    return parser.parse_args()


def main():
    args = parse_args()
    try:
        if args.command == "create":
            create_manifest(args.ground_repo, args.airborne_repo, args.model, args.output)
        else:
            verify_manifest(args.manifest, args.repo, args.role, args.proto)
    except (OSError, ValueError) as error:
        print(f"deployment manifest error: {error}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
