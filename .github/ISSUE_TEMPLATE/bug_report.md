---
name: Bug report
about: Report a problem with XEQM Core (xeqm-d, xeqm-wallet, xeqm-rpc)
title: ''
labels: bug
assignees: ''

---

**Describe the bug**
A clear and concise description of what the bug is.

**To reproduce**
Steps to reproduce the behaviour:
1.
2.
3.

**Expected behaviour**
What you expected to happen.

**Actual behaviour**
What actually happened.

**Logs**
Please attach the relevant section of the daemon log. If you can reproduce the issue, please re-run with debug logging enabled and include the relevant lines:

```
curl -s http://127.0.0.1:9231/json_rpc -X POST -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":"0","method":"set_log_level","params":{"categories":"*:warn,core:debug,qnet:debug,oxenmq:debug,service_nodes:debug"}}'
```

**Environment**
- OS and version (e.g. Ubuntu 24.04, Windows 11, macOS 14):
- Install method: [Docker image / binary release / built from source]
- Daemon version (output of `xeqm-d --version`):
- Service node? [yes / no]
- Behind NAT? [yes / no]

**Additional context**
Anything else that might help — recent changes, related issues, screenshots, etc.
