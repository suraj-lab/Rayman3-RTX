import subprocess, json, pathlib, os

WORKSPACE = pathlib.Path.home() / 'Projects/rayman3-rtx-analysis/hooks_0s8j6upd'
GAME_EXE = WORKSPACE / 'inputs' / 'Rayman3.exe'
DEST = WORKSPACE / 'evidence' / 'game-pass1'


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
    DEST.mkdir(parents=True)
    env = dict(os.environ)
    env.update(GHIDRA_INSTALL_DIR='/opt/ghidra', JAVA_HOME='/usr/lib/jvm/java-21-openjdk')
    log = (DEST / 'mcp-stderr.log').open('w')
    proc = subprocess.Popen(
        ['node', '/home/suraj/.npm/_npx/af752b90495482d7/node_modules/rea-agents/scripts/rea.mjs', 'mcp'],
        stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=log, text=True, env=env)
    counter = {'n': 0}

    def tool(label, name, arguments):
        counter['n'] += 1
        resp = rpc(proc, counter['n'], 'tools/call', {'name': name, 'arguments': arguments})
        (DEST / f'{label}.json').write_text(json.dumps(resp, indent=2))
        doc = unpack(resp)
        failure = resp.get('error') or resp.get('result', {}).get('isError')
        normalised = doc.get('normalized_result') if isinstance(doc, dict) else None
        print(json.dumps({'operation': name, 'label': label,
                          'evidence_id': doc.get('evidence_id') if isinstance(doc, dict) else None,
                          'error': failure,
                          'count': len(normalised) if isinstance(normalised, list) else None}), flush=True)
        return doc, failure

    try:
        rpc(proc, 1, 'initialize', {'protocolVersion': '2024-11-05', 'capabilities': {},
                                    'clientInfo': {'name': 'rayman-render-investigation', 'version': '1'}})
        counter['n'] = 1
        proc.stdin.write(json.dumps({'jsonrpc': '2.0', 'method': 'notifications/initialized'}) + '\n')
        proc.stdin.flush()

        _, failed = tool('00-open', 'open_binary', {'path': str(GAME_EXE), 'provider_id': 'ghidra'})
        if failed:
            return
        tool('01-overview', 'binary_overview', {})
        tool('02-d3d-strings', 'search_strings', {'pattern': 'Direct3DCreate8|d3d8|Camera_|Resolution|Widescreen|TnL|Software\\\\Microsoft\\\\Direct3D', 'mode': 'regex'})
        tool('03-d3d-procs', 'search_procedures', {'pattern': 'Direct3DCreate8|CreateDevice|D3D', 'mode': 'regex'})
        for label, address in [('04-xref-create8-iat', None)]:
            pass
        doc, _ = tool('05-entry', 'analyze_function', {'procedure': 'entry'})
    finally:
        tool('99-close', 'close_binary', {'snapshot_path': str(DEST / 'snapshot.json')})
        proc.stdin.close()
        try:
            proc.wait(timeout=20)
        except subprocess.TimeoutExpired:
            proc.terminate()
            proc.wait(timeout=20)
        log.close()


if __name__ == '__main__':
    main()
