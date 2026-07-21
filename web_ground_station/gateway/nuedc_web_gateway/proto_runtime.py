import hashlib
import importlib.util
import sys
from pathlib import Path
from types import ModuleType


_REPO_ROOT = Path(__file__).resolve().parents[3]
_PROTO_SOURCE = _REPO_ROOT / "shared/proto/messages.proto"
_GENERATED_MODULE = (
    _REPO_ROOT / "web_ground_station/.generated/proto/messages_pb2.py"
)
_GENERATED_HASH = _GENERATED_MODULE.with_name("messages.proto.sha256")


def _validate_generated() -> None:
    if not _GENERATED_MODULE.is_file() or not _GENERATED_HASH.is_file():
        raise RuntimeError(
            "generated protobuf is missing; run "
            "web_ground_station/scripts/generate_python_proto.py"
        )
    expected = hashlib.sha256(_PROTO_SOURCE.read_bytes()).hexdigest()
    actual = _GENERATED_HASH.read_text(encoding="ascii").strip()
    if actual != expected:
        raise RuntimeError(
            "generated protobuf hash does not match shared/proto/messages.proto"
        )


def load_messages_module() -> ModuleType:
    _validate_generated()
    module_name = "nuedc_web_gateway.generated.messages_pb2"
    spec = importlib.util.spec_from_file_location(module_name, _GENERATED_MODULE)
    if spec is None or spec.loader is None:
        raise ImportError(f"cannot load generated protobuf module: {_GENERATED_MODULE}")

    module = importlib.util.module_from_spec(spec)
    sys.modules[module_name] = module
    try:
        spec.loader.exec_module(module)
    except Exception:
        sys.modules.pop(module_name, None)
        raise
    return module
