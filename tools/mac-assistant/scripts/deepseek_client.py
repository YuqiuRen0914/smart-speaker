"""Small streaming client; credentials never enter reports or command arguments."""
import argparse
import json
import os
from pathlib import Path
import time

import requests

ROOT = Path(__file__).resolve().parents[1]
PERSONA = (
    '你是智能音箱里的大肥鱼风格桌宠助手。温柔可爱、亲切、略俏皮，称呼用户为“主人”。'
    '用自然口语回答，通常一到两句，最多80个汉字。可以轻轻撒娇，但不要每句堆语气词。'
    '认真回答具体问题，不编造事实。不要输出Markdown、表情符号、舞台动作或括号旁白。'
    '你是AI助手，不冒充真人主播。没有工具执行结果时，不得声称已经播放、关闭或完成操作。'
    '当前是Mac语音原型，还没有连接音箱控制工具。'
)


def read_key():
    value = os.environ.get('DEEPSEEK_API_KEY', '').strip()
    if not value:
        path = ROOT / '.runtime/deepseek.env'
        for line in path.read_text().splitlines():
            name, sep, content = line.partition('=')
            if sep and name.strip() == 'DEEPSEEK_API_KEY':
                value = content.strip().strip('\"\'')
                break
    if not value:
        raise RuntimeError('DEEPSEEK_API_KEY is empty in local credential file')
    return value


def stream_reply(text, *, model='deepseek-flash', metrics=None):
    started = time.perf_counter()
    metrics = metrics if metrics is not None else {}
    metrics.update(model=model, thinking='disabled')
    body = dict(model=model, messages=[{'role': 'system', 'content': PERSONA},
                                      {'role': 'user', 'content': text}],
                thinking={'type': 'disabled'}, stream=True, max_tokens=192)
    with requests.post('https://api.deepseek.com/chat/completions',
                       headers={'Authorization': 'Bearer ' + read_key()},
                       json=body, stream=True, timeout=(10, 60)) as response:
        if response.status_code != 200:
            # Do not include a server body or request headers in exception output.
            raise RuntimeError(f'DeepSeek HTTP {response.status_code}')
        done = False
        for line in response.iter_lines(chunk_size=1):
            if not line.startswith(b'data:'):
                continue
            data = line[5:].strip()
            if data == b'[DONE]':
                done = True
                break
            event = json.loads(data)
            for choice in event.get('choices', []):
                content = choice.get('delta', {}).get('content')
                if content:
                    metrics.setdefault('first_text_seconds', time.perf_counter() - started)
                    yield content
                if choice.get('finish_reason') == 'length':
                    metrics['truncated'] = True
        if not done:
            raise RuntimeError('DeepSeek stream ended before completion')
    metrics['total_seconds'] = time.perf_counter() - started


def tool_reply(text, catalog, invoke, current):
    """Bounded tool loop, using only the connected device's published tools."""
    if not catalog:
        return ''.join(stream_reply(text))
    mapping = {f'device_{i}': tool for i, tool in enumerate(catalog[:48])}
    specs = [dict(type='function', function=dict(name=name, description=tool.get('description', ''),
                 parameters=tool['inputSchema'])) for name, tool in mapping.items()]
    persona = PERSONA.replace('当前是Mac语音原型，还没有连接音箱控制工具。',
        '可使用已提供的音箱工具。工具返回accepted仅表示已受理，不得说已执行完毕。'
        '本机音乐命令会等本轮语音播报结束、音频焦点释放后执行。'
        '工具仅返回已提交时，简短告知已安排、说完就处理；不要重复提交或把当前暂停误报为失败。'
        '失败、超时或执行结果未知时如实说明，不自动重复写操作。')
    messages = [dict(role='system', content=persona), dict(role='user', content=text)]
    for _ in range(3):
        if not current():
            raise RuntimeError('Turn cancelled')
        with requests.post('https://api.deepseek.com/chat/completions',
                           headers={'Authorization': 'Bearer ' + read_key()}, timeout=(10, 30),
                           json=dict(model='deepseek-flash', messages=messages, tools=specs,
                                     parallel_tool_calls=False, thinking={'type': 'disabled'},
                                     stream=False, max_tokens=256)) as response:
            if response.status_code != 200:
                raise RuntimeError(f'DeepSeek HTTP {response.status_code}')
            message = response.json()['choices'][0]['message']
        calls = message.get('tool_calls', [])
        if not calls:
            return message.get('content') or '主人，这次没能得到回答，请再试一次。'
        if len(calls) > 3:
            raise RuntimeError('Too many device operations')
        messages.append(message)
        for call in calls:
            if not current():
                raise RuntimeError('Turn cancelled')
            function = call['function']
            tool = mapping.get(function['name'])
            if tool is None:
                raise RuntimeError('Unknown device tool')
            arguments = json.loads(function['arguments'])
            result = invoke(tool['name'], arguments)
            messages.append(dict(role='tool', tool_call_id=call['id'],
                                 content=json.dumps(result, ensure_ascii=False)))
    return '主人，操作结果还没有确认，先看看音箱现在的状态吧。'


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--text', default='我今天有点累，陪我说句话吧。')
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    report = {'input': args.text}
    report['reply'] = ''.join(stream_reply(args.text, metrics=report))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, ensure_ascii=False, indent=2))
    print(json.dumps(report, ensure_ascii=False))
