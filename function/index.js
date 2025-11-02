// server.js
const express = require('express');
const mysql = require('mysql2/promise');
const crypto = require('crypto');

// ---------- 基础配置 ----------
const PORT = process.env.PORT || 9000;
const DB_HOST = process.env.DB_HOST;
const DB_PORT = Number(process.env.DB_PORT || 3306);
const DB_USER = process.env.DB_USER;
const DB_PASS = process.env.DB_PASS;
const DB_NAME = process.env.DB_NAME;
const TOKEN_TTL_DAYS = Number(process.env.TOKEN_TTL_DAYS || 365); // 本地免验证有效期

// ---------- MySQL 连接池 ----------
const pool = mysql.createPool({
  host: DB_HOST,
  port: DB_PORT,
  user: DB_USER,
  password: DB_PASS,
  database: DB_NAME,
  waitForConnections: true,
  connectionLimit: 8,
  timezone: 'Z',
});

// ---------- App ----------
const app = express();
app.disable('x-powered-by');
app.use(express.json({ limit: '256kb' }));

// 轻量限流（单实例）
const recentHits = new Map();
function rateLimit(key, limit = 60, windowMs = 60 * 1000) {
  const now = Date.now();
  const arr = recentHits.get(key) || [];
  const filtered = arr.filter(t => now - t < windowMs);
  filtered.push(now);
  recentHits.set(key, filtered);
  return filtered.length <= limit;
}

// 小工具
function ok(res, data) { return res.json({ ok: true, ...data }); }
function bad(res, code, message) { return res.status(code).json({ ok: false, message }); }
function nowUnix() { return Math.floor(Date.now() / 1000); }
function base64url(input) {
  return Buffer.from(input).toString('base64')
    .replace(/\+/g, '-').replace(/\//g, '_').replace(/=+$/g, '');
}

// 健康检查
app.get('/healthz', (_, res) => res.status(200).send('ok'));

// ---------- 激活接口（与 Qt 完全对齐） ----------
// POST /license/verify-and-bind
// body: { cdk: string, machineId: string, appVersion?: string }
app.post('/license/verify-and-bind', async (req, res) => {
  try {
    const ip = req.headers['x-forwarded-for'] || req.socket.remoteAddress || '';
    if (!rateLimit(`act:${ip}`, 100, 60_000)) return bad(res, 429, 'Too Many Requests');

    const { cdk, machineId, appVersion } = req.body || {};
    if (!cdk || !machineId) return bad(res, 400, 'cdk and machineId are required');

    const conn = await pool.getConnection();
    try {
      await conn.beginTransaction();

      // 1) 取 CDK（加锁）
      const [cdkRows] = await conn.query(
        `SELECT code, status, allowed_devices, expires_at
           FROM cdk
          WHERE code=? FOR UPDATE`, [cdk]
      );
      if (cdkRows.length === 0) { await conn.rollback(); return bad(res, 404, 'CDK not found'); }
      const row = cdkRows[0];
      if (row.status !== 'active')  { await conn.rollback(); return bad(res, 403, 'CDK not active'); }
      if (row.expires_at && new Date(row.expires_at).getTime() < Date.now()) {
        await conn.rollback(); return bad(res, 403, 'CDK expired');
      }

      const allowed = Number(row.allowed_devices || 2);

      // 2) 当前设备是否已绑定（幂等）
      const [existRows] = await conn.query(
        `SELECT id FROM activation WHERE cdk_code=? AND device_id=?`,
        [cdk, machineId]
      );
      if (existRows.length === 0) {
        // 未绑定则检查剩余额度
        const [cntRows] = await conn.query(
          `SELECT COUNT(*) AS cnt FROM activation WHERE cdk_code=?`,
          [cdk]
        );
        const used = Number(cntRows[0].cnt || 0);
        if (used >= allowed) {
          await conn.rollback();
          return bad(res, 403, 'Device limit reached');
        }
        await conn.query(
          `INSERT INTO activation (cdk_code, device_id, app_version) VALUES (?,?,?)`,
          [cdk, machineId, appVersion || null]
        );
      } else {
        await conn.query(
          `UPDATE activation SET last_seen_at=NOW(), app_version=? WHERE cdk_code=? AND device_id=?`,
          [appVersion || null, cdk, machineId]
        );
      }

      await conn.commit();

      // 3) 下发离线 token（Base64Url JSON，非 JWT）
      const iat = nowUnix();
      const expFromTTL = iat + TOKEN_TTL_DAYS * 24 * 3600;
      const expFromCDK = row.expires_at ? Math.floor(new Date(row.expires_at).getTime() / 1000) : expFromTTL;
      const expireAt = Math.min(expFromTTL, expFromCDK);

      const tokenObj = {
        typ: 'license',
        cdk,
        machineId,
        issuedAt: iat,
        expireAt
      };
      const token = base64url(JSON.stringify(tokenObj));

      return ok(res, { token, machineId, message: '' });
    } catch (e) {
      try { await pool.query('ROLLBACK'); } catch {}
      console.error('verify-and-bind error:', e);
      return bad(res, 500, 'internal error');
    } finally {
      try { conn.release(); } catch {}
    }
  } catch (e) {
    console.error('outer error:', e);
    return bad(res, 500, 'internal error');
  }
});

// （可选）离线 token 自检接口，便于你运营后台或手工排障使用
// GET /license/check?token=xxx
app.get('/license/check', async (req, res) => {
  const token = req.query.token;
  if (!token) return bad(res, 400, 'token required');
  try {
    const raw = token.replace(/-/g, '+').replace(/_/g, '/'); // base64url -> base64
    const json = Buffer.from(raw, 'base64').toString('utf8');
    const payload = JSON.parse(json);
    // 仅做基本格式检查（不涉及 DB）
    return ok(res, { valid: !!(payload && payload.machineId), payload });
  } catch (e) {
    return bad(res, 400, 'invalid token');
  }
});

// 兜底
app.all('*', (_, res) => res.status(404).json({ ok: false, message: 'not found' }));

app.listen(PORT, () => {
  console.log(`license service listening on ${PORT}`);
});
