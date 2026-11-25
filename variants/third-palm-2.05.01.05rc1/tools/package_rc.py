from __future__ import annotations

import argparse
import hashlib
import re
from pathlib import Path
from zipfile import ZIP_DEFLATED, ZipFile, ZipInfo

ROOT = Path(__file__).resolve().parent.parent
VERSION = re.search(r'^#define SUPERJUMP_VERSION_TEXT "([^"]+)"$',
                    (ROOT / "src/ModVersion.h").read_text(encoding="utf-8"), re.MULTILINE)[1]
INI = ROOT / "distribution/SuperJump_ThirdPalm.ini"


def archive(path: Path, files: dict[str, bytes]) -> str:
    manifest = "".join(f"{hashlib.sha512(data).hexdigest()}  {name}\n"
                       for name, data in sorted(files.items()))
    contents = {**files, "checksums.sha512": manifest.encode("utf-8")}
    with ZipFile(path, "w", compression=ZIP_DEFLATED, compresslevel=9) as output:
        for name, data in sorted(contents.items()):
            info = ZipInfo(name, (2026, 11, 21, 0, 0, 0))
            info.compress_type = ZIP_DEFLATED
            output.writestr(info, data, compresslevel=9)
    with ZipFile(path) as result:
        if set(result.namelist()) != set(contents) or result.testzip():
            raise ValueError(f"Invalid archive members in {path}")
        for name, expected in contents.items():
            if result.read(name) != expected:
                raise ValueError(f"Archive verification failed for {name}")
    return hashlib.sha256(path.read_bytes()).hexdigest()


def source_files(reference: Path) -> dict[str, bytes]:
    files = {name: (ROOT / name).read_bytes()
             for name in ("CMakeLists.txt", "README.md", "BUILD.md", "licenses_and_third_party.txt")}
    files[INI.relative_to(ROOT).as_posix()] = INI.read_bytes()
    for directory in ("src", "tests", "tools"):
        for path in (ROOT / directory).rglob("*"):
            if path.suffix in {".cpp", ".h", ".rc", ".asm", ".py"}:
                files[path.relative_to(ROOT).as_posix()] = path.read_bytes()

    with_manifest = reference / "checksums.sha512"
    reference_manifest = {}
    if with_manifest.is_file():
        for row in with_manifest.read_text(encoding="utf-8-sig").splitlines():
            digest, name = row.split(maxsplit=1)
            reference_manifest[name.removeprefix("*")] = digest.lower()

    for path in (reference / "CMakeLists.txt", *(reference / name for name in ("compat", "deps", "shared", "source"))):
        selected = [path] if path.is_file() else sorted(p for p in path.rglob("*") if p.is_file())
        if not selected:
            raise ValueError(f"Missing reference input: {path}")
        for item in selected:
            relative = item.relative_to(reference)
            if relative.parts[0] == "source" and item.suffix != ".h":
                continue
            name = relative.as_posix()
            data = item.read_bytes()
            if reference_manifest and reference_manifest.get(name) != hashlib.sha512(data).hexdigest():
                raise ValueError(f"Reference input has changed: {name}")
            files[f"reference/P26/{name}"] = data
    return files


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("asi", type=Path)
    parser.add_argument("p26_reference", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    if args.asi.name != "SuperJump_ThirdPalm.asi":
        parser.error("expected SuperJump_ThirdPalm.asi")
    stem = f"SuperJump-ThirdPalm-{VERSION}"
    runtime = args.output / f"{stem}.zip"
    sources = args.output / f"{stem}-Source.zip"
    license_file = (ROOT / "licenses_and_third_party.txt").read_bytes()
    asi = args.asi.read_bytes()
    package = {"SuperJump_ThirdPalm.asi": asi, "README.md": (ROOT / "README.md").read_bytes(),
               INI.name: INI.read_bytes(), "licenses_and_third_party.txt": license_file}
    source = source_files(args.p26_reference)
    args.output.mkdir(parents=True, exist_ok=True)
    result = [f"{archive(runtime, package)}  {runtime.name}",
              f"{archive(sources, source)}  {sources.name}",
              f"{hashlib.sha256(asi).hexdigest()}  SuperJump_ThirdPalm.asi"]
    (args.output / f"{stem}.sha256").write_text("\n".join(result) + "\n", encoding="ascii")
    print("\n".join(result))


if __name__ == "__main__":
    main()
