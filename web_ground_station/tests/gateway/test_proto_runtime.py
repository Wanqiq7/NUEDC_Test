import pytest

from nuedc_web_gateway import proto_runtime
from nuedc_web_gateway.proto_runtime import load_messages_module


def test_loads_generated_envelope():
    messages = load_messages_module()
    envelope = messages.Envelope(sequence=7)

    assert envelope.sequence == 7


def test_missing_generated_proto_fails_without_invoking_protoc(monkeypatch, tmp_path):
    missing = tmp_path / "messages_pb2.py"
    monkeypatch.setattr(proto_runtime, "_GENERATED_MODULE", missing)
    monkeypatch.setattr(proto_runtime, "_GENERATED_HASH", tmp_path / "hash")

    with pytest.raises(RuntimeError, match="generate_python_proto.py"):
        proto_runtime.load_messages_module()


def test_stale_generated_proto_hash_is_rejected(monkeypatch, tmp_path):
    source = tmp_path / "messages.proto"
    generated = tmp_path / "messages_pb2.py"
    digest = tmp_path / "messages.proto.sha256"
    source.write_text("syntax = 'proto3';", encoding="utf-8")
    generated.write_text("VALUE = 1", encoding="utf-8")
    digest.write_text("0" * 64 + "\n", encoding="ascii")
    monkeypatch.setattr(proto_runtime, "_PROTO_SOURCE", source)
    monkeypatch.setattr(proto_runtime, "_GENERATED_MODULE", generated)
    monkeypatch.setattr(proto_runtime, "_GENERATED_HASH", digest)

    with pytest.raises(RuntimeError, match="hash"):
        proto_runtime.load_messages_module()
