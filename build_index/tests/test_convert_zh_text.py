from __future__ import annotations

from concurrent.futures import Future
from pathlib import Path

import pytest

import convert_zh_text
from convert_zh_text import (
    ConversionConfig,
    convert_file,
    discover_files,
    main,
    parse_args,
    run_conversion,
)


def write_source(root: Path, relative_path: str, text: str) -> Path:
    path = root / relative_path
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8", newline="")
    return path


def test_convert_file_converts_traditional_chinese_and_preserves_markup(tmp_path):
    source = write_source(
        tmp_path / "text",
        "AA/wiki_00",
        '<doc title="數學">\r\n繁體中文與结构\r\n</doc>\r\n',
    )
    output = tmp_path / "text_zhs" / "AA" / "wiki_00"
    convert_zh_text.initialize_worker()

    convert_file(source, output)

    assert output.read_bytes() == (
        '<doc title="数学">\r\n繁体中文与结构\r\n</doc>\r\n'.encode("utf-8")
    )


def test_discover_files_preserves_stable_relative_path_order(tmp_path):
    source_dir = tmp_path / "text"
    write_source(source_dir, "AB/wiki_00", "二")
    write_source(source_dir, "AA/wiki_02", "一")
    write_source(source_dir, "AA/wiki_01", "零")

    files = discover_files(source_dir)

    assert [path.relative_to(source_dir).as_posix() for path in files] == [
        "AA/wiki_01",
        "AA/wiki_02",
        "AB/wiki_00",
    ]


def test_run_top_is_applied_before_existing_outputs_are_skipped(tmp_path):
    source_dir = tmp_path / "text"
    output_dir = tmp_path / "text_zhs"
    for index in range(7):
        write_source(source_dir, f"AA/wiki_{index:02d}", "文字")
    write_source(output_dir, "AA/wiki_00", "existing")
    config = ConversionConfig(source_dir, output_dir, workers=1, run_top=5)

    summary = run_conversion(config, executor_factory=ImmediateExecutor)

    assert summary.scanned == 7
    assert summary.selected == 5
    assert summary.converted == 4
    assert summary.skipped == 1
    assert not (output_dir / "AA" / "wiki_05").exists()


def test_dry_run_does_not_create_output_or_start_executor(tmp_path):
    source_dir = tmp_path / "text"
    output_dir = tmp_path / "text_zhs"
    write_source(source_dir, "AA/wiki_00", "繁體")
    config = ConversionConfig(source_dir, output_dir, workers=2, dry_run=True)

    def fail_executor(**kwargs):
        raise AssertionError("dry-run must not start an executor")

    summary = run_conversion(config, executor_factory=fail_executor)

    assert summary.selected == 1
    assert summary.converted == 0
    assert summary.skipped == 0
    assert not output_dir.exists()


def test_existing_output_is_not_overwritten(tmp_path):
    source_dir = tmp_path / "text"
    output_dir = tmp_path / "text_zhs"
    write_source(source_dir, "AA/wiki_00", "繁體")
    existing = write_source(output_dir, "AA/wiki_00", "keep me")

    summary = run_conversion(
        ConversionConfig(source_dir, output_dir, workers=1),
        executor_factory=ImmediateExecutor,
    )

    assert summary.converted == 0
    assert summary.skipped == 1
    assert existing.read_text(encoding="utf-8") == "keep me"


def test_worker_count_is_passed_to_executor(tmp_path):
    source_dir = tmp_path / "text"
    write_source(source_dir, "AA/wiki_00", "繁體")
    calls = []

    class CapturingExecutor(ImmediateExecutor):
        def __init__(self, **kwargs):
            calls.append(kwargs)

    summary = run_conversion(
        ConversionConfig(source_dir, tmp_path / "text_zhs", workers=3),
        executor_factory=CapturingExecutor,
    )

    assert summary.converted == 1
    assert calls[0]["max_workers"] == 3
    assert calls[0]["initializer"] is convert_zh_text.initialize_worker


def test_invalid_utf8_is_reported_without_partial_output(tmp_path):
    source_dir = tmp_path / "text"
    output_dir = tmp_path / "text_zhs"
    source = source_dir / "AA" / "wiki_00"
    source.parent.mkdir(parents=True)
    source.write_bytes(b"\xff")

    summary = run_conversion(
        ConversionConfig(source_dir, output_dir, workers=1),
        executor_factory=ImmediateExecutor,
    )

    assert summary.converted == 0
    assert len(summary.failures) == 1
    assert summary.failures[0].relative_path == Path("AA/wiki_00")
    assert not (output_dir / "AA" / "wiki_00").exists()
    assert not list(output_dir.rglob("*.tmp"))


@pytest.mark.parametrize("field", ["workers", "run_top"])
def test_config_rejects_non_positive_counts(tmp_path, field):
    options = {"workers": 1, "run_top": 1}
    options[field] = 0

    with pytest.raises(ValueError, match=field.replace("_", "-")):
        ConversionConfig(tmp_path / "text", tmp_path / "text_zhs", **options)


@pytest.mark.parametrize(
    ("source_suffix", "output_suffix"),
    [("text", "text"), ("text", "text/output"), ("text/input", "text")],
)
def test_config_rejects_overlapping_directories(
    tmp_path, source_suffix, output_suffix
):
    with pytest.raises(ValueError, match="overlap"):
        ConversionConfig(tmp_path / source_suffix, tmp_path / output_suffix)


def test_parse_args_uses_current_directory_defaults(tmp_path, monkeypatch):
    monkeypatch.chdir(tmp_path)

    args = parse_args([])

    assert args.source_dir == tmp_path / "zhdata" / "text"
    assert args.output_dir == tmp_path / "zhdata" / "text_zhs"


def test_main_returns_nonzero_when_a_file_fails(tmp_path, monkeypatch):
    source_dir = tmp_path / "text"
    source_dir.mkdir()
    (source_dir / "bad").write_bytes(b"\xff")
    monkeypatch.setattr(convert_zh_text, "ProcessPoolExecutor", ImmediateExecutor)

    result = main(
        [
            "--source-dir",
            str(source_dir),
            "--output-dir",
            str(tmp_path / "text_zhs"),
            "--workers",
            "1",
        ]
    )

    assert result == 1


class ImmediateExecutor:
    def __init__(self, **kwargs):
        initializer = kwargs.get("initializer")
        if initializer is not None:
            initializer()

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_value, traceback):
        return False

    def submit(self, function, *args):
        future = Future()
        try:
            future.set_result(function(*args))
        except BaseException as error:
            future.set_exception(error)
        return future
