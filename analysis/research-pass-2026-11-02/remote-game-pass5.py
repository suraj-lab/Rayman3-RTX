import subprocess, json, pathlib, os

WORKSPACE = pathlib.Path.home() / 'Projects/rayman3-rtx-analysis/hooks_0s8j6upd'
GAME_EXE = WORKSPACE / 'inputs' / 'Rayman3.exe'
DEST = WORKSPACE / 'evidence' / 'game-pass5'

TARGETS = [
    ('view-site', '0x492870'),
    ('world-0x142-a', '0x490c0e'),
    ('world-0x142-b', '0x48d08a'),
    ('projection-hunt', '0x48f2d2'),
]


def rpc(proc, rid, method, params):
    proc.stdin.write(json.dumps({'jsonrpc': '2.0', 'id': rid, 'method': method, 'params': params}) + '\n')
    proc.stdin.flush()
    while True:
        line = proc.stdout.readline()
        if not line:
            raise RuntimeError('owned MCP ended')
        msg = json.loads(line)
        if msg.get('id') == rid:
            return msg


def unpack(resp):
    result = resp.get('result', {})
    if isinstance(result.get('structuredContent'), dict):
        return result['structuredContent']
    for c in result.get('content', []):
        if c.get('type') == 'text':
            try:
                return json.loads(c['text'])
            except ValueError:
                continue
    return result


def main():
    DEST.mkdir(parents=True)
    env = dict(os.environ)
    env.update(GHIDRA_INSTALL_DIR='/opt/ghidra', JAVA_HOME='/usr/lib/jvm/java-21-openjdk')
    log = (DEST / 'mcp-stderr.log').open('w')
    proc = subprocess.Popen(
        ['node', '/home/suraj/.npm/_npx/af752b90495482d7/node_modules/rea-agents/scripts/rea.mjs', 'mcp'],
        stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=log, text=True, env=env)
    n = {'i': 0}

    def tool(label, name, arguments):
        n['i'] += 1
        resp = rpc(proc, n['i'], 'tools/call', {'name': name, 'arguments': arguments})
        (DEST / f'{label}.json').write_text(json.dumps(resp, indent=2))
        doc = unpack(resp)
        ok = not (resp.get('error') or resp.get('result', {}).get('isError'))
        print(json.dumps({'op': name, 'label': label,
                          'evidence_id': doc.get('evidence_id') if isinstance(doc, dict) else None,
                          'ok': ok}), flush=True)
        return doc

    try:
        rpc(proc, 1, 'initialize', {'protocolVersion': '2024-11-05', 'capabilities': {},
                                    'clientInfo': {'name': 'rayman-render-investigation', 'version': '1'}})
        n['i'] = 1
        proc.stdin.write(json.dumps({'jsonrpc': '2.0', 'method': 'notifications/initialized'}) + '\n')
        proc.stdin.flush()
        tool('00-open', 'open_binary', {'path': str(GAME_EXE), 'provider_id': 'ghidra'})
        for label, address in TARGETS:
            doc = tool(label, 'analyze_function', {'procedure': address})
            norm = (doc or {}).get('normalized_result', {})
            p = norm.get('procedure', {})
            print(json.dumps({'target': label, 'procedure': p.get('name'),
                              'asm_lines': len(norm.get('assembly') or [])}), flush=True)
    finally:
        tool('99-close', 'close_binary', {'snapshot_path': str(DEST / 'snapshot.json')})
        proc.stdin.close()
        try:
            proc.wait(timeout=20)
        except subprocess.TimeoutExpired:
            proc.terminate(); proc.wait(timeout=20)
        log.close()


if __name__ == '__main__':
    main()
