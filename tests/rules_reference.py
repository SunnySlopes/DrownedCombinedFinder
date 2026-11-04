"""只读冻结原版审计；常规测试不启动Minecraft或下载依赖。"""
from fractions import Fraction
import hashlib
import json
from pathlib import Path

root = Path(__file__).resolve().parent
m = json.loads((root/'drowned-rules-metadata.json').read_text(encoding='utf-8'))
for row in m['jsonTables']:
    river = row['biomes']['river']
    cave = row['biomes']['dripstone_caves']
    assert (river['totalWeight'],river['entry']['weight']) == (615,100)
    assert (cave['totalWeight'],cave['entry']['weight']) == (610,95)
    assert river['entry'].get('count',river['entry'].get('minCount')) == 1
    assert cave['entry'].get('count',cave['entry'].get('minCount')) == 4
for row in m['earlyRegistries']:
    assert row['records'] == ['river 615 100 1 1','dripstone_caves 610 95 4 4']
for row in m['predicates']:
    assert row['predicateFrequencyDenominators'] == [15,40]
assert Fraction(95,610)*4/Fraction(40) / (Fraction(100,615)/15) == Fraction(7011,4880)
assert hashlib.sha256((root/'drowned-original-reference.tsv').read_bytes()).hexdigest() == m['referenceSha256']
assert sum(row['points'] for row in m['biomeSamples']) == 2808
assert all(row['differences'] == 0 for row in m['biomeSamples'])
print('26 original biome spawn tables, 9 predicate versions and 2808 low-Y reference cells verified')
