# rpc-mt release notes

### v1.0

Date: 2019-02-08

That is, v1.0 is the first point in history
that gets a tag, so that anyone who might have
downloaded it could refer to it by tag.

All points in history between tags are not supported.

That may sound strange, because absolutely nothing is supported,
but some are more unsupported than others.

### v1.1

Date: 2019-02-09

New features:

1. Ability to specify a range of file descriptors to be used by RPC-MT.

1. New methods for waiting until it is safe to return to the main loop,
listen, and possibly launch serve another request.
