# Protected app audit ledger

Tumoflip audits every exact `xMasterX/all-the-plugins` Community Pack before
TumoCompanion treats a protected binary difference as reviewed. A tag alone is
not an identity: the audit key is the tag plus the named SHA-256 of both ZIPs.

## Lifecycle

1. Dispatch `Protected App Audit` with the new and previous pack tags. The
   workflow downloads both ZIPs, checks their GitHub asset digests, resolves the
   source commits, and creates or reuses one canonical issue.
2. The scanner compares every registered source path and the original author
   ref. A full `protectedKeys` inventory makes a new protected FAP/FAL that lacks
   a registry route fail closed as an unregistered intersection.
3. Reviewed entries are published immediately to the cumulative
   `protected-app-audit-ledger/latest.json`. Unresolved artifacts are omitted, so
   TumoCompanion continues to show only those artifacts as `DIFF`.
4. A target-bearing entry is accepted only when its target bytes occur in an
   exact released FW Packages manifest *and* the corresponding ZIP. The scanner
   checks path, byte count, MD5, SHA-256, clean source commit, revision and tag.
   A newer catalog may additionally retain a bounded older overlay through
   `compatible_builds`; the scanner recomputes the current manifest `release_id`
   and admits only aliases tied to one exact older catalog identity. Undeclared
   files from that older catalog remain untrusted.
5. Changed source needs an exact decision. A port records author and pack source
   commits, changelog, implementation commit, FW Packages channel/revision/tag,
   and hardware acceptance. Rejected changes still pin the exact retained target
   bytes. An intentional replacement is the only accepted missing-target case.
6. The issue remains open while any artifact is unresolved. Only a fully verified
   scan closes it automatically.

The immutable history files are content-addressed by the semantic audit payload
(excluding only `generatedAt`), so a scheduled no-op never creates churn and new
target evidence never overwrites an older record. The cumulative `latest.json`
uses schema 2. Accepted
target entries contain `targetMD5s` and one or more unique provenance records for
each allowed hash. Stable and dev may legitimately prove the same target MD5.

## Reviewed decision example

```json
{
  "schema": 2,
  "decisions": [
    {
      "appId": "subghz_raw_edit",
      "throughAuthorCommit": "<40 lowercase hex>",
      "sourceCommit": "<40 lowercase hex>",
      "disposition": "auditedDifference",
      "changelog": "Ported the reviewed upstream behavior.",
      "implementationCommit": "<40 lowercase hex>",
      "fwPackages": {
        "channel": "dev",
        "revision": 5,
        "releaseTag": "fw-packages-dev-005"
      },
      "hardwareAccepted": true
    }
  ]
}
```

Run the local gates with:

```sh
python3 -m unittest \
  tools/tumoflip/test_protected_app_audit.py \
  tools/tumoflip/test_ci_workflows.py
```
