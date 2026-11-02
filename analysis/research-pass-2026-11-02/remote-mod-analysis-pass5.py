import subprocess, json, pathlib, os

WORKSPACE = pathlib.Path.home() / 'Projects/rayman3-rtx-analysis/hooks_0s8j6upd'
INPUTS = WORKSPACE / 'inputs'
DEST = WORKSPACE / 'evidence' / 'pass5'

PLAN = [
    ('UltimateASILoader-winmm.dll', [('60-asi-loader-fn', '0x10019a90')]),
    ('BetterRayman3.dll', [('70-config-fn', '0x10001dce'), ('71-config-fn-2', '0x100020ea')]),
]


def rpc(proc, request_id, method, params):
    proc.stdin.write(json.dumps({'jsonrpc': '2.0', 'id': request_id, 'method': method, 'params': params}) + '\n')
    proc.stdin.flush()
    while True:
        line = proc.stdout.readline()
        if not line:
            raise RuntimeError('owned MCP ended')
        msg = json.loads(line)
        if msg.get('id') == request_id:
            return msg


def unpack(resp):
    result = resp.get('result', {})
    structured = result.get('structuredContent')
    if isinstance(structured, dict):
        return structured
    for content in result.get('content', []):
        if content.get('type') == 'text':
            try:
                return json.loads(content['text'])
            except ValueError:
                continue
    return result


def main():
    DEST.mkdir()
    env = dict(os.environ)
    env.update(GHIDRA_INSTALL_DIR='/opt/ghidra', JAVA_HOME='/usr/lib/jvm/java-21-openjdk')
    log = (DEST / 'mcp-stderr.log').open('w')
    proc = subprocess.Popen(
        ['node', '/home/suraj/.npm/_npx/af752b90495482d7/node_modules/rea-agents/scripts/rea.mjs', 'mcp'],
        stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=log, text=True, env=env)
    counter = {'n': 0}

    def tool(target, label, name, arguments):
        counter['n'] += 1
        resp = rpc(proc, counter['n'], 'tools/call', {'name': name, 'arguments': arguments})
        (target / f'{label}.json').write_text(json.dumps(resp, indent=2))
        doc = unpack(resp)
        failure = resp.get('error') or resp.get('result', {}).get('isError')
        print(json.dumps({'target': target.name, 'operation': name, 'label': label,
                          'evidence_id': doc.get('evidence_id') if isinstance(doc, dict) else None,
                          'error': failure}), flush=True)
        if failure:
            raise RuntimeError(f'{name} failed: {json.dumps(failure)[:400]}')
        return doc

    try:
        rpc(proc, 1, 'initialize', {'protocolVersion': '2024-11-05', 'capabilities': {},
                                    'clientInfo': {'name': 'rayman-static-analysis', 'version': '1'}})
        counter['n'] = 1
        proc.stdin.write(json.dumps({'jsonrpc': '2.0', 'method': 'notifications/initialized'}) + '\n')
        proc.stdin.flush()

        for image, steps in PLAN:
            target = DEST / image
            target.mkdir()
            tool(target, '00-open', 'open_binary', {'path': str(INPUTS / image), 'provider_id': 'ghidra'})
            for label, address in steps:
                tool(target, label, 'analyze_function', {'procedure': address})
            tool(target, '99-close', 'close_binary', {'snapshot_path': str(target / 'snapshot.json')})
    finally:
        proc.stdin.close()
        try:
            proc.wait(timeout=20)
        except subprocess.TimeoutExpired:
            proc.terminate()
            proc.wait(timeout=20)
        log.close()


if __name__ == '__main__':
    main()
