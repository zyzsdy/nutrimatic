from __future__ import annotations

import argparse
import os
import platform
import shutil
import subprocess
import sys
import urllib.request
from contextlib import nullcontext
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, Mapping, Sequence

from tqdm import tqdm


DEFAULT_DUMP_URL = (
    "https://dumps.wikimedia.org/enwiki/latest/"
    "enwiki-latest-pages-articles.xml.bz2"
)


@dataclass(frozen=True)
class DumpConfig:
    name: str
    url: str
    dump_path: Path
    text_dir: Path
    index_prefix: Path


@dataclass(frozen=True)
class BuildConfig:
    data_dir: Path
    repo_root: Path | None = None
    dumps: tuple[DumpConfig, ...] = ()
    dump_url: str = DEFAULT_DUMP_URL
    dump_path: Path | None = None
    text_dir: Path | None = None
    index_prefix: Path | None = None
    final_index: Path | None = None
    make_index_exe: Path | None = None
    merge_indexes_exe: Path | None = None
    find_expr_exe: Path | None = None
    batch_size: int = 200

    def __post_init__(self) -> None:
        repo_root = self.repo_root
        if repo_root is None:
            repo_root = Path(__file__).resolve().parents[1]
            object.__setattr__(self, "repo_root", repo_root)

        data_dir = self.data_dir.resolve()
        object.__setattr__(self, "data_dir", data_dir)
        if self.dumps:
            resolved_dumps = tuple(
                DumpConfig(
                    name=dump.name,
                    url=dump.url,
                    dump_path=(dump.dump_path if dump.dump_path.is_absolute() else data_dir / dump.dump_path).resolve(),
                    text_dir=(dump.text_dir if dump.text_dir.is_absolute() else data_dir / dump.text_dir).resolve(),
                    index_prefix=(dump.index_prefix if dump.index_prefix.is_absolute() else data_dir / dump.index_prefix).resolve(),
                )
                for dump in self.dumps
            )
        else:
            resolved_dumps = (
                DumpConfig(
                    name="enwiki",
                    url=self.dump_url,
                    dump_path=(self.dump_path or data_dir / "enwiki-latest-pages-articles.xml.bz2").resolve(),
                    text_dir=(self.text_dir or data_dir / "text").resolve(),
                    index_prefix=(self.index_prefix or data_dir / "wikipedia").resolve(),
                ),
            )
        if not resolved_dumps:
            raise ValueError("BuildConfig.dumps must not be empty")
        if len({dump.name for dump in resolved_dumps}) != len(resolved_dumps):
            raise ValueError("dump names must be unique")
        object.__setattr__(self, "dumps", resolved_dumps)
        primary = resolved_dumps[0]
        object.__setattr__(self, "dump_url", primary.url)
        object.__setattr__(
            self,
            "dump_path",
            primary.dump_path,
        )
        object.__setattr__(self, "text_dir", primary.text_dir)
        object.__setattr__(
            self,
            "index_prefix",
            primary.index_prefix,
        )
        object.__setattr__(
            self,
            "final_index",
            (self.final_index or data_dir / "wiki-merged.index").resolve(),
        )
        object.__setattr__(
            self,
            "make_index_exe",
            (self.make_index_exe or repo_root / "build" / "make-index.exe").resolve(),
        )
        object.__setattr__(
            self,
            "merge_indexes_exe",
            (self.merge_indexes_exe or repo_root / "build" / "merge-indexes.exe").resolve(),
        )
        object.__setattr__(
            self,
            "find_expr_exe",
            (self.find_expr_exe or repo_root / "build" / "find-expr.exe").resolve(),
        )


@dataclass(frozen=True)
class MergeJob:
    cutoff: int
    inputs: Sequence[Path]
    output: Path


def default_config(repo_root: Path | None = None) -> BuildConfig:
    root = (repo_root or Path(__file__).resolve().parents[1]).resolve()
    return BuildConfig(repo_root=root, data_dir=root / "data")


def check_native_windows(env: Mapping[str, str] | None = None) -> None:
    env = env or os.environ
    if platform.system() != "Windows":
        raise RuntimeError("This script is intended for native Windows only.")
    if env.get("MSYSTEM") or env.get("MINGW_CHOST") or env.get("MINGW_PREFIX"):
        raise RuntimeError("Run this from native Windows PowerShell/cmd, not MinGW/MSYS.")
    executable = Path(sys.executable)
    if any(part.lower() == "mingw-venv" for part in executable.parts):
        raise RuntimeError("Do not run this with the existing mingw-venv Python.")


