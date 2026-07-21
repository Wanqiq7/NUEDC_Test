import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess

import pytest


SCRIPT = Path(__file__).parents[2] / "scripts" / "deployment_manifest.py"


def load_module():
    spec = importlib.util.spec_from_file_location("deployment_manifest", SCRIPT)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def make_repo(path: Path, proto: bytes, commit_time: str) -> str:
    (path / "shared/proto").mkdir(parents=True)
    (path / "shared/proto/messages.proto").write_bytes(proto)
    (path / ".gitignore").write_text("runtime/deployment_manifest.json\n", encoding="utf-8")
    subprocess.run(["git", "init", "-q", str(path)], check=True)
    subprocess.run(["git", "-C", str(path), "config", "user.name", "Test"], check=True)
    subprocess.run(["git", "-C", str(path), "config", "user.email", "test@example.com"], check=True)
    subprocess.run(["git", "-C", str(path), "add", "."], check=True)
    subprocess.run(
        ["git", "-C", str(path), "commit", "-q", "-m", "fixture"],
        check=True,
        env={"GIT_AUTHOR_DATE": commit_time, "GIT_COMMITTER_DATE": commit_time},
    )
    return subprocess.check_output(
        ["git", "-C", str(path), "rev-parse", "HEAD"], text=True
    ).strip()


def valid_manifest(ground_commit: str, airborne_commit: str, proto: bytes, model: bytes):
    return {
        "schema": "nuedc.deployment.v1",
        "ground_commit": ground_commit,
        "airborne_commit": airborne_commit,
        "protocol_sha256": hashlib.sha256(proto).hexdigest(),
        "model_sha256": hashlib.sha256(model).hexdigest(),
        "created_at_utc": "2026-07-21T00:00:00Z",
    }


def test_create_manifest_hashes_matching_protocol_and_model(tmp_path):
    proto = b'syntax = "proto3";\n'
    model = b"model-weights"
    ground = tmp_path / "ground"
    airborne = tmp_path / "airborne"
    ground_commit = make_repo(ground, proto, "2026-07-20T00:00:00Z")
    airborne_commit = make_repo(airborne, proto, "2026-07-20T00:00:01Z")
    model_path = tmp_path / "model.rknn"
    model_path.write_bytes(model)
    output = tmp_path / "runtime/deployment.json"

    manifest = load_module().create_manifest(
        ground, airborne, model_path, output, "2026-07-21T00:00:00Z"
    )

    assert manifest == valid_manifest(ground_commit, airborne_commit, proto, model)
    assert json.loads(output.read_text(encoding="utf-8")) == manifest
    assert list(output.parent.iterdir()) == [output]


def test_create_rejects_protocol_mismatch(tmp_path):
    ground = tmp_path / "ground"
    airborne = tmp_path / "airborne"
    make_repo(ground, b"ground", "2026-07-20T00:00:00Z")
    make_repo(airborne, b"airborne", "2026-07-20T00:00:01Z")
    model = tmp_path / "model.rknn"
    model.write_bytes(b"model")

    with pytest.raises(ValueError, match="protocol"):
        load_module().create_manifest(ground, airborne, model, tmp_path / "out.json")


def test_create_can_regenerate_ignored_runtime_manifest(tmp_path):
    proto = b"same"
    ground = tmp_path / "ground"
    airborne = tmp_path / "airborne"
    make_repo(ground, proto, "2026-07-20T00:00:00Z")
    make_repo(airborne, proto, "2026-07-20T00:00:01Z")
    model = tmp_path / "model.rknn"
    model.write_bytes(b"model")
    output = ground / "runtime/deployment_manifest.json"

    first = load_module().create_manifest(ground, airborne, model, output)
    second = load_module().create_manifest(ground, airborne, model, output)

    assert second == first
    assert subprocess.check_output(
        ["git", "-C", str(ground), "status", "--porcelain"], text=True
    ) == ""
    (ground / "unexpected.txt").write_text("dirty", encoding="utf-8")
    with pytest.raises(ValueError, match="dirty"):
        load_module().create_manifest(ground, airborne, model, output)


