import subprocess, json, pathlib, sys

WORKSPACE = pathlib.Path.home() / 'Projects/rayman3-rtx-analysis/hooks_0s8j6upd'
EVIDENCE = WORKSPACE / 'evidence'
INPUTS = WORKSPACE / 'inputs'


def rpc_call(proc, request_id, method, params):
    proc.stdin.write(json.dumps({'jsonrpc': '2.0', 'id': request_id, 'method': method, 'params': params}) + '\n')
    proc.stdin.flush()
    while True:
        line = proc.stdout.readline()
        if not line:
            raise RuntimeError('owned MCP process ended unexpectedly')
        message = json.loads(line)
        if message.get('id') == request_id:
            return message


def unpack(response):
    result = response.get('result', {})
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


def matches(document):
    normalized = document.get('normalized_result', {})
    return normalized if isinstance(normalized, list) else []


def main():
    destination = EVIDENCE / 'pass2'
    destination.mkdir()
    environment = dict(__import__('os').environ)
    environment.update(GHIDRA_INSTALL_DIR='/opt/ghidra', JAVA_HOME='/usr/lib/jvm/java-21-openjdk')
    stderr_log = (destination / 'mcp-stderr.log').open('w')
    proc = subprocess.Popen([
        'node', '/home/suraj/.npm/_npx/af752b90495482d7/node_modules/rea-agents/scripts/rea.mjs', 'mcp'
    ], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=stderr_log, text=True, env=environment)
    request_id = 0

    def tool(target, label, name, arguments):
        nonlocal request_id
        request_id += 1
        response = rpc_call(proc, request_id, 'tools/call', {'name': name, 'arguments': arguments})
        (target / f'{label}.json').write_text(json.dumps(response, indent=2))
        document = unpack(response)
        failure = response.get('error') or response.get('result', {}).get('isError')
        print(json.dumps({'target': target.name, 'operation': name, 'label': label,
                          'evidence_id': document.get('evidence_id') if isinstance(document, dict) else None,
                          'error': failure}), flush=True)
        if failure:
            raise RuntimeError(f'{name} failed: {json.dumps(failure)[:600]}')
        return document

    try:
        rpc_call(proc, 1, 'initialize', {'protocolVersion': '2024-11-05', 'capabilities': {},
                                         'clientInfo': {'name': 'rayman-static-analysis', 'version': '1'}})
        request_id = 1
        proc.stdin.write(json.dumps({'jsonrpc': '2.0', 'method': 'notifications/initialized'}) + '\n')
        proc.stdin.flush()

        targets = ['BetterRayman3.dll', 'UltimateASILoader-winmm.dll', 'ALchemy-dsound.dll']
        for name in targets:
            target = destination / name
            target.mkdir()
            tool(target, '00-open', 'open_binary', {'path': str(INPUTS / name), 'provider_id': 'ghidra'})
            if name == 'BetterRayman3.dll':
                tool(target, '10-imported-apis', 'search_procedures', {'pattern': 'GetProcAddress|VirtualProtect|SetWindow|LoadLibrary|CreateWindow', 'mode': 'regex'})
                tool(target, '11-behaviour-strings', 'search_strings', {'pattern': 'Rayman|wndproc|window|fullscreen|resolution|fov|camera|splash|config|asi', 'mode': 'regex'})
                tool(target, '12-api-names', 'search_strings', {'pattern': 'GetProcAddress|VirtualProtect|SetWindowLong|LoadLibrary', 'mode': 'regex'})
            elif name == 'UltimateASILoader-winmm.dll':
                tool(target, '10-loader-api', 'search_procedures', {'pattern': 'LoadLibrary|GetProcAddress|PlaySound|timeGetTime|mmio|SetWindow|SetWindowsHook', 'mode': 'regex'})
                tool(target, '11-loader-strings', 'search_strings', {'pattern': '\.asi|plugins|scripts|LoadLibrary|LoadLibraryEx|VirtualProtect|SetWindow|GetProcAddress|dinput|winmm', 'mode': 'regex'})
                tool(target, '12-loader-exports', 'search_procedures', {'pattern': 'DllMain|CreateFile|FindFirstFile|ReadFile', 'mode': 'regex'})
            else:
                tool(target, '10-sound-api', 'search_procedures', {'pattern': 'DirectSound|SetWindow|GetProcAddress|LoadLibrary|CoCreate|RegOpen', 'mode': 'regex'})
                tool(target, '11-sound-strings', 'search_strings', {'pattern': 'DirectSound|SetWindow|GetProcAddress|LoadLibrary|CoCreate|RegOpen|ray|globalsettings|alchemy', 'mode': 'regex'})
            tool(target, '99-close', 'close_binary', {'snapshot_path': str(target / 'analysis-snapshot.json')})
    finally:
        proc.stdin.close()
        try:
            proc.wait(timeout=20)
        except subprocess.TimeoutExpired:
            proc.terminate()
            proc.wait(timeout=20)
        stderr_log.close()


if __name__ == '__main__':
    main()
