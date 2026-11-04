"""仅小范围正确性与进程控制验收；不进行性能测量。"""
import csv
import io
import json
from pathlib import Path
import subprocess
import sys
import tempfile

exe = Path(sys.argv[1]).resolve()
mode = sys.argv[2]
base = exe.parent / 'drowned-work'
base.mkdir(exist_ok=True)

def verify(result):
    cfg = result['config']
    rows = result['candidates']
    assert result['model'] == 'drowned-weighted-area-v2'
    assert result['verified'] is False and result['searchApproximate'] is True
    layers = (cfg['yMax'] - cfg['yMin'] + 1) if cfg['includeCaves'] else 1
    assert result['layers'] == layers
    assert cfg['candidateBudget'] >= cfg['top']
    expected = sorted(rows, key=lambda h: (-int(h['scoreNumerator']), h['distanceSq'], h['x'], h['z']))
    assert rows == expected
    for i, h in enumerate(rows):
        assert h['rank'] == i + 1
        numerator = 4880 * h['riverVolume'] + 7011 * h['caveVolume']
        denominator = 4880 * layers
        assert int(h['scoreNumerator']) == numerator
        assert h['scoreDenominator'] == denominator
        assert abs(h['score'] - numerator / denominator) < 1e-8
        assert numerator >= cfg['minArea'] * denominator
        assert h['bestPlayer'] == {'x': h['x'], 'y': h['y'], 'z': h['z']}
        if not cfg['includeCaves']:
            assert h['caveVolume'] == 0 and h['riverVolume'] == h['riverArea']
        for other in rows[:i]:
            distance = (h['x'] - other['x']) ** 2 + (h['z'] - other['z']) ** 2
            assert distance >= cfg['minDistance'] ** 2
    return rows

with tempfile.TemporaryDirectory(dir=base, prefix=mode+'-') as folder:
    work = Path(folder)
    if mode == 'cli':
        for coverage in ['circle', 'square', 'game']:
            for caves in [False, True]:
                args = [str(exe),'--seed','12345','--radius','64','--center-x','-87',
                        '--center-z','-236','--min-area','0','--limit','3','--threads','1',
                        '--inner-radius','16','--coverage',coverage,'--output',str(work)]
                if caves:
                    args += ['--include-caves']
                p = subprocess.run(args, capture_output=True, text=True, encoding='utf-8', timeout=120)
                assert p.returncode == 0, p.stderr
                result = json.loads(p.stdout.splitlines()[0])
                rows = verify(result)
                if len(sys.argv) > 3:
                    check = subprocess.run([sys.argv[3], result['exports']['json']],capture_output=True,
                        text=True,encoding='utf-8',timeout=120)
                    assert check.returncode == 0,check.stderr
                exported = json.loads(Path(result['exports']['json']).read_text(encoding='utf-8'))
                assert exported['candidates'] == rows
                table = list(csv.DictReader(Path(result['exports']['csv']).open(encoding='utf-8')))
                assert [int(r['scoreNumerator']) for r in table] == [int(r['scoreNumerator']) for r in rows]
        p = subprocess.run([str(exe),'--seed','12345','--radius','1','--center-x','4608',
            '--center-z','512','--include-caves','--optimize-player-y','--simulation-distance','1',
            '--min-area','0','--limit','1','--threads','1','--output',str(work)],
            capture_output=True,text=True,encoding='utf-8',timeout=120)
        assert p.returncode == 0,p.stderr
        result = json.loads(p.stdout.splitlines()[0])
        rows = verify(result)
        assert rows and rows[0]['caveVolume'] > 0
        if len(sys.argv) > 3:
            check = subprocess.run([sys.argv[3],result['exports']['json']],capture_output=True,
                text=True,encoding='utf-8',timeout=120)
            assert check.returncode == 0,check.stderr
        for old in ['--sample-y','--cave-factor']:
            p = subprocess.run([str(exe),old,'0'],capture_output=True,text=True,encoding='utf-8')
            assert p.returncode != 0 and '移除' in p.stderr
        config = work / '配置.json'
        config.write_text(json.dumps({'seed':'12345','radius':64,'center-x':-87,'center-z':-236,
            'min-area':0,'limit':1,'include-caves':True,'coverage':'square','inner-radius':8,
            'output':str(work/'中文结果')}),encoding='utf-8')
        p = subprocess.run([str(exe),'--config',str(config),'--no-include-caves','--inner-radius','1.5',
                            '--inner-radius-unit','chunks'],capture_output=True,text=True,encoding='utf-8')
        assert p.returncode == 0,p.stderr
        payload = json.loads(p.stdout.splitlines()[0])
        assert all(Path(path).is_file() for path in payload['exports'].values())
        baseline = payload['candidates']
        q = subprocess.run([str(exe),'--config',str(config),'--no-include-caves',
            '--inner-radius','1.5','--inner-radius-unit','chunks','--threads','2'],
            capture_output=True,text=True,encoding='utf-8',timeout=120)
        assert q.returncode == 0,q.stderr
        assert json.loads(q.stdout.splitlines()[0])['candidates'] == baseline
        c = payload['config']
        assert c['includeCaves'] is False and c['innerRadius'] == 24
    else:
        config = {'protocolVersion':3,'finder':'river','seed':'12345',
            'bounds':{'centerX':-87,'centerZ':-236,'radiusBlocks':64,'shape':'circle','fullWorld':False},
            'threads':1,'resultLimit':3,'deferExport':True,
            'params':{'mcVersion':'26.3','minArea':0,'coverage':'square','innerRadius':8,
                      'includeCaves':True}}
        def talk(commands):
            payload = ''.join(json.dumps(c)+'\n' for c in commands)
            p = subprocess.run([str(exe),'--desktop'],input=payload,capture_output=True,text=True,
                               encoding='utf-8',cwd=work,timeout=120)
            assert p.returncode == 0,p.stderr
            return [json.loads(line) for line in p.stdout.splitlines()]
        events = talk([{'type':'start','requestId':'test','config':config}])
        done = [e for e in events if e['type'] == 'done']
        assert len(done) == 1,events
        assert not [e for e in events if e['type'] == 'failed']
        rows = verify(done[0]['result'])
        assert json.loads(done[0]['exports']['json'])['candidates'] == rows
        assert list(csv.DictReader(io.StringIO(done[0]['exports']['csv'])))
        events = talk([{'type':'start','config':config},{'type':'pause','jobId':'river-1'},
                       {'type':'cancel','jobId':'river-1'}])
        assert [e for e in events if e['type']=='failed' and e['code']=='cancelled'],events
        assert not [e for e in events if e['type']=='done']
        config['params']['sampleY'] = 63
        events = talk([{'type':'start','config':config}])
        assert [e for e in events if e['type']=='failed' and e['code']=='invalid-config']
print(mode, 'passed; no performance benchmark')
