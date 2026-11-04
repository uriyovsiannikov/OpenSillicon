devfs exposes `/dev/console`, `/dev/tty`, `/dev/null`, `/dev/zero`,
`/dev/random`, and `/dev/urandom` through the common filesystem interface.
`/dev/tty` currently aliases the single system TTY. Both random devices
currently use the bounded kernel RDRAND provider and return `EAGAIN` when it
cannot provide bytes. They have identical behavior and do not yet provide a
cryptographic random stream or blocking entropy-pool semantics.
