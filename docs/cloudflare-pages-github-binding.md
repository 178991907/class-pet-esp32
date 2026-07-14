# Cloudflare Pages 手动绑定 GitHub 自动部署操作文档

> 目标：每次 `git push` 到 GitHub 仓库 `178991907/class-pet-esp32` 的 `main` 分支，Cloudflare Pages 自动重新部署前端，且保留现有自定义域 `pet.classe.ccwu.cc`。

---

## 一、前置条件

- 你已拥有 Cloudflare 账号，且 `pet.classe.ccwu.cc` 自定义域已绑定到现有 Pages 项目。
- GitHub 仓库 `178991907/class-pet-esp32` 已存在，`main` 分支为生产分支。
- 你将用 Cloudflare 控制台完成（无需任何 API Token，走 GitHub OAuth 授权）。

> 已知前端根目录有 `vite.config.ts` / `package.json`，生产构建命令为 `npm run build`，输出目录为 `dist`。
> 前端需要用到的环境变量：`VITE_API_URL = https://api.classe.ccwu.cc`

---

## 二、控制台操作步骤

### 步骤 1：打开 Pages 项目
1. 登录 Cloudflare 控制台 → 左侧菜单 **Workers & Pages**。
2. 在列表中找到你现有的 Pages 项目（线上 slug 为 `class-pet-garden-6fq`，即 `*.pages.dev` 的前缀）。
   - 若想用全新项目：点 **Create** → **Pages** → **Connect to Git**。
3. 进入该项目后，顶部进入 **Settings** 标签。

### 步骤 2：连接 GitHub 仓库
1. 在 **Settings → Builds & deployments**（或 "Source"）区域，点 **Connect GitHub**（首次会弹出 GitHub 授权页，按提示授权 Cloudflare 访问该仓库）。
2. 授权完成后，选择仓库 **`178991907/class-pet-esp32`**，生产分支选 **`main`**。
3. 若提示「Production branch」，确认是 `main`。

### 步骤 3：填写构建配置
在构建配置中填写（关键，填错会部署失败或页面空白）：

| 项目 | 值 |
|------|-----|
| Framework preset | 选 **Vite**（或选 None 后手动填下方两项） |
| Build command | `npm run build` |
| Build output directory | `dist` |
| Root directory | `/`（前端在仓库根，vite.config.ts 在根） |
| Node.js version | **20** 或 **22**（在设置里选，避免用旧版默认 18） |

### 步骤 4：配置环境变量
在 **Settings → Environment variables**（或 Builds 的环境变量）里，**Production** 环境添加：

| 变量名 | 值 |
|--------|-----|
| `VITE_API_URL` | `https://api.classe.ccwu.cc` |

> 说明：`VITE_API_URL` 是前端连接后端 API 的地址。若不配置，前端在部署态（非 localhost）会请求 `/pet-garden/api` 而 404，导致页面"连不上、操作不了"。

### 步骤 5：保存并触发首次部署
1. 点 **Save** / **Deploy**（不同界面文案略异）。
2. Cloudflare 会立即拉取 `main` 执行首次构建。等待构建完成（约 1–3 分钟），状态变 **Success**。

---

## 三、验证自动部署

1. 在本地改一行前端代码（例如 `src/App.vue` 加个注释），提交并推送：
   ```bash
   git add -A && git commit -m "test: 验证自动部署" && git push origin main
   ```
2. 回到 Cloudflare 控制台 → 该项目 **Deployments** 标签，应出现一条新的部署记录，状态从 `Building` → `Success`。
3. 打开 `https://pet.classe.ccwu.cc`，确认改动生效。

> 每次 push 都会自动构建；开 PR 会自动生成预览部署（Preview），合入 main 后才是生产部署。

---

## 四、确认自定义域保留

部署完成后，访问 `https://pet.classe.ccwu.cc`：
- 若页面正常且 URL 不变，说明自定义域已保留（Git 集成不会动你的自定义域配置）。
- 若自定义域丢失或变回 `*.pages.dev`：进入 **Settings → Custom domains** 重新添加 `pet.classe.ccwu.cc` 并验证 DNS（通常只需点一次 Verify）。

---

## 五、注意事项 / 坑

1. **项目名一致性**：仓库根 `pages.toml` 里写的是 `name = "classpet-web"`，但线上 slug 是 `class-pet-garden-6fq`。控制台 Git 集成是"连接已有项目"或"新建项目"，请确认连接的就是你正在用的 `class-pet-garden-6fq`，避免新建出一个 `classpet-web` 项目（那样自定义域不会带过去）。
2. **构建命令带类型检查**：`npm run build` 实际是 `vue-tsc && vite build`（全量 TS 类型检查）。前端代码若有类型错误，构建会在 Build 阶段失败。修复类型错误即可，与部署方式无关。
3. **根 package.json 混了后端依赖**：`better-sqlite3` / `pg` / `express` 等前端用不到，`npm ci` 会一并安装。`better-sqlite3` 是原生模块，Cloudflare 构建环境通常能拉预编译包；万一安装失败，可把这些后端依赖移到 `optionalDependencies` 或单独的 `package.json`，前端构建本身只依赖 vite，不受影响。
4. **Node 版本**：Cloudflare 构建默认 Node 可能较旧，务必在 Settings 里把 Node.js version 设为 20/22，与本地一致。

---

## 六、故障排查

| 现象 | 原因 / 处理 |
|------|------|
| 部署成功但页面空白 / 接口 404 | 没配 `VITE_API_URL`，前端走了 `/pet-garden/api`。回步骤 4 补环境变量后重新部署。 |
| Build 失败：`vue-tsc` 类型错误 | 前端有 TS 类型错误。修掉或临时改用 `vite build` 单独构建（权衡：失去类型检查）。 |
| Build 失败：`better-sqlite3` 编译错误 | 后端原生依赖干扰。把后端依赖移出根 package.json（见注意 3）。 |
| 自定义域变回 pages.dev | 见第四节重新绑定自定义域。 |
| push 后没触发部署 | 确认连接的是 `main` 分支、仓库/分支选对；Cloudflare 的 GitHub App 是否仍有权限（GitHub → Settings → Applications 里查看 Cloudflare 授权）。 |
