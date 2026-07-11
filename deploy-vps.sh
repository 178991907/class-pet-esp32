#!/usr/bin/env bash
# 班级宠物园 · Route B 语音服务部署脚本 (部署到任意你能 SSH 的 VPS, 含国外节点)
#
# 前置条件:
#   1) VPS 已装 git + Node.js 18+ (node -v 确认)
#   2) 你已把代码 push 到 GitHub 仓库 (本脚本默认 https://github.com/178991907/class-pet-esp32.git)
#      - 若仓库是私有的, 把下面 REPO 改成带 token 的地址, 或在 VPS 上配好 SSH key
#   3) 你知道 Neon 数据库连接串 DATABASE_URL (与 Vercel 共用同一个)
#
# 用法:
#   # 在你自己电脑上, 把脚本传到 VPS:
#   scp deploy-vps.sh root@<VPS_IP>:/root/
#   # 登录 VPS 运行:
#   ssh root@<VPS_IP>
#   bash deploy-vps.sh
#
set -e

APP_DIR=~/class-pet-server
REPO=https://github.com/178991907/class-pet-esp32.git
PORT=3003

echo "== 1. 拉取最新代码 =="
if [ -d "$APP_DIR/.git" ]; then
  cd "$APP_DIR" && git pull --ff-only
else
  git clone "$REPO" "$APP_DIR"
  cd "$APP_DIR"
fi

echo "== 2. 安装服务端依赖 (只装 server/) =="
cd "$APP_DIR/server"
npm install --omit=dev

echo "== 3. 写入环境变量 (.env) =="
ENV_FILE="$APP_DIR/server/.env"
if [ ! -f "$ENV_FILE" ]; then
  echo "请输入 Neon 数据库连接串 DATABASE_URL (输入时不显示, 粘贴后回车):"
  read -rs DATABASE_URL
  printf 'DATABASE_URL=%s\n' "$DATABASE_URL" > "$ENV_FILE"
  printf 'VERCEL=\n' >> "$ENV_FILE"      # 必须留空! 设成 1 服务就不会 listen
  printf 'PORT=%s\n' "$PORT" >> "$ENV_FILE"
  echo "已写入 $ENV_FILE"
else
  echo "已存在 .env, 跳过 (如需修改请手动编辑 $ENV_FILE)"
fi

echo "== 4. 用 pm2 常驻启动 =="
npm install -g pm2 >/dev/null 2>&1 || true
set -a; source "$ENV_FILE"; set +a
cd "$APP_DIR/server"
pm2 delete classpet-voice 2>/dev/null || true
pm2 start "node index.js" --name classpet-voice
pm2 save
pm2 startup >/dev/null 2>&1 || true

echo "== 5. 本机自检 =="
sleep 2
curl -s "http://localhost:$PORT/api/health" || echo "(健康检查失败, 看 pm2 logs classpet-voice)"

echo ""
echo "=========================================================="
echo "✅ 部署完成。接下来两步在 Cloudflare 后台做 (不用动固件):"
echo "  1) VPS 防火墙放行 $PORT (如: ufw allow $PORT)"
echo "  2) Cloudflare → Rules → Origin Rules → 新建规则:"
echo "       条件: Hostname = pete.qqzy.de5.net 且 URI Path 匹配 /ws/voice*"
echo "       动作: Override origin → IP 地址 = <VPS_IP>, 端口 = $PORT, Scheme = HTTP"
echo "  设备 server_url 保持 https://pete.qqzy.de5.net, 重启设备即自动走 Route B。"
echo "=========================================================="
