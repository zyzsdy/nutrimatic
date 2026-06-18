import importlib
import io
import os
import sys
from pathlib import Path

import pytest

from build_wikipedia_index import (
    BuildConfig,
    build_partial_indexes,
    check_native_windows,
    count_merge_jobs,
    default_config,
    extract_text,
    merge_indexes,
    plan_merge_round,
)


def test_build_partial_indexes_reports_file_progress(tmp_path, monkeypatch):
    text_dir = tmp_path / "text"
    text_dir.mkdir()
    (text_dir / "wiki_00").write_text("first", encoding="utf-8")
    (text_dir / "wiki_01").write_text("second", encoding="utf-8")
    make_index_exe = tmp_path / "make-index.exe"
    make_index_exe.touch()
    config = BuildConfig(
        data_dir=tmp_path,
        text_dir=text_dir,
        make_index_exe=make_index_exe,
    )
    progress_calls = []

    class FakeProgress:
        def __init__(self, iterable, **kwargs):
            progress_calls.append((list(iterable), kwargs))
            self.iterable = iterable

        def __enter__(self):
            return iter(self.iterable)

        def __exit__(self, exc_type, exc_value, traceback):
            return False

    class FakeProcess:
        def __init__(self):
            self.stdin = io.BytesIO()

        def wait(self):
            return 0

    monkeypatch.setattr("build_wikipedia_index.tqdm", FakeProgress, raising=False)
    monkeypatch.setattr(
        "build_wikipedia_index.subprocess.Popen", lambda *args, **kwargs: FakeProcess()
    )

    build_partial_indexes(config, dry_run=False)

    files, options = progress_calls[0]
    assert [path.name for path in files] == ["wiki_00", "wiki_01"]
    assert options == {"total": 2, "desc": "Indexing", "unit": "file"}


def test_default_config_uses_repo_data_directory():
    repo_root = Path(r"H:\Workspace\ciphertools\nutrimatic")

    config = default_config(repo_root)

    assert config.data_dir == repo_root / "data"
    assert config.dump_path == repo_root / "data" / "enwiki-latest-pages-articles.xml.bz2"
    assert config.text_dir == repo_root / "data" / "text"
    assert config.make_index_exe == repo_root / "build" / "make-index.exe"
    assert config.merge_indexes_exe == repo_root / "build" / "merge-indexes.exe"


def test_check_native_windows_rejects_mingw_environment():
    with pytest.raises(RuntimeError, match="MinGW"):
        check_native_windows({"MSYSTEM": "MINGW64"})


def test_plan_merge_round_batches_inputs_without_exceeding_limit(tmp_path):
    inputs = [tmp_path / f"wikipedia.{i:05d}.index" for i in range(451)]
    config = BuildConfig(data_dir=tmp_path, batch_size=200)

    jobs, next_inputs = plan_merge_round(config, inputs, round_number=0, cutoff=2)

    assert [len(job.inputs) for job in jobs] == [200, 200, 51]
    assert next_inputs == [job.output for job in jobs]
    assert next_inputs[0].name == "wiki-stage00-00000.index"
    assert next_inputs[2].name == "wiki-stage00-00002.index"


@pytest.mark.parametrize(
    ("input_count", "batch_size", "expected"),
    [(1, 200, 1), (200, 200, 1), (201, 200, 3), (451, 200, 4)],
)
def test_count_merge_jobs_includes_intermediate_and_final_jobs(
    input_count, batch_size, expected
):
    assert count_merge_jobs(input_count, batch_size) == expected


def test_merge_indexes_reports_progress_for_every_job(tmp_path, monkeypatch):
    for index in range(201):
        (tmp_path / f"wikipedia.{index:05d}.index").touch()
    merge_indexes_exe = tmp_path / "merge-indexes.exe"
    merge_indexes_exe.touch()
    (tmp_path / "wiki-stage00-00000.index").touch()
    config = BuildConfig(
        data_dir=tmp_path,
        merge_indexes_exe=merge_indexes_exe,
        batch_size=200,
    )
    progress_calls = []
    updates = []
    commands = []

    class FakeProgress:
        def __init__(self, **kwargs):
            progress_calls.append(kwargs)

        def __enter__(self):
            return self

        def __exit__(self, exc_type, exc_value, traceback):
            return False

        def update(self, amount):
            updates.append(amount)

    monkeypatch.setattr("build_wikipedia_index.tqdm", FakeProgress)
    monkeypatch.setattr(
        "build_wikipedia_index.run_command",
        lambda args, *, dry_run, env=None: commands.append(args),
    )

    merge_indexes(config, dry_run=False)

    assert progress_calls == [{"total": 3, "desc": "Merging", "unit": "merge"}]
    assert updates == [1, 1, 1]
    assert len(commands) == 2


def test_merge_indexes_dry_run_does_not_create_progress_bar(tmp_path, monkeypatch):
    (tmp_path / "wikipedia.00000.index").touch()
    config = BuildConfig(data_dir=tmp_path)

    def fail_tqdm(*args, **kwargs):
        raise AssertionError("dry-run must not create a progress bar")

    monkeypatch.setattr("build_wikipedia_index.tqdm", fail_tqdm)

    merge_indexes(config, dry_run=True)


def test_extract_text_prefers_vendored_wikiextractor(tmp_path, monkeypatch):
    dump_path = tmp_path / "dump.xml"
    dump_path.write_text("<mediawiki />", encoding="utf-8")
    config = BuildConfig(data_dir=tmp_path, dump_path=dump_path)
    calls = []

    def fake_run_command(args, *, dry_run, env=None):
        calls.append((args, dry_run, env))

    monkeypatch.setattr("build_wikipedia_index.run_command", fake_run_command)

    extract_text(config, dry_run=False)

    args, dry_run, env = calls[0]
    assert args[1:3] == ["-m", "wikiextractor.WikiExtractor"]
    assert dry_run is False
    assert env is not None
    assert str(Path(__file__).resolve().parents[1] / "wikiextractor") in env["PYTHONPATH"].split(
        os.pathsep
    )


def test_vendored_wikiextractor_runs_without_fork(tmp_path, monkeypatch):
    vendored_root = Path(__file__).resolve().parents[1] / "wikiextractor"
    monkeypatch.syspath_prepend(str(vendored_root))
    for name in list(sys.modules):
        if name == "wikiextractor" or name.startswith("wikiextractor."):
            del sys.modules[name]
    wiki_extractor = importlib.import_module("wikiextractor.WikiExtractor")
    monkeypatch.setattr(wiki_extractor, "get_all_start_methods", lambda: ["spawn"])
    monkeypatch.setattr(wiki_extractor.Extractor, "to_json", False, raising=False)

    dump_path = tmp_path / "tiny.xml"
    output_dir = tmp_path / "text"
    dump_path.write_text(
        """<mediawiki>
<siteinfo>
<base>https://en.wikipedia.org/wiki/Main_Page</base>
<namespace key="0" case="first-letter" />
<namespace key="10" case="first-letter">Template</namespace>
</siteinfo>
<page>
<title>Plain</title>
<id>1</id>
<revision>
<id>11</id>
<text xml:space="preserve">Hello '''Windows''' world.</text>
</revision>
</page>
</mediawiki>
""",
        encoding="utf-8",
    )

    wiki_extractor.process_dump(
        str(dump_path),
        None,
        str(output_dir),
        1024 * 1024,
        False,
        2,
        True,
        False,
    )

    extracted = "\n".join(path.read_text(encoding="utf-8") for path in output_dir.rglob("wiki_*"))
    assert "Hello Windows world." in extracted
