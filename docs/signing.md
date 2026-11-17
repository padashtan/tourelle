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
3. Add these repository secrets: `SIGNPATH_API_TOKEN`, `SIGNPATH_ORGANIZATION_ID`.
4. Add these repository variables: `SIGNPATH_PROJECT_SLUG`, `SIGNPATH_POLICY_SLUG`.

The `sign` job in `.github/workflows/build.yml` runs only when `SIGNPATH_ORGANIZATION_ID` is set. It
publishes the signed files as the `tourelle-windows-x64-signed` artifact. Once the first signed build
succeeds, make the `release` job depend on `sign` and download that artifact instead of the unsigned
one.

## Option 2: Azure Trusted Signing (paid, about 10 USD per month)

It suits a project that wants to be signed under its own name. It needs an Azure subscription and
identity validation, available to individuals in some countries.

## Before asking for a certificate

A certificate vouches for what it signs. Before applying:

1. Run the security review again: see the latest report in the pull request or the release notes.
2. Make sure the CI is green: build, `/analyze`, unit tests and the AddressSanitizer run.
3. Tag a release from `main` only, never from a local build.
