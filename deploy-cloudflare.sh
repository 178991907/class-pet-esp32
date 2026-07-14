#!/usr/bin/env bash
# 班级宠物园 · Cloudflare 全栈部署脚本 (Worker + D1 + Pages)
#
# 前置条件:
#   1) 已安装 Node.js 18+ 与 npm
#   2) 已设置环境变量 CLOUDFLARE_API_TOKEN (具备 Workers/Durable Objects/D1/Pages/Workers AI 权限)
#      export CLOUDFLARE_API_TOKEN="<your-token>"
#   3) 已登录账户 (或依赖上面的 token): npx wrangler login
#
# 用法:
#   export CLOUDFLARE_API_TOKEN="xxxx"
#   bash deploy-cloudflare.sh
#
# 说明:
#   - Worker 代码在 ./cloudflare，使用本地安装的 wrangler@3 (避免 v4 被上级 pages.toml 误判)
#   - 前端在仓库根目录，构建后部署到 Cloudflare Pages 项目 class-pet-garden
#   - 数据库 classpet (D1) 的建表迁移必须加 --remote 才会写入线上库
set -e

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CF_DIR="$ROOT_DIR/cloudflare"
WRANGLER="$CF_DIR/node_modules/.bin/wrangler"

API_URL="${VITE_API_URL:-https://api.classe.ccwu.cc}"
PAGES_PROJECT="class-pet-garden"

echo "============================================================"
echo " 班级宠物园 · Cloudflare 部署"
echo " API_URL        = $API_URL"
echo " Pages 项目名   = $PAGES_PROJECT"
echo "============================================================"

if [ -z "$CLOUDFLARE_API_TOKEN" ]; then
  echo "❌ 未设置 CLOUDFLARE_API_TOKEN，请先 export 你的 Cloudflare API Token"
  exit 1
fi

if [ ! -x "$WRANGLER" ]; then
  echo "⚠️  未找到本地 wrangler@3，正在安装到 cloudflare/ ..."
  (cd "$CF_DIR" && npm install wrangler@3)
fi

echo ""
echo "== [1/4] 后端 D1 建表迁移 (含日历/清单/主人记忆) =="
(cd "$CF_DIR" && "$WRANGLER" d1 execute classpet --remote --file=./migrations/0002_features.sql)
echo "✅ D1 迁移完成"

echo ""
echo "== [2/4] 部署 Cloudflare Worker (classpet-api) =="
# 注意: 上级目录有 pages.toml，wrangler 4.x 会被误判为 Pages 工程；
#       此处显式 --config ./wrangler.toml 且使用本地 wrangler@3 规避
(cd "$CF_DIR" && "$WRANGLER" deploy --config ./wrangler.toml)
echo "✅ Worker 部署完成"

echo ""
echo "== [3/4] 构建前端 (Vue3) =="
cd "$ROOT_DIR"
VITE_API_URL="$API_URL" npm run build
echo "✅ 前端构建完成 -> dist/"

echo ""
echo "== [4/4] 部署前端到 Cloudflare Pages =="
# 显式 pages deploy，不受 pages.toml 误判影响
"$WRANGLER" pages deploy dist --project-name "$PAGES_PROJECT"
echo "✅ Pages 部署完成"

echo ""
echo "============================================================"
echo "🎉 全部部署完成！"
echo "   Web 管理后台 : https://pet.classe.ccwu.cc  (或 Pages 默认域名)"
echo "   API / WS     : $API_URL"
echo "   设备端        : server_url 保持 $API_URL，重启设备后即生效"
echo "------------------------------------------------------------"
echo " 若设备点「功能菜单」仍拉不到数据，确认:"
echo "   1) Worker 已部署且 D1 迁移成功 (本脚本第 1 步)"
echo "   2) 设备已烧录含功能屏的固件 (firmware/ClassPetDevice)"
echo "============================================================"
