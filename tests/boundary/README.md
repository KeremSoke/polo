# Boundary checker self-tests

Each directory is a tiny source tree run through `tools/check-boundary.py
--root <dir> --expect <rule>`. Every `rule*` tree contains exactly one kind of
violation, so these tests prove that each boundary rule actually fails the
build. `clean/` must produce no violations. The repository-wide check skips
this directory.
