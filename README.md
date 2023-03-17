# ompulsar
## Output module for Apache Pulsar message broker for rsyslog

Apache Pulsar is a message broker I'd like to add support to `rsyslog` as output module

This is early prototype.

I do not follow `rsyslog` code-style rules (do not use macros for function definitions, because it not useful with modern IDE with code folding).

It uses `cmake` for build (because Apache Pulsar C++ client uses it and do not have plans to support `autoconf`/`make`)