@pytest.mark.parametrize("dirty_role", ["ground", "airborne"])
def test_create_rejects_dirty_repository(tmp_path, dirty_role):
    proto = b"same"
    ground = tmp_path / "ground"
    airborne = tmp_path / "airborne"
    make_repo(ground, proto, "2026-07-20T00:00:00Z")
    make_repo(airborne, proto, "2026-07-20T00:00:01Z")
    (locals()[dirty_role] / "dirty.txt").write_text("dirty", encoding="utf-8")
    model = tmp_path / "model.rknn"
    model.write_bytes(b"model")

    with pytest.raises(ValueError, match="dirty"):
        load_module().create_manifest(ground, airborne, model, tmp_path / "out.json")


def test_verify_ground_accepts_local_commit_and_protocol(tmp_path):
    proto = b"proto"
    model = b"model"
    repo = tmp_path / "ground"
    commit = make_repo(repo, proto, "2026-07-20T00:00:00Z")
    manifest_path = tmp_path / "manifest.json"
    manifest_path.write_text(
        json.dumps(valid_manifest(commit, "b" * 40, proto, model)), encoding="utf-8"
    )

    load_module().verify_manifest(manifest_path, repo, "ground", repo / "shared/proto/messages.proto")


@pytest.mark.parametrize("staged", [False, True])
def test_verify_ground_rejects_tracked_repository_changes(tmp_path, staged):
    proto = b"proto"
    repo = tmp_path / "ground"
    commit = make_repo(repo, proto, "2026-07-20T00:00:00Z")
    manifest_path = tmp_path / "manifest.json"
    manifest_path.write_text(
        json.dumps(valid_manifest(commit, "b" * 40, proto, b"model")), encoding="utf-8"
    )
    (repo / ".gitignore").write_text("runtime/deployment_manifest.json\n# changed\n", encoding="utf-8")
    if staged:
        subprocess.run(["git", "-C", str(repo), "add", ".gitignore"], check=True)

    with pytest.raises(ValueError, match="tracked changes"):
        load_module().verify_manifest(
            manifest_path, repo, "ground", repo / "shared/proto/messages.proto"
        )


def test_verify_ground_ignores_runtime_manifest(tmp_path):
    proto = b"proto"
    repo = tmp_path / "ground"
    commit = make_repo(repo, proto, "2026-07-20T00:00:00Z")
    manifest_path = repo / "runtime/deployment_manifest.json"
    manifest_path.parent.mkdir()
    manifest_path.write_text(
        json.dumps(valid_manifest(commit, "b" * 40, proto, b"model")), encoding="utf-8"
    )

    load_module().verify_manifest(manifest_path, repo, "ground", repo / "shared/proto/messages.proto")


@pytest.mark.parametrize(
    ("mutation", "error"),
    [
        (lambda doc: doc.pop("protocol_sha256"), "missing"),
        (lambda doc: doc.update(schema="wrong"), "schema"),
        (lambda doc: doc.update(ground_commit="c" * 40), "commit"),
        (lambda doc: doc.update(protocol_sha256="0" * 64), "protocol"),
        (lambda doc: doc.update(model_sha256="not-a-hash"), "SHA256"),
    ],
)
def test_verify_ground_fails_closed(tmp_path, mutation, error):
    proto = b"proto"
    repo = tmp_path / "ground"
    commit = make_repo(repo, proto, "2026-07-20T00:00:00Z")
    document = valid_manifest(commit, "b" * 40, proto, b"model")
    mutation(document)
    manifest_path = tmp_path / "manifest.json"
    manifest_path.write_text(json.dumps(document), encoding="utf-8")

    with pytest.raises(ValueError, match=error):
        load_module().verify_manifest(manifest_path, repo, "ground", repo / "shared/proto/messages.proto")


def test_verify_ground_rejects_malformed_json(tmp_path):
    manifest_path = tmp_path / "manifest.json"
    manifest_path.write_text("{bad json", encoding="utf-8")

    with pytest.raises(ValueError, match="JSON"):
        load_module().verify_manifest(manifest_path, tmp_path, "ground", tmp_path / "proto")
