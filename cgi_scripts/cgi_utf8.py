class InternalUtf8EncodingError(RuntimeError):
    pass


def decode_subprocess_utf8(data: bytes) -> str:
    try:
        return data.decode("utf-8", errors="strict")
    except UnicodeDecodeError as error:
        raise InternalUtf8EncodingError("internal UTF-8 encoding error") from error
