# Shared chain storage for multi-daemon hosts

Optional, Linux only, off by default. Nothing in this document applies unless you
pass `--shared-chain-dir`.

If you run several XEQM daemons on one host, they normally each keep a full,
independent copy of the chain. Shared chain storage lets them share a single copy
instead: one daemon is the **writer** and the others are read-only **readers** that
follow it. This cuts disk use and sync work to roughly that of a single node while
every daemon keeps its own service node identity, keys, and peer connections.

This is a production feature. It is used in production on hosts running many service
nodes. It is also independent of any private tooling: everything here is done with
the released binary and a standard service manager (systemd examples below), so an
operator who runs their own nodes can deploy and upgrade it without anything beyond
this repository's release.

## How it is laid out

Each daemon gets two directories:

- `--shared-chain-dir=DIR` — the shared chain. Every daemon on the host points at the
  **same** `DIR`. The writer creates and advances it; readers open it read-only.
- `--data-dir=DIR` — per-daemon, as usual: service node keys, p2p state, logs. Each
  daemon keeps its **own** `--data-dir`.

Roles:

| Role | Flags |
|------|-------|
| Writer (exactly one) | `--shared-chain-writer` |
| Reader (any number) | `--db-readonly` |

A daemon given `--shared-chain-dir` with **neither** role refuses to start. A reader
keeps all of its service node duties: it tracks the service node list, takes part in
quorums, and relays its votes. A reader does not accept transaction submissions over
RPC; it returns an error telling the sender to submit elsewhere.

### Storage backend

Shared chain storage works with either the default LMDB backend or the SQLite
backend. If your shared chain is SQLite, set the environment variable
`XEQM_DB_BACKEND=sqlite` for **every** daemon that points at it — writer and readers
alike. Without it the daemon opens the default LMDB backend and will not find a
SQLite chain. In a systemd unit this is an `Environment=` line (see the examples).

## Failover and reclaim

Readers can take over if the writer stops, and the writer can take the chain back
when it returns. This is what makes a rolling restart (and an unattended crash)
safe.

- `--shared-chain-promote` on a reader: if the writer stops, the reader promotes
  itself to writer and keeps the chain advancing. `--shared-chain-promote-grace`
  (default 30s) is how long it waits after its own startup before the first attempt.
- `--shared-chain-startup-fence-secs` (default 600, 0 disables): a writer that hangs
  during startup exits so a reader can take over. Raise it on a host where startup
  legitimately takes longer.
- `--shared-chain-reclaim-timeout` (default 45s, 0 disables): when the designated
  writer restarts while a promoted reader holds the chain, it asks the reader to step
  down and takes the chain back.

Run every daemon under a service manager that restarts it on a non-zero exit. Three
exit codes are part of normal operation, so do **not** treat them as crashes to
investigate:

| Exit code | Meaning |
|-----------|---------|
| 70 | The writer stopped making progress and fenced itself |
| 71 | A promoted writer stepped down for the designated writer |
| 72 | A reader could not follow a reorganisation and needs to reload |

With `Restart=on-failure` the service manager restarts the daemon in each case and it
rejoins in the correct role.

## Example systemd units

Writer (`/etc/systemd/system/xeqm-writer.service`):

```ini
[Service]
# Only if the shared chain is SQLite; omit for the default LMDB backend.
Environment=XEQM_DB_BACKEND=sqlite
ExecStart=/usr/local/bin/xeqm-d --non-interactive \
  --data-dir=/var/lib/xeqm/writer \
  --shared-chain-dir=/var/lib/xeqm/shared \
  --shared-chain-writer \
  --p2p-bind-port=9230 --rpc-admin=127.0.0.1:9231
Restart=on-failure
RestartSec=2
```

Reader (`/etc/systemd/system/xeqm-reader@.service`, one instance per node):

```ini
[Service]
Environment=XEQM_DB_BACKEND=sqlite
ExecStart=/usr/local/bin/xeqm-d --non-interactive \
  --data-dir=/var/lib/xeqm/%i \
  --shared-chain-dir=/var/lib/xeqm/shared \
  --db-readonly --shared-chain-promote --shared-chain-promote-grace=60 \
  --service-node --service-node-public-ip=YOUR.PUBLIC.IP \
  --p2p-bind-port=... --rpc-admin=127.0.0.1:... --quorumnet-port=...
Restart=on-failure
RestartSec=2
```

Give each reader its own ports and `--data-dir`. Point them all at the one
`--shared-chain-dir`.

## Upgrading a shared-chain host

A new release is a drop-in binary replacement. Because the readers can cover for the
writer, you can roll the whole host with no chain stall. This is the procedure; it
uses only `systemctl` and a binary copy.

1. **Pre-flight.** Confirm the new binary accepts the flags your units already pass
   (`xeqm-d --help`). Optionally canary it read-only first, without touching anything:
   run the new binary P2P-isolated against the live shared chain and confirm it opens
   and reaches the current tip, then stop it.

   ```bash
   # SQLite shared chain: keep the env var, or it opens LMDB and fails.
   XEQM_DB_BACKEND=sqlite /path/to/new/xeqm-d --non-interactive \
     --data-dir=/tmp/canary --shared-chain-dir=/var/lib/xeqm/shared \
     --db-readonly --out-peers=0 --in-peers=0 --rpc-admin=127.0.0.1:19999
   ```

2. **Back up the shared chain directory.** It is the one copy on the host.

3. **Install the new binary.** Replace the binary your units run (keep a copy of the
   old one). Daemons already running keep the old binary until they restart, so this
   step changes nothing on its own.

4. **Roll the readers, one at a time.** `systemctl restart` each reader and confirm it
   comes back on the new version, opens the shared chain read-only, and tracks the
   tip before moving to the next. The writer is never disturbed and keeps committing
   throughout.

5. **Restart the writer.** Two options:
   - Simplest: `systemctl restart` the writer. There is a brief gap of a few seconds
     with no new local blocks while it restarts on the new binary.
   - Zero-stall: `systemctl stop` the writer; with `--shared-chain-promote` a reader
     promotes itself and keeps the chain advancing; then `systemctl start` the writer,
     which reclaims the chain and the promoted reader steps back to a reader. The chain
     advances the whole time.

6. **Verify.** The writer is committing (height advancing), every reader is back and
   submitting uptime proofs, and the writer holds the lock. See the service node
   monitoring fields in the main [README](../README.md#running-a-service-node).

To roll back, restore the old binary the same way (readers first, then the writer)
and, only if the chain itself will not open, restore the backup from step 2.

## Known limit

A reader reads the chain, the rewards database, and the name database separately, not
as one snapshot. Votes, checkpoints, and uptime proofs do not use the two extra
databases and are unaffected. A reader chosen to produce a block can propose one from
a reward view that is a moment out of date; validators reject it and the next round
proceeds. A common snapshot for readers is planned for a later release.
