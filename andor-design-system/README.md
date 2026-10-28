# ANDOR design system

ANDOR OSP's interface is the ANDOR design. This folder documents it, and gives every sister
product the means to reuse it exactly.

| | |
|---|---|
| **Start here** | [HANDOFF.md](HANDOFF.md): what exists, what to change, the absolute design rules |
| Prompt for a new instrument | [NEW_INSTRUMENT_PROMPT.md](NEW_INSTRUMENT_PROMPT.md) |
| Design bible | [guide/index.html](guide/index.html) (gallery of real renders, 100/150/200 %), [01 identity](guide/01-identity.md), [02 tokens](guide/02-tokens.md), [03 components](guide/03-components.md), [04 interaction](guide/04-interaction.md), [05 assets](guide/05-assets.md), [06 code](guide/06-code.md) |
| Visual regression | [regression/README.md](regression/README.md) |
| Product registry | [product-registry.json](product-registry.json) |
| Asset licensing | [assets/manifest.json](assets/manifest.json) |
| Reports | [reports/audit.md](reports/audit.md) (extraction audit), [reports/validation.md](reports/validation.md) (results) |

The code lives elsewhere:
- `shared/andor-ui`: the interface;
- `shared/andor-shell`: the instrument shell;
- `tools/create_andor_instrument.py`: the bootstrap tool;
- `products/andor-example`: the example product.
