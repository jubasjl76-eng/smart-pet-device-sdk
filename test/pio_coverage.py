"""Add --coverage to the link step for [env:native-cov].

PlatformIO propagates build_flags to compilation but not to the test-binary
link, so gcov's runtime (__gcov_init) goes unresolved. Appending --coverage to
LINKFLAGS lets the gcc driver pull in libgcov last, in the right order.
"""
Import("env")  # noqa: F821  (injected by PlatformIO/SCons)

env.Append(LINKFLAGS=["--coverage"])
