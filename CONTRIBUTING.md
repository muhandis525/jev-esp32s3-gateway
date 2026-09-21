# Contributing

Issues and pull requests are welcome. Start with an issue for larger changes
so the design can be discussed before implementation.

Before opening a pull request:

1. Run `make test`.
2. Keep GPIO safety defaults enabled and add tests for parser or gateway changes.
3. Do not commit `jev.txt`, API keys, Wi-Fi credentials, provisioning PoPs, device tokens, `sdkconfig`, or build output.
4. Explain hardware assumptions and test-board details in the pull request.

Firmware changes should preserve the rule that natural-language input is
translated into a bounded canonical command before it reaches a device.
