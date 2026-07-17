## Summary

Describe the independently written change and the behavior or metadata it establishes.

## Evidence and validation

List tests, corpus checks, and evidence-ledger entries. Do not attach proprietary inputs or
private research logs.

## Clean-room checklist

- [ ] I included no BIOS, disc image, retail executable/module, save state, key, extracted asset,
      packet credential, decompiled source, or translated retail instruction block.
- [ ] Shipping code remains pure native host code and does not execute or translate PS2/MIPS code.
- [ ] Format or behavior claims use the confidence states in `docs/01-Clean-Room-Method.md`.
- [ ] Synthetic tests cover new parsing behavior; private-corpus results contain metadata only.
- [ ] Every commit has a matching `Signed-off-by` trailer (`git commit -s`).
