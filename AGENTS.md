# Public ESP32-P4 reference application

- Read README.md, docs/ARCHITECTURE.md and docs/FEATURES.md before code changes.
- SDK implementation remains proprietary. Use only imported headers and archives under ignored vendor/moto-sdk; never copy SDK src/ or ports/.
- No device licenses, private keys, pairing records, personal paths, build output or production logs in Git.
- Keep demo/simulated values visibly identified; absent board drivers must fail, never invent success.
- Keep Chinese and English instructions synchronized. Build both root and examples/managed-terminal and run host tests before publishing.
- Do not flash hardware or start services unless requested.