def run_command(
    args: Sequence[str | Path],
    *,
    dry_run: bool,
    env: Mapping[str, str] | None = None,
) -> None:
    printable = " ".join(f'"{str(arg)}"' if " " in str(arg) else str(arg) for arg in args)
    print(f"> {printable}", flush=True)
    if dry_run:
        return
    subprocess.run([str(arg) for arg in args], check=True, env=env)


def vendored_wikiextractor_dir(repo_root: Path) -> Path:
    return repo_root / "build_index" / "wikiextractor"


def env_with_vendored_wikiextractor(repo_root: Path) -> dict[str, str]:
    env = os.environ.copy()
    vendored_dir = str(vendored_wikiextractor_dir(repo_root))
    pythonpath = env.get("PYTHONPATH")
    env["PYTHONPATH"] = (
        vendored_dir if not pythonpath else os.pathsep.join([vendored_dir, pythonpath])
    )
    return env


def download_dump(
    config: BuildConfig, dump: DumpConfig | None = None, *, dry_run: bool
) -> None:
    dump = dump or config.dumps[0]
    config.data_dir.mkdir(parents=True, exist_ok=True)
    if dump.dump_path.exists():
        print(f"Dump already exists, skipping download: {dump.dump_path}", flush=True)
        return
    print(f"Downloading {dump.url} -> {dump.dump_path}", flush=True)
    if dry_run:
        return
    with urllib.request.urlopen(dump.url) as response:
        with dump.dump_path.open("wb") as output:
            shutil.copyfileobj(response, output, length=1024 * 1024)


def extract_text(
    config: BuildConfig, dump: DumpConfig | None = None, *, dry_run: bool
) -> None:
    dump = dump or config.dumps[0]
    if not dump.dump_path.exists() and not dry_run:
        raise FileNotFoundError(dump.dump_path)
    if dump.text_dir.exists() and any(dump.text_dir.rglob("*")):
        print(f"Extracted text already exists, skipping extraction: {dump.text_dir}", flush=True)
        return
    args = [
        sys.executable,
        "-m",
        "wikiextractor.WikiExtractor",
        "-o",
        dump.text_dir,
        dump.dump_path,
    ]
    assert config.repo_root is not None
    run_command(args, dry_run=dry_run, env=env_with_vendored_wikiextractor(config.repo_root))


def iter_text_files(text_dir: Path) -> Iterable[Path]:
    return (path for path in sorted(text_dir.rglob("*")) if path.is_file())


def build_partial_indexes(
    config: BuildConfig, dump: DumpConfig | None = None, *, dry_run: bool
) -> None:
    dump = dump or config.dumps[0]
    assert config.make_index_exe is not None
    if not config.make_index_exe.exists() and not dry_run:
        raise FileNotFoundError(config.make_index_exe)
    existing = sorted(config.data_dir.glob(f"{dump.index_prefix.name}.?????.index"))
    if existing:
        print(f"Partial indexes already exist, skipping make-index: {len(existing)} files", flush=True)
        return
    if not dump.text_dir.exists() and not dry_run:
        raise FileNotFoundError(dump.text_dir)
    print(f"Streaming extracted text into {config.make_index_exe}", flush=True)
    if dry_run:
        print(f"> {config.make_index_exe} {dump.index_prefix} < {dump.text_dir}\\**\\*", flush=True)
        return

    files = list(iter_text_files(dump.text_dir))
    process = subprocess.Popen(
        [str(config.make_index_exe), str(dump.index_prefix)],
        stdin=subprocess.PIPE,
    )
    assert process.stdin is not None
    try:
        with tqdm(files, total=len(files), desc="Indexing", unit="file") as paths:
            for path in paths:
                with path.open("rb") as source:
                    shutil.copyfileobj(source, process.stdin, length=1024 * 1024)
                process.stdin.write(b"\n")
    finally:
        process.stdin.close()
    return_code = process.wait()
    if return_code:
        raise subprocess.CalledProcessError(return_code, [config.make_index_exe, dump.index_prefix])


def plan_merge_round(
    config: BuildConfig,
    inputs: Sequence[Path],
    *,
    round_number: int,
    cutoff: int,
) -> tuple[list[MergeJob], list[Path]]:
    jobs: list[MergeJob] = []
    next_inputs: list[Path] = []
    for start in range(0, len(inputs), config.batch_size):
        batch = list(inputs[start : start + config.batch_size])
        output = config.data_dir / f"wiki-stage{round_number:02d}-{len(jobs):05d}.index"
        job = MergeJob(cutoff=cutoff, inputs=batch, output=output)
        jobs.append(job)
        next_inputs.append(output)
    return jobs, next_inputs


def count_merge_jobs(input_count: int, batch_size: int) -> int:
    total = 0
    while input_count > batch_size:
        input_count = (input_count + batch_size - 1) // batch_size
        total += input_count
    return total + 1


