#!/usr/bin/env python3
import hashlib
import subprocess
from pathlib import Path


def main() -> None:
    repo_root = Path(__file__).resolve().parents[2]
    proto_source = repo_root / "shared/proto/messages.proto"
    output_dir = repo_root / "web_ground_station/.generated/proto"
    output_dir.mkdir(parents=True, exist_ok=True)
    (output_dir / "__init__.py").touch()
    subprocess.run(
        [
            "protoc",
            f"--proto_path={proto_source.parent}",
            f"--python_out={output_dir}",
            str(proto_source),
        ],
        check=True,
    )
    source_hash = hashlib.sha256(proto_source.read_bytes()).hexdigest()
    (output_dir / "messages.proto.sha256").write_text(
        source_hash + "\n", encoding="ascii"
    )


if __name__ == "__main__":
    main()
