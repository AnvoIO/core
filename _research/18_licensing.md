# Licensing Strategy

## Decision: Business Source License 1.1 (BSL)

Fork under BSL with a 3-year change date converting to Apache 2.0.

## Rationale

The fork introduces significant engineering work — Block-STM parallel execution,
AArch64 port, gas/staking hybrid model, passkey account integration, built-in
indexer, identity framework. BSL protects these innovations from being cloned
to launch competing chains while allowing full use by our ecosystem.

After the change date, everything converts to Apache 2.0 (permissive + patent
protection), giving the code back to the community.

## Legal Basis

Spring is MIT-licensed as of November 2025 (commit `e6a99f68b`). MIT explicitly
permits sublicensing. We can take MIT-licensed code and release the derivative
work under BSL. The original MIT-licensed portions remain available under MIT.

**Copyright chain:**
```
EOSIO/eos         — block.one (2017-2021) — MIT
AntelopeIO/spring — ENF (2021-2025) — BSL, then MIT (Nov 2025)
AntelopeIO/spring — Vaulta Foundation (2025+) — MIT
Anvo Network     — Anvo Network Foundation (2026+) — BSL → Apache 2.0
```

## License Text

```
Anvo Network
Copyright (c) 2026 Anvo Network Foundation and its contributors. All rights reserved.

License text copyright (c) 2023 MariaDB plc, All Rights Reserved.
"Business Source License" is a trademark of MariaDB plc.

Parameters

Licensor:           Anvo Network Foundation

Licensed Work:      Anvo Network v1.0 and all subsequent versions
                    released under this License.

Additional Use Grant:

    You are granted the right to use, copy, modify, create derivative
    works, and redistribute the Licensed Work for any purpose, subject
    to the following limitation:

    You may NOT use the Licensed Work, or any substantial portion of it,
    to operate, deploy, or provide a public blockchain network that
    competes with Anvo Network as a general-purpose Layer 1 blockchain.

    The following uses are expressly PERMITTED without limitation:

    (a) Operating nodes, validators, finalizers, API endpoints, indexers,
        or any other infrastructure for the Anvo Network.

    (b) Building and deploying smart contracts, dApps, tools, SDKs,
        wallets, explorers, bridges, or any other software that interacts
        with the Anvo Network.

    (c) Operating private, permissioned, or enterprise blockchain networks
        for internal use that are not offered as public general-purpose
        Layer 1 networks.

    (d) Academic research, education, personal study, and non-commercial
        experimentation.

    (e) Migrating an existing EOSIO/Antelope blockchain network to run
        Anvo Network software as its node implementation.

    (f) Any use that the Licensor has separately authorized in writing.

    For the purposes of this license:

    "Public blockchain network" means a permissionless blockchain network
    that is accessible to the general public and processes transactions
    from any participant.

    "Competes with Anvo Network" means a blockchain network that
    positions itself as an alternative general-purpose Layer 1 to
    Anvo Network for the purpose of attracting users, developers,
    validators, or liquidity away from Anvo Network.

Change Date:        Three (3) years from the date each version of the
                    Licensed Work is first publicly released by the
                    Licensor.

Change License:     Apache License, Version 2.0

Notice

Business Source License 1.1

Terms

The Licensor hereby grants you the right to copy, modify, create
derivative works, redistribute, and make non-production use of the
Licensed Work. The Licensor may make an Additional Use Grant, above,
permitting limited production use.

Effective on the Change Date, or the third anniversary of the first
publicly available distribution of a specific version of the Licensed
Work under this License, whichever comes first, the Licensor hereby
grants you rights under the terms of the Change License, and the rights
granted in the paragraph above terminate.

If your use of the Licensed Work does not comply with the requirements
currently in effect as described in this License, you must purchase a
commercial license from the Licensor, its affiliated entities, or
authorized resellers, or you must refrain from using the Licensed Work.

All copies of the original and modified Licensed Work, and derivative
works of the Licensed Work, are subject to this License. This License
applies separately for each version of the Licensed Work and the Change
Date may vary for each version of the Licensed Work released by Licensor.

You must conspicuously display this License on each original or modified
copy of the Licensed Work. If you receive the Licensed Work in original
or modified form from a third party, the terms and conditions set forth
in this License apply to your use of that work.

Any use of the Licensed Work in violation of this License will
automatically terminate your rights under this License for the current
and all other versions of the Licensed Work.

This License does not grant you any right in any trademark or logo of
Licensor or its affiliates (provided that you may use a trademark or
logo of Licensor as expressly required by this License).

TO THE EXTENT PERMITTED BY APPLICABLE LAW, THE LICENSED WORK IS PROVIDED
ON AN "AS IS" BASIS. LICENSOR HEREBY DISCLAIMS ALL WARRANTIES AND
CONDITIONS, EXPRESS OR IMPLIED, INCLUDING (WITHOUT LIMITATION) WARRANTIES
OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, NON-INFRINGEMENT,
AND TITLE.

---

This software is derived from:

AntelopeIO/spring
Copyright (c) 2025 Vaulta Foundation (VF) and its contributors.
Copyright (c) 2021-2025 EOS Network Foundation (ENF) and its contributors.
Licensed under the MIT License.

EOSIO/eos
Copyright (c) 2017-2021 block.one and its contributors.
Licensed under the MIT License.

The MIT License for the original works is included below:

Permission is hereby granted, free of charge, to any person obtaining a
copy of this software and associated documentation files (the "Software"),
to deal in the Software without restriction, including without limitation
the rights to use, copy, modify, merge, publish, distribute, sublicense,
and/or sell copies of the Software, and to permit persons to whom the
Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included
in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
```

