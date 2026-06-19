from __future__ import annotations

import argparse
import os
import sys
import uuid
from concurrent.futures import Future, ProcessPoolExecutor, as_completed
from dataclasses import dataclass
from pathlib import Path
from typing import Callable, Sequence

from opencc import OpenCC
from tqdm import tqdm


_converter: OpenCC | None = None


@dataclass(frozen=True)
class ConversionConfig:
    source_dir: Path
    output_dir: Path
    workers: int = os.cpu_count() or 1
    dry_run: bool = False
    run_top: int | None = None

    def __post_init__(self) -> None:
        source_dir = self.source_dir.resolve()
        output_dir = self.output_dir.resolve()
        object.__setattr__(self, "source_dir", source_dir)
        object.__setattr__(self, "output_dir", output_dir)

        if self.workers <= 0:
            raise ValueError("--workers must be greater than zero")
        if self.run_top is not None and self.run_top <= 0:
            raise ValueError("--run-top must be greater than zero")
        if source_dir == output_dir or source_dir in output_dir.parents or output_dir in source_dir.parents:
            raise ValueError("source and output directories must not overlap")


@dataclass(frozen=True)
class ConversionFailure:
    relative_path: Path
    message: str


@dataclass(frozen=True)
class ConversionSummary:
    scanned: int
    selected: int
    converted: int
    skipped: int
    failures: tuple[ConversionFailure, ...] = ()


def initialize_worker() -> None:
    global _converter
    _converter = OpenCC("t2s")


def convert_file(source: Path, output: Path) -> None:
    global _converter
    if _converter is None:
        initialize_worker()
    assert _converter is not None

    text = source.read_bytes().decode("utf-8")
    converted = _converter.convert(text).encode("utf-8")
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.with_name(
        f".{output.name}.{os.getpid()}.{uuid.uuid4().hex}.tmp"
    )
    try:
        temporary.write_bytes(converted)
        os.replace(temporary, output)
    finally:
        temporary.unlink(missing_ok=True)


def discover_files(source_dir: Path) -> list[Path]:
    return sorted(
        (path for path in source_dir.rglob("*") if path.is_file()),
        key=lambda path: path.relative_to(source_dir).as_posix(),
    )


def run_conversion(
    config: ConversionConfig,
    *,
    executor_factory: Callable[..., object] | None = None,
) -> ConversionSummary:
    if not config.source_dir.is_dir():
        raise FileNotFoundError(f"source directory does not exist: {config.source_dir}")

    files = discover_files(config.source_dir)
    selected = files[: config.run_top] if config.run_top is not None else files
    pending: list[tuple[Path, Path, Path]] = []
    skipped = 0
    for source in selected:
        relative_path = source.relative_to(config.source_dir)
        output = config.output_dir / relative_path
        if output.exists():
            skipped += 1
        else:
            pending.append((source, output, relative_path))

    if config.dry_run:
        return ConversionSummary(
            scanned=len(files),
            selected=len(selected),
            converted=0,
            skipped=skipped,
        )

    if not pending:
        return ConversionSummary(
            scanned=len(files),
            selected=len(selected),
            converted=0,
            skipped=skipped,
        )

    factory = executor_factory or ProcessPoolExecutor
    converted = 0
    failures: list[ConversionFailure] = []
    with factory(max_workers=config.workers, initializer=initialize_worker) as executor:
        futures: dict[Future[None], Path] = {
            executor.submit(convert_file, source, output): relative_path
            for source, output, relative_path in pending
        }
        for future in tqdm(
            as_completed(futures),
            total=len(futures),
            desc="Converting",
            unit="file",
        ):
            relative_path = futures[future]
            try:
                future.result()
                converted += 1
            except BaseException as error:
                failures.append(
                    ConversionFailure(relative_path=relative_path, message=str(error))
                )

    failures.sort(key=lambda failure: failure.relative_path.as_posix())
    return ConversionSummary(
        scanned=len(files),
        selected=len(selected),
        converted=converted,
        skipped=skipped,
        failures=tuple(failures),
    )


def positive_integer(value: str) -> int:
    number = int(value)
    if number <= 0:
        raise argparse.ArgumentTypeError("must be greater than zero")
    return number


def parse_args(argv: Sequence[str]) -> argparse.Namespace:
    current_dir = Path.cwd()
    parser = argparse.ArgumentParser(
        description="Convert a directory tree from Traditional to Simplified Chinese."
    )
    parser.add_argument(
        "--source-dir",
        type=Path,
        default=current_dir / "zhdata" / "text",
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=current_dir / "zhdata" / "text_zhs",
    )
    parser.add_argument(
        "--workers",
        type=positive_integer,
        default=os.cpu_count() or 1,
    )
    parser.add_argument("--run-top", type=positive_integer)
    parser.add_argument("--dry-run", action="store_true")
    return parser.parse_args(argv)


def print_summary(config: ConversionConfig, summary: ConversionSummary) -> None:
    mode = "Dry run" if config.dry_run else "Conversion"
    print(f"{mode}: {config.source_dir} -> {config.output_dir}")
    print(
        "Summary: "
        f"scanned={summary.scanned}, selected={summary.selected}, "
        f"converted={summary.converted}, skipped={summary.skipped}, "
        f"failed={len(summary.failures)}"
    )
    for failure in summary.failures:
        print(f"FAILED {failure.relative_path}: {failure.message}", file=sys.stderr)


def main(argv: Sequence[str] | None = None) -> int:
    args = parse_args(sys.argv[1:] if argv is None else argv)
    try:
        config = ConversionConfig(
            source_dir=args.source_dir,
            output_dir=args.output_dir,
            workers=args.workers,
            dry_run=args.dry_run,
            run_top=args.run_top,
        )
        summary = run_conversion(config)
    except (FileNotFoundError, ValueError) as error:
        print(f"Error: {error}", file=sys.stderr)
        return 2

    print_summary(config, summary)
    return 1 if summary.failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