def run_merge_job(config: BuildConfig, job: MergeJob, *, dry_run: bool) -> None:
    assert config.merge_indexes_exe is not None
    if job.output.exists():
        print(f"Merge output already exists, skipping: {job.output}", flush=True)
        return
    args: list[str | Path] = [config.merge_indexes_exe, str(job.cutoff), *job.inputs, job.output]
    run_command(args, dry_run=dry_run)


def merge_indexes(config: BuildConfig, *, dry_run: bool) -> None:
    assert config.final_index is not None
    assert config.merge_indexes_exe is not None
    if not config.merge_indexes_exe.exists() and not dry_run:
        raise FileNotFoundError(config.merge_indexes_exe)
    if config.final_index.exists():
        print(f"Final index already exists, skipping merge: {config.final_index}", flush=True)
        return

    inputs = sorted(
        path
        for dump in config.dumps
        for path in config.data_dir.glob(f"{dump.index_prefix.name}.?????.index")
    )
    if not inputs:
        message = f"No partial indexes found in {config.data_dir}"
        if dry_run:
            print(f"{message}; merge will run after the index step creates them.", flush=True)
            return
        raise RuntimeError(message)

    progress_context = (
        nullcontext(None)
        if dry_run
        else tqdm(
            total=count_merge_jobs(len(inputs), config.batch_size),
            desc="Merging",
            unit="merge",
        )
    )
    with progress_context as progress:
        round_number = 0
        while len(inputs) > config.batch_size:
            jobs, inputs = plan_merge_round(
                config, inputs, round_number=round_number, cutoff=2
            )
            for job in jobs:
                run_merge_job(config, job, dry_run=dry_run)
                if progress is not None:
                    progress.update(1)
            round_number += 1

        final_job = MergeJob(cutoff=5, inputs=inputs, output=config.final_index)
        run_merge_job(config, final_job, dry_run=dry_run)
        if progress is not None:
            progress.update(1)


def test_index(config: BuildConfig, expression: str, *, dry_run: bool) -> None:
    assert config.final_index is not None
    assert config.find_expr_exe is not None
    if not config.final_index.exists() and not dry_run:
        raise FileNotFoundError(config.final_index)
    run_command([config.find_expr_exe, config.final_index, expression], dry_run=dry_run)


def parse_args(argv: Sequence[str]) -> argparse.Namespace:
    defaults = default_config()
    parser = argparse.ArgumentParser(
        description="Build a Nutrimatic Wikipedia index on native Windows."
    )
    parser.add_argument("--data-dir", type=Path, default=defaults.data_dir)
    parser.add_argument("--dump-url", default=DEFAULT_DUMP_URL)
    parser.add_argument("--dump-path", type=Path)
    parser.add_argument(
        "--dump",
        action="append",
        default=[],
        metavar="NAME=URL",
        help="add a named Wikipedia dump; may be repeated",
    )
    parser.add_argument("--batch-size", type=int, default=200)
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument(
        "--steps",
        nargs="+",
        choices=["download", "extract", "index", "merge", "test"],
        default=["download", "extract", "index", "merge", "test"],
    )
    parser.add_argument("--test-expression", default="<aciimnrttu>")
    return parser.parse_args(argv)


def main(argv: Sequence[str] | None = None) -> int:
    args = parse_args(argv or sys.argv[1:])
    check_native_windows()
    dump_configs: tuple[DumpConfig, ...] = ()
    if args.dump:
        parsed_dumps = []
        for value in args.dump:
            name, separator, url = value.partition("=")
            if not separator or not name or not url:
                raise ValueError("--dump must use NAME=URL syntax")
            parsed_dumps.append(
                DumpConfig(
                    name=name,
                    url=url,
                    dump_path=Path(f"{name}-latest-pages-articles.xml.bz2"),
                    text_dir=Path(f"{name}-text"),
                    index_prefix=Path(name),
                )
            )
        dump_configs = tuple(parsed_dumps)
    config = BuildConfig(
        repo_root=Path(__file__).resolve().parents[1],
        data_dir=args.data_dir,
        dumps=dump_configs,
        dump_url=args.dump_url,
        dump_path=args.dump_path,
        batch_size=args.batch_size,
    )

    if "download" in args.steps:
        for dump in config.dumps:
            download_dump(config, dump, dry_run=args.dry_run)
    if "extract" in args.steps:
        for dump in config.dumps:
            extract_text(config, dump, dry_run=args.dry_run)
    if "index" in args.steps:
        for dump in config.dumps:
            build_partial_indexes(config, dump, dry_run=args.dry_run)
    if "merge" in args.steps:
        merge_indexes(config, dry_run=args.dry_run)
    if "test" in args.steps:
        test_index(config, args.test_expression, dry_run=args.dry_run)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
