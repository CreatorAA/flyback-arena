"""Copy the exact Ubuntu glibc source package used to link the Linux bots."""
import hashlib
from pathlib import Path
import re
import shutil
import sys


def stage(source: Path, destination: Path, version: str) -> None:
    # Debian's source filename omits the optional epoch, but its Version field does not.
    dsc = source / f"glibc_{version.split(':')[-1]}.dsc"
    lines = dsc.read_text(encoding="utf-8").splitlines()
    if "Source: glibc" not in lines or f"Version: {version}" not in lines:
        raise ValueError(f"Unexpected source or version in {dsc.name}")
    files = []
    for row in lines[lines.index("Checksums-Sha256:") + 1:]:
        if not row.startswith(" "):
            break
        digest, size, name = row.split()
        if not re.fullmatch(r"glibc_[a-zA-Z0-9_.+~-]+", name):
            raise ValueError(f"Invalid source filename: {name}")
        entry = source / name
        with entry.open("rb") as handle:
            if entry.stat().st_size != int(size) or hashlib.file_digest(handle, "sha256").hexdigest() != digest:
                raise ValueError(f"Source file does not match {dsc.name}: {name}")
        files.append(entry)
    if not all(any(part in entry.name and not entry.name.endswith(".asc") for entry in files)
               for part in (".orig.tar.", ".debian.tar.")):
        raise ValueError(f"Missing upstream or Debian source archive in {dsc.name}")
    destination.mkdir(parents=True, exist_ok=True)
    for entry in [dsc, *files]:
        shutil.copyfile(entry, destination / entry.name)


if __name__ == "__main__":
    stage(Path(sys.argv[1]), Path(sys.argv[2]), sys.argv[3])
