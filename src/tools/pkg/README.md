# Package tools and repository

- `server/server.c`: the C HTTP repository host (`src/build/pkg/pkg-server` after building its file target).
- `build.c`: the deterministic `.spkg` archive builder.
- `repo/_pkges/`: generated ready-to-install archives.
- `repo/_src/<package>/`: program sources and package build recipes.

See [the package manager documentation](../../user/pkg/README.md) for commands,
format details, repository configuration, and tests.
