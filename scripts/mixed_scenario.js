import http from 'k6/http';
import { check, sleep } from 'k6';
import { Rate, Trend } from 'k6/metrics';

// 自定义指标
const errorRate = new Rate('errors');
const loginDuration = new Trend('login_duration');
const profileDuration = new Trend('profile_duration');
const achievementDuration = new Trend('achievement_duration');
const leaderboardDuration = new Trend('leaderboard_duration');
const currencyDuration = new Trend('currency_duration');

export const options = {
  stages: [
    { duration: '1m', target: 20 },    // 预热：1分钟爬升到20用户
    { duration: '3m', target: 50 },    // 逐步加压到50用户
    { duration: '5m', target: 50 },    // 保持50用户5分钟
    { duration: '3m', target: 100 },   // 加压到100用户
    { duration: '5m', target: 100 },   // 保持100用户5分钟
    { duration: '2m', target: 0 },     // 逐步降压
  ],
  thresholds: {
    http_req_duration: ['p(95)<500', 'p(99)<1000'],
    errors: ['rate<0.01'],
  },
};

const AUTH_URL = __ENV.AUTH_URL || 'http://localhost:8083';
const USER_URL = __ENV.USER_URL || 'http://localhost:8082';
const GAME_DATA_URL = __ENV.GAME_DATA_URL || 'http://localhost:8084';

export default function () {
  const userIndex = __VU;
  const username = `loadtest_user_${userIndex}`;
  const password = 'Test@12345';

  // ========== 1. 登录 ==========
  const loginRes = http.post(`${AUTH_URL}/api/v1/auth/login`, JSON.stringify({
    username: username,
    password: password,
  }), {
    headers: { 'Content-Type': 'application/json' },
  });

  loginDuration.add(loginRes.timings.duration);
  check(loginRes, {
    'login status 200': (r) => r.status === 200,
    'login has token': (r) => {
      try { return r.json('data.access_token') !== undefined; }
      catch { return false; }
    },
  }) || errorRate.add(1);

  if (loginRes.status !== 200) return;

  let token, userId;
  try {
    const loginData = loginRes.json('data');
    token = loginData.access_token;
    userId = loginData.user?.user_id || username;
  } catch {
    return;
  }

  const authHeaders = {
    headers: {
      'Authorization': `Bearer ${token}`,
      'Content-Type': 'application/json',
    },
  };

  sleep(0.5);

  // ========== 2. 获取游戏档案 ==========
  const profileRes = http.get(
    `${GAME_DATA_URL}/api/v1/gamedata/profiles/${userId}`,
    authHeaders
  );
  profileDuration.add(profileRes.timings.duration);
  check(profileRes, { 'profile status 200': (r) => r.status === 200 }) || errorRate.add(1);

  sleep(0.2);

  // ========== 3. 获取成就列表 ==========
  const achievementRes = http.get(
    `${GAME_DATA_URL}/api/v1/gamedata/achievements/${userId}`,
    authHeaders
  );
  achievementDuration.add(achievementRes.timings.duration);
  check(achievementRes, { 'achievements ok': (r) => r.status === 200 }) || errorRate.add(1);

  sleep(0.2);

  // ========== 4. 获取货币 ==========
  const currencyRes = http.get(
    `${GAME_DATA_URL}/api/v1/gamedata/currency/${userId}`,
    authHeaders
  );
  currencyDuration.add(currencyRes.timings.duration);
  check(currencyRes, { 'currency ok': (r) => r.status === 200 }) || errorRate.add(1);

  sleep(0.3);

  // ========== 5. 获取排行榜 ==========
  const leaderboardRes = http.get(
    `${GAME_DATA_URL}/api/v1/gamedata/leaderboard/rating/gomoku?limit=50`,
    authHeaders
  );
  leaderboardDuration.add(leaderboardRes.timings.duration);
  check(leaderboardRes, { 'leaderboard ok': (r) => r.status === 200 }) || errorRate.add(1);

  sleep(1);
}
