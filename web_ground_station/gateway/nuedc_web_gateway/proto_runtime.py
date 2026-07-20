import importlib.util
import subprocess
import sys
from pathlib import Path
from types import ModuleType


_REPO_ROOT = Path(__file__).resolve().parents[3]
_PROTO_SOURCE = _REPO_ROOT / "shared/proto/messages.proto"
_GENERATED_MODULE = (
    _REPO_ROOT / "web_ground_station/.generated/proto/messages_pb2.py"
)
_GENERATOR = _REPO_ROOT / "web_ground_station/scripts/generate_python_proto.py"


def _generate_if_stale() -> None:
    if (
        not _GENERATED_MODULE.exists()
        or _GENERATED_MODULE.stat().st_mtime < _PROTO_SOURCE.stat().st_mtime
    ):
        subprocess.run([sys.executable, str(_GENERATOR)], check=True)


def load_messages_module() -> ModuleType:
    _generate_if_stale()
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
