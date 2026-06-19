import importlib.util
from pathlib import Path

import pytest


def load_cgi_utf8_module():
    path = Path(__file__).resolve().parents[2] / "cgi_scripts" / "cgi_utf8.py"
    spec = importlib.util.spec_from_file_location("cgi_utf8", path)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


def test_subprocess_output_decodes_strict_utf8():
    module = load_cgi_utf8_module()
    assert module.decode_subprocess_utf8("中国 <>&\"".encode()) == "中国 <>&\""


def test_invalid_subprocess_output_uses_fixed_internal_error():
    module = load_cgi_utf8_module()
    with pytest.raises(module.InternalUtf8EncodingError) as error:
        module.decode_subprocess_utf8(b"bad\xff")
    assert str(error.value) == "internal UTF-8 encoding error"
