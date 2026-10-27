import { execFile } from 'node:child_process';
import { promisify } from 'node:util';
import { createHash } from 'node:crypto';
import { createWriteStream } from 'node:fs';
import { mkdir, mkdtemp, rename, rm, stat, unlink, writeFile } from 'node:fs/promises';
import { homedir, tmpdir } from 'node:os';
import { dirname, isAbsolute, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const runFile = promisify(execFile);
const PLUGIN_DIR = dirname(fileURLToPath(import.meta.url));
const MAX_LIMIT = 200;

function cachePath(config) {
  const dir = (config && typeof config.cacheDir === 'string' && config.cacheDir) || join(homedir(), '.dsh-wen-check');
  return { dir, file: join(dir, 'wen.exe') };
}

function exeCandidates(config, cached) {
  const list = [
    config && typeof config.exePath === 'string' ? config.exePath : '',
    process.env.WEN_EXE || '',
    join(PLUGIN_DIR, 'wen.exe'),
    cached || '',
    'wen',
    'wen.exe',
  ];
  return [...new Set(list.filter(Boolean))];
}

async function ensureDownloaded(config, log) {
  const url = config && typeof config.downloadUrl === 'string' ? config.downloadUrl.trim() : '';
  if (!url) return '';
  const { dir, file } = cachePath(config);
  try {
    const st = await stat(file);
    if (st.size > 0) return file;
  } catch {
    // not cached yet
  }
  await mkdir(dir, { recursive: true });
  const tmp = `${file}.download`;
  const response = await fetch(url, { redirect: 'follow' });
  if (!response.ok || !response.body) throw new Error(`下载失败：HTTP ${response.status}`);
  const hash = createHash('sha256');
  const sink = createWriteStream(tmp);
  let bytes = 0;
  for await (const chunk of response.body) {
    hash.update(chunk);
    bytes += chunk.length;
    if (!sink.write(chunk)) await new Promise((r) => sink.once('drain', r));
  }
  await new Promise((r, j) => sink.end((err) => (err ? j(err) : r())));
  const expected = config && typeof config.sha256 === 'string' ? config.sha256.trim().toLowerCase() : '';
  if (expected) {
    const actual = hash.digest('hex');
    if (actual !== expected) {
      await unlink(tmp).catch(() => {});
      throw new Error(`校验失败：期望 ${expected}，实际 ${actual}`);
    }
  }
  await rename(tmp, file);
  log?.(`下载完成：${file}（${(bytes / 1024 / 1024).toFixed(1)} MB）`);
  return file;
}

function hintFor(d) {
  const text = `${d.reason || ''} ${d.detail || ''}`;
  if (/运算符不能当名字|运算符「/.test(text)) return '换一个不是运算符的名字，例如「相加」「平方算」。';
  if (/未定义|不存在/.test(text)) return '先赋值或定义，再引用；检查拼写。';
  if (/依赖|属于「.*库」/.test(text)) return '在文件开头补「依赖 库名」。';
  if (/下标|越界/.test(text)) return '检查下标范围；负下标从末尾算。';
  if (/参数/.test(text)) return '检查函数参数个数和类型。';
  if (/连续空了两行|空行/.test(text)) return '删掉多余空行；块内最多空一格。';
  if (/期望「则」|缺少「结束」|没有写完|需要.*结束/.test(text))
    return '在「如果/函数/对于/尝试」块末尾补「结束」；条件后补「则」。';
  return '';
}

function withHint(d) {
  const hint = d.hint || hintFor(d);
  return hint ? { ...d, hint } : d;
}

function parseTextDiagnostics(stderr, limit) {
  const text = (stderr || '').replace(/\r\n/g, '\n');
  const diagnostics = [];
  const startRe = /错误\s*(\d+)\s*\/\s*(\d+)/g;
  const starts = [];
  let m;
  while ((m = startRe.exec(text)) !== null) starts.push(m.index);
  const chunks = [];
  if (starts.length === 0) {
    if (text.includes('【')) chunks.push(text);
  } else {
    for (let i = 0; i < starts.length && i < limit; i++)
      chunks.push(text.slice(starts[i], i + 1 < starts.length ? starts[i + 1] : text.length));
  }
  for (const chunk of chunks) {
    if (diagnostics.length >= limit) break;
    const kind = /【(.+?)】/.exec(chunk);
    const pos = /位置：(.+?)\s+第\s*(\d+)\s*行(?:\s*第\s*(\d+)\s*列)?/.exec(chunk);
    const reason = /原因：([^\n]*)/.exec(chunk);
    const detail = /说明：([^\n]*)/.exec(chunk);
    const code = /^\s*(\d+)\s*\|\s?(.*)$/m.exec(chunk);
    const entry = {
      kind: kind ? kind[1] : '',
      line: pos ? Number(pos[2]) : 0,
      column: pos && pos[3] ? Number(pos[3]) : 0,
      reason: reason ? reason[1].trim() : '',
      detail: detail ? detail[1].trim() : '',
    };
    if (code) entry.sourceLine = { number: Number(code[1]), text: code[2] };
    diagnostics.push(withHint(entry));
  }
  return diagnostics;
}

async function runWen(args, config, timeoutMs) {
  let cached = '';
  let downloadError = '';
  try {
    cached = await ensureDownloaded(config);
  } catch (err) {
    downloadError = err?.message ?? String(err);
  }
  const candidates = exeCandidates(config, cached);
  for (const exe of candidates) {
    try {
      const result = await runFile(exe, args, {
        encoding: 'utf8',
        timeout: timeoutMs,
        windowsHide: true,
        maxBuffer: 32 * 1024 * 1024,
      });
      return { exe, exitCode: 0, stdout: result.stdout ?? '', stderr: result.stderr ?? '' };
    } catch (err) {
      if (err && err.code === 'ENOENT') continue;
      if (err && typeof err === 'object' && 'code' in err && (typeof err.code === 'number' || err.killed)) {
        return {
          exe,
          exitCode: typeof err.code === 'number' ? err.code : -1,
          stdout: err.stdout ?? '',
          stderr: err.stderr ?? '',
        };
      }
      return { error: `无法运行 wen（${exe}）：${err?.message ?? String(err)}` };
    }
  }
  return {
    error:
      `找不到 wen（试过：${candidates.join('、')}）。把 wen.exe 放进 PATH，或设置 config.exePath / WEN_EXE。` +
      (downloadError ? ` 下载失败：${downloadError}` : ''),
  };
}

function commonOutput(extra = {}) {
  return {
    type: 'object',
    properties: {
      ok: { type: 'boolean' },
      file: { type: 'string' },
      exitCode: { type: 'integer' },
      error: { type: 'string' },
      ...extra,
    },
    additionalProperties: false,
  };
}

export const inject = ['tools'];

export function apply(ctx, config) {
  const checkDefinition = {
    name: 'wen_check',
    description:
      'Check 文语言 (.脚本 / .wen) code with the wen compiler and return structured diagnostics with fixes: kind, line, column, reason, hint, and source line. Accepts a file path or an inline source string.',
    parameters: {
      type: 'object',
      properties: {
        path: {
          type: 'string',
          description: 'Absolute path to a .脚本 or .wen file. Prefer absolute paths.',
        },
        source: {
          type: 'string',
          description: 'Inline 文语言 source to check; written to a temp .脚本 file automatically.',
        },
        limit: {
          type: 'integer',
          description: `Maximum number of diagnostics to return, 1-${MAX_LIMIT}. Defaults to 20.`,
        },
        timeoutMs: {
          type: 'integer',
          description: 'Compiler timeout in milliseconds. Defaults to 30000.',
        },
      },
      required: [],
      additionalProperties: false,
    },
    output: {
      schema: commonOutput({
        statementCount: { type: 'integer' },
        diagnostics: {
          type: 'array',
          items: {
            type: 'object',
            properties: {
              kind: { type: 'string' },
              code: { type: 'string' },
              rule: { type: 'string' },\n              frames: { type: 'array', items: { type: 'string' } },
              severity: { type: 'string' },
              line: { type: 'integer' },
              column: { type: 'integer' },
              reason: { type: 'string' },
              detail: { type: 'string' },
              hint: { type: 'string' },
              sourceLine: {
                type: 'object',
                properties: { number: { type: 'integer' }, text: { type: 'string' } },
                additionalProperties: false,
              },
            },
            additionalProperties: false,
          },
        },
        truncated: { type: 'boolean' },
      }),
      render(args, value) {
        return [{ type: 'text', text: JSON.stringify(value, null, 2) }];
      },
    },
    async execute(args) {
      const source = typeof args?.source === 'string' ? args.source : '';
      const requested = String(args?.path ?? '').trim();
      if (!source && !requested) throw new Error('path or source is required');
      const limit = Math.min(MAX_LIMIT, Math.max(1, Number(args?.limit ?? 20) || 20));
      const timeoutMs = Math.min(120000, Math.max(1000, Number(args?.timeoutMs ?? 30000) || 30000));

      let tempDir = '';
      let file = requested;
      try {
        if (source) {
          tempDir = await mkdtemp(join(tmpdir(), 'dsh-wen-'));
          file = join(tempDir, '片段.脚本');
          await writeFile(file, source, 'utf8');
        } else {
          file = isAbsolute(requested) ? requested : resolve(process.cwd(), requested);
        }
        const result = await runWen(['--检查', '--json', file], config, timeoutMs);
        if (result.error) {
          return { ok: false, file, exitCode: -1, statementCount: 0, diagnostics: [], truncated: false, error: result.error };
        }
        const trimmed = result.stdout.trim();
        if (trimmed.startsWith('{')) {
          try {
            const payload = JSON.parse(trimmed);
            const all = Array.isArray(payload.diagnostics) ? payload.diagnostics : [];
            const shown = all.slice(0, limit).map(withHint);
            return {
              ok: Boolean(payload.ok),
              file: typeof payload.file === 'string' ? payload.file : file,
              exitCode: result.exitCode,
              statementCount: Number(payload.statementCount ?? 0) || 0,
              diagnostics: shown,
              truncated: all.length > shown.length,
            };
          } catch {
            // fall through to text parsing
          }
        }
        const diagnostics = parseTextDiagnostics(result.stderr, limit);
        const totalHeader = /发现\s*(\d+)\s*个错误/.exec(result.stderr);
        const statementCount = Number(/语法通过：顶层\s*(\d+)\s*条语句/.exec(result.stdout)?.[1] ?? 0);
        return {
          ok: result.exitCode === 0,
          file,
          exitCode: result.exitCode,
          statementCount,
          diagnostics,
          truncated: Boolean(totalHeader && Number(totalHeader[1]) > diagnostics.length),
        };
      } finally {
        if (tempDir) await rm(tempDir, { recursive: true, force: true }).catch(() => {});
      }
    },
  };

  const runDefinition = {
    name: 'wen_run',
    description:
      'Run a 文语言 .脚本 / .wen file and return exitCode, stdout, stderr, and the first error line. Use it to try code like running Python, not just syntax-check it.',
    parameters: {
      type: 'object',
      properties: {
        path: {
          type: 'string',
          description: 'Absolute path to a .脚本 or .wen file. Prefer absolute paths.',
        },
        timeoutMs: {
          type: 'integer',
          description: 'Run timeout in milliseconds. Defaults to 30000.',
        },
      },
      required: ['path'],
      additionalProperties: false,
    },
    output: {
      schema: commonOutput({
        line: { type: 'integer', description: 'First error line parsed from stderr, 0 if none.' },
        stdout: { type: 'string' },
        stderr: { type: 'string' },
      }),
      render(args, value) {
        return [{ type: 'text', text: JSON.stringify(value, null, 2) }];
      },
    },
    async execute(args) {
      const requested = String(args?.path ?? '').trim();
      if (!requested) throw new Error('path is required');
      const file = isAbsolute(requested) ? requested : resolve(process.cwd(), requested);
      const timeoutMs = Math.min(120000, Math.max(1000, Number(args?.timeoutMs ?? 30000) || 30000));
      const result = await runWen([file], config, timeoutMs);
      if (result.error) return { ok: false, file, exitCode: -1, line: 0, stdout: '', stderr: '', error: result.error };
      const line = Number(/第\s*(\d+)\s*行/.exec(result.stderr)?.[1] ?? 0);
      return {
        ok: result.exitCode === 0,
        file,
        exitCode: result.exitCode,
        line,
        stdout: result.stdout,
        stderr: result.stderr,
      };
    },
  };

  ctx.effect(() => {
    const disposeCheck = ctx.tools.register(checkDefinition);
    const disposeRun = ctx.tools.register(runDefinition);
    return () => {
      disposeRun?.();
      disposeCheck?.();
    };
  });
}