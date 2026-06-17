# Index and Merge Progress Bars

## Scope

Add terminal progress bars to the `index` and `merge` steps in
`build_index/build_wikipedia_index.py`. Download, extraction, and test behavior
remain unchanged.

## Design

Use `tqdm` as a runtime dependency, installed through `uv add tqdm` from the
`build_index` project.

For the index step, materialize the sorted text-file iterator before launching
`make-index.exe`. Wrap those paths with a `tqdm` bar labeled `Indexing`, with
one unit per file. Preserve the current file ordering and streaming behavior.

For the merge step, calculate the merge jobs for all intermediate rounds and
the final merge as execution progresses. Show one progress bar for the complete
merge step, with one unit per merge job. The total is derived from the number
of partial indexes and the configured batch size. Outputs that already exist
and are skipped still advance the bar because their corresponding jobs are
complete.

Dry runs retain their current command-oriented output and do not display
progress bars. Existing early exits, missing-file errors, subprocess failures,
and resume behavior remain unchanged. Progress bars are closed if processing
raises an exception.

## Testing

Add focused pytest coverage using an injected or monkeypatched `tqdm` object:

- index reports a total equal to the number of input text files and advances
  once per streamed file;
- merge reports a total equal to all intermediate and final merge jobs and
  advances for both executed and skipped jobs;
- dry runs do not create progress bars.

Run the build-index pytest suite after implementation.
