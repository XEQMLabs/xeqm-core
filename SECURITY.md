# Security Policy

## Reporting a vulnerability

If you believe you've found a security vulnerability in XEQM Core or any related XEQMLabs software, please report it privately so it can be triaged and fixed before public disclosure.

**Email**: <security@xeqmlabs.com>

When reporting, please include:

- A description of the issue and its potential impact
- Step-by-step reproduction instructions
- The version, commit hash, or release tag you observed the issue against (`xeqm-d --version`)
- Any proof-of-concept code or sample data needed to reproduce
- Your name or alias for credit in the eventual fix announcement (optional)

We will acknowledge receipt within 72 hours and provide a more detailed response with our planned timeline within 7 days.

## Disclosure timeline

We coordinate disclosure with reporters. Our default approach:

1. Acknowledge receipt within 72 hours.
2. Confirm the issue, classify severity, and develop a fix.
3. Prepare a patch and coordinate a release with the reporter.
4. Disclose publicly once a fixed release is available and operators have had reasonable time to upgrade.

For severe vulnerabilities affecting funds at rest, network consensus, or operator safety, we will move as quickly as possible, including coordinating embargoed updates with major operators where appropriate.

## Scope

In scope:

- The XEQM Core daemon (`xeqm-d`)
- The CLI wallet (`xeqm-wallet`) and wallet RPC (`xeqm-rpc`)
- Service node consensus, P2P, and quorumnet code
- Build and release infrastructure (`.github/workflows/`, `Dockerfile`)
- Wallet GUI (separate repository: <https://github.com/XEQMLabs/XEQMLabs-GUI>)

Out of scope:

- Vulnerabilities in third-party dependencies (please report upstream; we will pull updated dependencies as they become available)
- Issues that require physical access to the operator's machine or compromised host privileges
- Denial-of-service via excessive resource consumption that does not affect consensus or fund safety

## Safe harbour

Good-faith security research conducted under this policy will not result in legal action from XEQMLabs. We ask that you:

- Make a reasonable effort to avoid privacy violations, service degradation, and data destruction
- Do not exploit a vulnerability beyond what is necessary to confirm it
- Do not disclose the issue publicly until we have had reasonable time to develop and release a fix
