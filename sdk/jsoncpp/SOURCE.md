# jsoncpp source provenance

`jsoncpp.cpp`, `json/json.h`, and `json/json-forwards.h` were generated unmodified
with `amalgamate.py` from upstream jsoncpp tag `1.9.3`. `LICENSE` is copied
unmodified from the same tag.

- Tag: https://github.com/open-source-parsers/jsoncpp/releases/tag/1.9.3
- Source archive: https://codeload.github.com/open-source-parsers/jsoncpp/zip/refs/tags/1.9.3
- Source archive SHA-256: `7853fe085ddd5da94b9795f4b520689c21f2753c4a8f7a5097410ee6136bf671`

From the root of an extracted upstream checkout, regenerate with:

```bash
astra_root=/path/to/Astra
python3 amalgamate.py -s "$astra_root/sdk/jsoncpp/jsoncpp.cpp" -i json/json.h
cp LICENSE "$astra_root/sdk/jsoncpp/LICENSE"
```
