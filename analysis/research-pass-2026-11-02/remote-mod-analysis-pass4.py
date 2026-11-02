import subprocess, json, pathlib, os

WORKSPACE = pathlib.Path.home() / 'Projects/rayman3-rtx-analysis/hooks_0s8j6upd'
INPUTS = WORKSPACE / 'inputs'
DEST = WORKSPACE / 'evidence' / 'pass4'


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
        normalised = doc.get('normalized_result') if isinstance(doc, dict) else None
        print(json.dumps({'target': target.name, 'operation': name, 'label': label,
                          'evidence_id': doc.get('evidence_id') if isinstance(doc, dict) else None,
                          'error': failure,
                          'count': len(normalised) if isinstance(normalised, list) else None}), flush=True)
        if failure:
            raise RuntimeError(f'{name} failed: {json.dumps(failure)[:400]}')
        return doc

    def callers_of(document_address):
        return [entry.get('source_procedure', {}).get('address')
                for entry in document_address.get('normalized_result', [])
                if isinstance(entry, dict) and entry.get('source_procedure')]

    try:
        rpc(proc, 1, 'initialize', {'protocolVersion': '2024-11-05', 'capabilities': {},
                                    'clientInfo': {'name': 'rayman-static-analysis', 'version': '1'}})
        counter['n'] = 1
        proc.stdin.write(json.dumps({'jsonrpc': '2.0', 'method': 'notifications/initialized'}) + '\n')
        proc.stdin.flush()

        asi = DEST / 'UltimateASILoader-winmm.dll'
        asi.mkdir()
        tool(asi, '00-open', 'open_binary', {'path': str(INPUTS / 'UltimateASILoader-winmm.dll'), 'provider_id': 'ghidra'})
        for label, address in [('40-windowpos-string', '0x10080e30'), ('41-windowtext-string', '0x10080e82'),
                               ('42-asi-glob-string', '0x100624ec')]:
            doc = tool(asi, label, 'xrefs', {'address': address})
            for index, caller in enumerate(callers_of(doc)[:3]):
                tool(asi, f'{label}-fn-{index}', 'analyze_function', {'procedure': caller})
        tool(asi, '99-close', 'close_binary', {'snapshot_path': str(asi / 'snapshot.json')})

        br = DEST / 'BetterRayman3.dll'
        br.mkdir()
        tool(br, '00-open', 'open_binary', {'path': str(INPUTS / 'BetterRayman3.dll'), 'provider_id': 'ghidra'})
        for label, address in [('50-ubiini-string', '0x10039734'), ('51-widescreen-header-string', '0x100395f8'),
                               ('52-bettername-string', '0x10039798')]:
            doc = tool(br, label, 'xrefs', {'address': address})
            for index, caller in enumerate(callers_of(doc)[:3]):
                tool(br, f'{label}-fn-{index}', 'analyze_function', {'procedure': caller})
        tool(br, '99-close', 'close_binary', {'snapshot_path': str(br / 'snapshot.json')})
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