## What's Permitted vs. Restricted

### Fully Permitted (No Restrictions)

| Use Case | Why |
|---|---|
| Run a node on our chain | Core ecosystem use |
| Build dApps on our chain | Core ecosystem use |
| Build wallets, explorers, SDKs | Ecosystem tooling |
| Operate bridge infrastructure | Ecosystem infrastructure |
| Migrate an existing EOSIO chain | Our core value proposition |
| Run a private/enterprise chain | Not a competing public L1 |
| Academic research | Non-commercial |
| Read, study, learn from the code | Non-commercial |
| Contribute improvements back | Community development |

### Restricted (During BSL Period)

| Use Case | Why Restricted |
|---|---|
| Fork and launch a competing public L1 | Protects our ecosystem investment |
| Clone our innovations for another chain | Protects engineering work |

### After Change Date (3 Years → Apache 2.0)

Everything becomes fully permissive with patent protection. Anyone can do
anything, including launching competing chains using the code.

## What BSL Protects

Our BSL specifically protects the engineering work we add on top of MIT Spring:

| Innovation | Development Effort | BSL Period |
|---|---|---|
| Block-STM parallel execution | ~6 months | Protected 3 years |
| AArch64 eos-vm-oc port | ~3 months | Protected 3 years |
| Gas/staking hybrid model | ~2-3 months | Protected 3 years |
| Passkey account integration | ~2-3 months | Protected 3 years |
| Built-in indexer plugin | ~4-5 months | Protected 3 years |
| System account compatibility | ~1-2 weeks | Protected 3 years |
| Identity framework contracts | ~3-4 months | Protected 3 years |

Anyone can still go to Spring's MIT repository and build their own versions
of these features. They just can't take our implementations.

## What BSL Does NOT Protect

