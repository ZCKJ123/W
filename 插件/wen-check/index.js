import { execFile } from 'node:child_process';
import { promisify } from 'node:util';
import { createHash } from 'node:crypto';
import { createWriteStream } from 'node:fs';
import { mkdir, rename, stat, unlink } from 'node:fs/promises';
import { homedir } from 'node:os';
import { isAbsolute, join, resolve } from 'node:path';

const runFile = promisify(execFile);

const LEGACY_EXE = 'C:\\Users\\Acer\\Desktop\\zhongwen_lang\\wen.exe';
const MAX_LIMIT = 200;

function cachePath(config) {
  const dir = (config && typeof config.cacheDir === 'string' && config.cacheDir) || join(homedir(), '.dsh-wen-check');
  return { dir, file: join(dir, 'wen.exe') };
}

function exeCandidates(config, cached) {
  const list = [
    config && typeof config.exePath === 'string' ? config.exePath : '',
    process.env.WEN_EXE || '',
    cached || '',
    'wen',
    'wen.exe',
    LEGACY_EXE,
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

// Fallback for wen.exe builds that predate --json: parse the four-section text.
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
    for (let i = 0; i < starts.length && i < limit; i++) {
      chunks.push(text.slice(starts[i], i + 1 < starts.length ? starts[i + 1] : text.length));
    }
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
    diagnostics.push(entry);
  }
  return diagnostics;
}

export const inject = ['tools'];

export function apply(ctx, config) {
  const definition = {
    name: 'wen_check',
    description:
      'Check a 文语言 (.wen) script with the wen compiler and return its structured diagnostics: kind, line, column, reason, and the offending source line. Use it after writing or editing .wen code instead of guessing whether the script parses.',
    parameters: {
      type: 'object',
      properties: {
        path: {
          type: 'string',
          description:
            'Absolute path to the .wen file. Relative paths resolve against the DSH process directory rather than the session workspace, so prefer absolute paths.',
        },
        limit: {
          type: 'integer',
          description: `Maximum number of diagnostics to return, 1-${MAX_LIMIT}. Defaults to 20.`,
        },
      },
      required: ['path'],
      additionalProperties: false,
    },
    output: {
      schema: {
        type: 'object',
        properties: {
          ok: { type: 'boolean', description: 'True when the script parses.' },
          file: { type: 'string' },
          statementCount: { type: 'integer', description: 'Top-level statement count reported on success.' },
          exitCode: { type: 'integer' },
          diagnostics: {
            type: 'array',
            items: {
              type: 'object',
              properties: {
                kind: { type: 'string' },
                line: { type: 'integer' },
                column: { type: 'integer' },
                reason: { type: 'string' },
                detail: { type: 'string' },
                sourceLine: {
                  type: 'object',
                  properties: {
                    number: { type: 'integer' },
                    text: { type: 'string' },
                  },
                  additionalProperties: false,
                },
              },
              additionalProperties: false,
            },
          },
          truncated: { type: 'boolean' },
          error: { type: 'string', description: 'Present when the compiler could not run or could not be downloaded.' },
        },
        additionalProperties: false,
      },
      render(args, value) {
        return [{ type: 'text', text: JSON.stringify(value, null, 2) }];
      },
    },
    async execute(args) {
      const requested = String(args?.path ?? '').trim();
      if (!requested) throw new Error('path is required');
      const file = isAbsolute(requested) ? requested : resolve(process.cwd(), requested);
      const limit = Math.min(MAX_LIMIT, Math.max(1, Number(args?.limit ?? 20) || 20));

      let cached = '';
      let downloadError = '';
      try {
        cached = await ensureDownloaded(config);
      } catch (err) {
        downloadError = err?.message ?? String(err);
      }

      const candidates = exeCandidates(config, cached);
      let stdout = '';
      let stderr = '';
      let exitCode = 0;
      let usedExe = '';
      for (const exe of candidates) {
        try {
          const result = await runFile(exe, ['--检查', '--json', file], {
            encoding: 'utf8',
            timeout: 30000,
            windowsHide: true,
            maxBuffer: 8 * 1024 * 1024,
          });
          stdout = result.stdout ?? '';
          stderr = result.stderr ?? '';
          usedExe = exe;
          break;
        } catch (err) {
          if (err && err.code === 'ENOENT') continue;
          if (err && typeof err === 'object' && 'code' in err && (typeof err.code === 'number' || err.killed)) {
            exitCode = typeof err.code === 'number' ? err.code : -1;
            stdout = err.stdout ?? '';
            stderr = err.stderr ?? '';
            usedExe = exe;
            break;
          }
          return {
            ok: false,
            file,
            exitCode: -1,
            statementCount: 0,
            diagnostics: [],
            truncated: false,
            error: `无法运行 wen 检查器（${exe}）：${err?.message ?? String(err)}`,
          };
        }
      }
      if (!usedExe) {
        return {
          ok: false,
          file,
          exitCode: -1,
          statementCount: 0,
          diagnostics: [],
          truncated: false,
          error:
            `找不到 wen 检查器（试过：${candidates.join('、')}）。把 wen.exe 放进 PATH，或设置 config.exePath / WEN_EXE / config.downloadUrl。` +
            (downloadError ? ` 下载失败：${downloadError}` : ''),
        };
      }

      const trimmed = stdout.trim();
      if (trimmed.startsWith('{')) {
        try {
          const payload = JSON.parse(trimmed);
          const all = Array.isArray(payload.diagnostics) ? payload.diagnostics : [];
          const shown = all.slice(0, limit);
          return {
            ok: Boolean(payload.ok),
            file: typeof payload.file === 'string' ? payload.file : file,
            exitCode,
            statementCount: Number(payload.statementCount ?? 0) || 0,
            diagnostics: shown,
            truncated: all.length > shown.length,
          };
        } catch {
          // fall through to text parsing
        }
      }

      const diagnostics = parseTextDiagnostics(stderr, limit);
      const totalHeader = /发现\s*(\d+)\s*个错误/.exec(stderr);
      const statementCount = Number(/语法通过：顶层\s*(\d+)\s*条语句/.exec(stdout)?.[1] ?? 0);
      return {
        ok: exitCode === 0,
        file,
        exitCode,
        statementCount,
        diagnostics,
        truncated: Boolean(totalHeader && Number(totalHeader[1]) > diagnostics.length),
      };
    },
  };

  ctx.effect(() => ctx.tools.register(definition));
}
