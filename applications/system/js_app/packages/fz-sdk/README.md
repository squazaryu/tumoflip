# Tumoflip JavaScript SDK

This package contains tooling and type declarations for developing Flipper Zero
JavaScript applications for Tumoflip firmware. It is derived from the
[official Flipper Zero JavaScript SDK](https://www.npmjs.com/package/@flipperdevices/fz-sdk)
and adds declarations for Tumoflip-specific JavaScript APIs.

The published npm package keeps its historical `@darkflippers/fz-sdk-ul` name
for compatibility with existing projects. New repository metadata, support
links, and documentation point to Tumoflip.

Applications made for the official Flipper Zero JavaScript SDK also work on
Tumoflip. When using distribution-specific features, check them with
`doesSdkSupport(["feature-name"])`. If a feature is essential, call
`checkSdkFeatures(["feature1", "feature2"])` near the beginning of the script
to show an explicit compatibility warning on unsupported firmware.

## Getting started
Create your application using the interactive wizard:
```shell
npx @darkflippers/create-fz-app-ul@latest
```

Then, enter the directory with your application and launch it:
```shell
cd my-flip-app
npm start
```

You are free to use `pnpm` or `yarn` instead of `npm`.

## Versioning
For each version of this package, the major and minor components match those of
the Flipper Zero JS SDK version that that package version targets. This version
follows semver. For example, apps compiled with SDK version `0.1.0` will be
compatible with SDK versions `0.1`...`1.0` (not including `1.0`).

Every API has a version history reflected in its JSDoc comment. It is heavily
recommended to check SDK compatibility using a combination of
`sdkCompatibilityStatus`, `isSdkCompatible`, `assertSdkCompatibility` depending
on your use case.

## Documentation
Check out the [JavaScript section in the Flipper Developer Documentation](https://developer.flipper.net/flipperzero/doxygen/js.html).