- The underlying Spring/EOSIO code (remains MIT, anyone can fork Spring directly)
- Ideas, approaches, or architectural concepts (only the specific implementation)
- Smart contracts deployed on our chain (those are their authors' property)
- Trademarks (handled separately, not by software license)

## The MIT Escape Hatch

Important to acknowledge: because our fork is built on MIT-licensed Spring,
someone could always:

1. Fork Spring directly (MIT, no restrictions)
2. Implement their own Block-STM, gas model, etc. from scratch
3. Launch a competing chain without touching our BSL code

BSL doesn't prevent competition. It prevents **free-riding on our specific
implementation.** The protection is practical (12+ months of engineering
effort to replicate) not absolute.

## Handling Contributions

### Contributor License Agreement (CLA)

Contributors must accept a CLA granting us the right to include their
contributions under BSL. Standard practice for BSL projects.

Options:
- **Individual CLA** — each contributor signs
- **Developer Certificate of Origin (DCO)** — lighter weight, sign-off per commit
- **CLA via GitHub/GitLab bot** — automated, contributor signs once

**Recommendation:** DCO (Developer Certificate of Origin) via sign-off.
Less friction than a formal CLA. Same legal effect for our purposes.
Linux kernel, GitLab, and many large projects use DCO.

```
Signed-off-by: Developer Name <developer@example.com>
```

### What Contributors Should Know

- Their contributions are licensed under BSL (same as the project)
- After 3 years, contributions convert to Apache 2.0 (same as the project)
- They retain copyright on their contributions
- They grant us the right to distribute under BSL terms

## Crosslink Bridge Licensing

Crosslink is a separate project. Options:

**Option A: BSL (same as node)**
- Consistent licensing across the platform
- Protects bridge innovations (TSS/MPC, multi-party ceremonies)
- Bridge can only be used for our chain during BSL period

**Option B: Apache 2.0 (more permissive)**
- Encourages other chains to adopt Crosslink as their bridge
- Grows the bridge's ecosystem independently
- Bridge becomes a standalone product with its own adoption narrative

**Option C: Dual license**
- BSL for use with competing chains
- Free use for our chain and migrating EOSIO chains

**Recommendation:** Separate decision. Crosslink's licensing depends on whether
you want it to be a platform play (Apache 2.0, broad adoption) or a moat
(BSL, our chain only).

## Community Messaging

### How to Frame BSL

**Do say:**
- "Source-available from day one — read, study, audit every line of code"
- "Converts to Apache 2.0 after 3 years — fully open source, guaranteed"
- "Use it for anything EXCEPT cloning our chain to compete with us"
- "Build dApps, run nodes, operate infrastructure — all fully permitted"
- "Existing EOSIO chains can migrate — that's explicitly allowed"

**Don't say:**
- "Open source" — BSL is not OSI-approved open source. Say "source-available"
- "Free software" — loaded term, avoid

### Addressing the EOSIO BSL History

Spring's BSL was controversial because it restricted use to the EOS network
specifically, which was seen as ENF/Vaulta locking out other EOSIO chains.

Our BSL is different:
- **Existing EOSIO chain migration is explicitly permitted** (clause e)
- **Private/enterprise chains are explicitly permitted** (clause c)
- **Restriction is only on competing public L1s** — narrower than Spring's BSL
- **Converts to Apache 2.0** (with patent protection), not just MIT

## Implementation

### Files to Create/Update

| File | Content |
|---|---|
| `LICENSE` | Full BSL text (as above) |
| `NOTICE` | Apache 2.0 attribution (for after change date) |
| `DCO` | Developer Certificate of Origin text |
| `.github/CONTRIBUTING.md` | Contribution guidelines including DCO requirement |

### Rebrand Integration

The license file update is part of the rebrand (doc 07, Phase 1A):
1. Remove old LICENSE
2. Write new LICENSE with BSL text
3. Update copyright headers in all source files
4. Add NOTICE file for inherited MIT attributions

## Timeline

| Event | Date |
|---|---|
| Fork with BSL | Month 1 of development |
| v1.0 release | ~Month 6 (public testnet) |
| v1.0 Change Date | Month 6 + 3 years |
| v1.0 → Apache 2.0 | Automatic at Change Date |
| v2.0 release (if any) | Whenever released |
| v2.0 Change Date | v2.0 release + 3 years |

Each version has its own independent 3-year clock. v2.0 additions get their
own 3-year protection even after v1.0 has converted to Apache 2.0.
