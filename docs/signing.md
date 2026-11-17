# Code signing

Unsigned downloads trigger Windows SmartScreen ("Windows protected your PC", unknown publisher).
Signing the installer and the binaries removes the "unknown publisher" warning. SmartScreen
reputation then builds up over downloads.

The release workflow signs automatically once a signing method is configured. Until then, it
publishes unsigned files.

## Option 1: SignPath Foundation (free for open source)

[SignPath Foundation](https://signpath.org/) provides free code signing certificates to open source
projects. The certificate is issued to SignPath Foundation, and the project is listed as the
publisher of record.

1. Apply on signpath.org with the repository URL. Conditions include an OSI license (MIT is fine), a
   public build on GitHub Actions, and a short review.
2. Once accepted, install the SignPath GitHub app on the repository and create a *signing policy* for
   release builds.
3. Add one repository **secret** (*Settings > Secrets and variables > Actions > Secrets*):
   `SIGNPATH_API_TOKEN`.
4. Add three repository **variables** (same page, *Variables* tab, not *Secrets*):
   `SIGNPATH_ORGANIZATION_ID`, `SIGNPATH_PROJECT_SLUG`, `SIGNPATH_POLICY_SLUG`. The workflow reads
   them as `vars.*`. If you store the organization id as a secret, signing is silently skipped.

The `sign` job in `.github/workflows/build.yml` runs only when the `SIGNPATH_ORGANIZATION_ID`
variable is set. Once it is set, the `release` job publishes only the signed files. The job signs in
three rounds, in this order:

1. `TourelleCamera.dll`, the media source loaded by the Windows Frame Server.
2. `Tourelle.exe` and `tourelle-cli.exe`, rebuilt around the **signed** DLL, which `Tourelle.exe`
   embeds and installs.
3. The installer, built from the signed executables.

Each round is a separate SignPath signing request on a GitHub artifact. Allow the policy to sign PE
files (`.dll` and `.exe`).

Protect what feeds the signer:

5. In *Settings > Environments*, create the `release` environment and add yourself as a required
   reviewer. The `sign` and `release` jobs wait for that approval.
6. In *Settings > Rules*, add a tag ruleset for `v*`: only maintainers can create these tags, and
   nobody can update or delete them. The `tag-check` job also refuses a tag whose commit is not on
   `main`.
7. Third-party actions are pinned to commit SHAs, and Inno Setup is pinned to a version and a
   SHA-256. Update the pins on purpose, never automatically.

## Option 2: Azure Trusted Signing (paid, about 10 USD per month)

It suits a project that wants to be signed under its own name. It needs an Azure subscription and
identity validation, available to individuals in some countries.

## Before asking for a certificate

A certificate vouches for what it signs. Before applying:

1. Run the security review again: see the latest report in the pull request or the release notes.
2. Make sure the CI is green: build, `/analyze`, unit tests, the AddressSanitizer run and the
   planted-DLL test (`tools/test-planted-dlls.ps1`).
3. Tag a release from `main` only, never from a local build.
