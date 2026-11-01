const $ = id => document.getElementById(id);

/* ---------------- 主题 ---------------- */

const THEME_KEY = "filesafe-theme";

function currentTheme() {
    return document.documentElement.getAttribute("data-theme") === "light" ? "light" : "dark";
}

function updateThemeButton(theme) {
    const btn = $("themeBtn");
    if (btn) btn.title = theme === "light" ? "切换到深色主题" : "切换到浅色主题";
}

function applyTheme(theme) {
    document.documentElement.setAttribute("data-theme", theme);
    try {
        localStorage.setItem(THEME_KEY, theme);
    } catch (e) { }
    updateThemeButton(theme);
}

function toggleTheme() {
    applyTheme(currentTheme() === "light" ? "dark" : "light");
}

updateThemeButton(currentTheme());

const TEXT_EXT = ["txt", "md", "markdown", "log", "json", "js", "mjs", "cjs", "ts", "css", "scss",
    "html", "htm", "xml", "yml", "yaml", "ini", "conf", "cfg", "csv", "tsv", "lrc", "c", "h", "hpp",
    "cpp", "cc", "py", "java", "go", "dart", "rs", "sh", "bat", "ps1", "sql", "toml", "gitignore"];
const IMAGE_EXT = ["png", "jpg", "jpeg", "gif", "bmp", "webp", "svg", "ico", "avif"];
const AUDIO_EXT = ["mp3", "wav", "ogg", "oga", "m4a", "aac", "flac", "opus"];
const VIDEO_EXT = ["mp4", "webm", "ogv", "m4v", "mov", "mkv", "avi"];
const PDF_EXT = ["pdf"];

let token = "";
let currentDir = "";
let currentFiles = [];
let selectedPath = "";
let activeModal = null;
let collapsed = new Set();
let taskTimer = null;
let lastFinished = null;
let watchedTasks = new Set();
let toastTimer = null;

function extOf(path) {
    const name = String(path).split("/").pop() || "";
    const index = name.lastIndexOf(".");
    return index > 0 ? name.slice(index + 1).toLowerCase() : "";
}

function baseName(path) {
    const parts = String(path).split(/[\\/]/).filter(Boolean);
    return parts.length ? parts[parts.length - 1] : path;
}

function kindOf(path) {
    const ext = extOf(path);
    if (IMAGE_EXT.includes(ext)) return "image";
    if (AUDIO_EXT.includes(ext)) return "audio";
    if (VIDEO_EXT.includes(ext)) return "video";
    if (PDF_EXT.includes(ext)) return "pdf";
    if (TEXT_EXT.includes(ext)) return "text";
    return "other";
}

const SVG_HEAD = '<svg viewBox="0 0 16 16" fill="none" stroke="currentColor" ' +
    'stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round">';

const ICONS = {
    folder: SVG_HEAD +
        '<path d="M2 12.4V4.3a.8.8 0 0 1 .8-.8h3l1.5 1.7h6a.8.8 0 0 1 .8.8v6.4a.8.8 0 0 1-.8.8H2.8a.8.8 0 0 1-.8-.8Z"/>' +
        '</svg>',
    text: SVG_HEAD +
        '<path d="M3.6 2.3h5.1l3.7 3.7v7.7a.8.8 0 0 1-.8.8H3.6a.8.8 0 0 1-.8-.8V3.1a.8.8 0 0 1 .8-.8Z"/>' +
        '<path d="M8.5 2.4v3.6h3.6"/>' +
        '<path d="M5.6 9.1h4.8M5.6 11.4h4.8"/>' +
        '</svg>',
    image: SVG_HEAD +
        '<rect x="2.1" y="3.1" width="11.8" height="9.8" rx="1.1"/>' +
        '<circle cx="5.7" cy="6.5" r="1.1"/>' +
        '<path d="M2.5 11.5 5.9 8.7l2.3 2 2.1-1.7 3.2 2.5"/>' +
        '</svg>',
    audio: SVG_HEAD +
        '<path d="M6.5 11.3V4.7l6-1.4v6.4"/>' +
        '<ellipse cx="5" cy="11.6" rx="1.5" ry="1.2"/>' +
        '<ellipse cx="11" cy="10.2" rx="1.5" ry="1.2"/>' +
        '</svg>',
    video: SVG_HEAD +
        '<rect x="2.1" y="3.5" width="11.8" height="9" rx="1.1"/>' +
        '<path d="M6.6 6.4 10.1 8 6.6 9.6Z"/>' +
        '</svg>',
    pdf: SVG_HEAD +
        '<path d="M3.6 2.3h5.1l3.7 3.7v7.7a.8.8 0 0 1-.8.8H3.6a.8.8 0 0 1-.8-.8V3.1a.8.8 0 0 1 .8-.8Z"/>' +
        '<path d="M8.5 2.4v3.6h3.6"/>' +
        '<path d="M5.8 11.3V6.9h1.2a1.1 1.1 0 0 1 0 2.2H5.8"/>' +
        '</svg>',
    other: SVG_HEAD +
        '<circle cx="8" cy="8" r="5.8"/>' +
        '<path d="M6.2 6.3a1.9 1.9 0 1 1 2.4 2.2c-.4.1-.6.5-.6 1"/>' +
        '<circle cx="8" cy="11.4" r=".7" fill="currentColor" stroke="none"/>' +
        '</svg>',
    chevronDown: SVG_HEAD + '<path d="M3.7 6.1 8 10.4l4.3-4.3"/></svg>',
    chevronRight: SVG_HEAD + '<path d="M6.1 3.7 10.4 8l-4.3 4.3"/></svg>'
};

function createIcon(name) {
    const span = document.createElement("span");
    span.className = "icon icon-" + name;
    span.innerHTML = ICONS[name] || ICONS.other;
    return span;
}

function formatSize(bytes) {
    if (!bytes) return "0 B";
    if (bytes < 1024) return bytes + " B";
    const units = ["KB", "MB", "GB", "TB"];
    let value = bytes;
    let index = -1;
    do {
        value /= 1024;
        index++;
    } while (value >= 1024 && index < units.length - 1);
    return value.toFixed(1) + " " + units[index];
}

function showMsg(el, text, type) {
    el.textContent = text;
    el.className = "msg show " + (type || "info");
}

function hideMsg(el) {
    el.className = "msg";
    el.textContent = "";
}

function showToast(text) {
    const el = $("toast");
    el.textContent = text;
    el.classList.remove("hidden");
    if (toastTimer) clearTimeout(toastTimer);
    toastTimer = setTimeout(() => el.classList.add("hidden"), 2600);
}

async function callApi(path, options) {
    try {
        const response = await fetch(path, options);
        const text = await response.text();
        try {
            return JSON.parse(text);
        } catch (e) {
            return { ok: false, error: text || ("HTTP " + response.status) };
        }
    } catch (e) {
        return { ok: false, error: "网络请求失败: " + e.message };
    }
}

function postJson(path, data) {
    return callApi(path, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify(data)
    });
}

function fileUrl(id, download) {
    return "/api/file?token=" + encodeURIComponent(token) +
        "&id=" + id + (download ? "&download=1" : "");
}

/* ---------------- 弹窗 ---------------- */

function openModal(options) {
    activeModal = options;
    $("modalTitle").textContent = options.title;

    const body = $("modalBody");
    body.innerHTML = "";

    (options.fields || []).forEach(field => {
        const wrap = document.createElement("div");
        wrap.className = "field";

        const label = document.createElement("label");
        label.textContent = field.label;
        label.setAttribute("for", "mf-" + field.id);

        const input = document.createElement("input");
        input.id = "mf-" + field.id;
        input.type = field.type || "text";
        input.value = field.value || "";
        if (field.placeholder) input.placeholder = field.placeholder;
        if (field.readonly) input.readOnly = true;

        wrap.appendChild(label);
        wrap.appendChild(input);
        body.appendChild(wrap);
    });

    hideMsg($("modalMsg"));
    $("modalSubmit").textContent = options.submitText || "提交";
    $("modalSubmit").disabled = false;
    $("modalMask").classList.remove("hidden");

    const first = body.querySelector("input:not([readonly])");
    if (first) first.focus();
}

function closeModal() {
    $("modalMask").classList.add("hidden");
    activeModal = null;
    hideMsg($("modalMsg"));
}

function modalField(id) {
    const input = $("mf-" + id);
    return input ? input.value.trim() : "";
}

$("modalClose").addEventListener("click", closeModal);
$("modalCancel").addEventListener("click", closeModal);
$("modalMask").addEventListener("click", event => {
    if (event.target === $("modalMask")) closeModal();
});

$("modalBody").addEventListener("keydown", event => {
    if (event.key !== "Enter" || event.isComposing) return;
    event.preventDefault();
    if (!$("modalSubmit").disabled) $("modalSubmit").click();
});

$("modalSubmit").addEventListener("click", async () => {
    if (!activeModal || !activeModal.onSubmit) return;
    const btn = $("modalSubmit");
    const msg = $("modalMsg");
    btn.disabled = true;
    hideMsg(msg);
    try {
        const error = await activeModal.onSubmit();
        if (error) showMsg(msg, error, "err");
        else closeModal();
    } catch (e) {
        showMsg(msg, e.message || String(e), "err");
    } finally {
        btn.disabled = false;
    }
});

/* ---------------- 目录树 ---------------- */

function buildTree(files) {
    const root = { name: "", path: "", type: "dir", children: new Map() };

    files.forEach(file => {
        const parts = String(file.path).split("/").filter(Boolean);
        if (!parts.length) return;

        let node = root;
        parts.forEach((part, index) => {
            const isLeaf = index === parts.length - 1;
            if (!node.children.has(part)) {
                node.children.set(part, {
                    name: part,
                    path: node.path ? node.path + "/" + part : part,
                    type: isLeaf ? "file" : "dir",
                    children: new Map(),
                    file: null,
                    size: 0
                });
            }
            node = node.children.get(part);
            if (isLeaf) {
                node.type = "file";
                node.file = file;
                node.size = file.size || 0;
            }
        });
    });

    return root;
}

function sortedChildren(node) {
    return [...node.children.values()].sort((a, b) => {
        if (a.type !== b.type) return a.type === "dir" ? -1 : 1;
        return a.name.localeCompare(b.name, "zh-CN");
    });
}

function dirPaths(files) {
    const set = new Set();
    (function walk(node) {
        node.children.forEach(child => {
            if (child.type === "dir") {
                set.add(child.path);
                walk(child);
            }
        });
    })(buildTree(files));
    return set;
}

function buildRow(node, depth) {
    const row = document.createElement("div");
    row.className = "tree-row" + (node.type === "dir" ? " dir" : "");
    row.dataset.path = node.path;
    row.style.paddingLeft = (8 + (depth || 0) * 12) + "px";

    const caret = document.createElement("span");
    caret.className = "caret";
    if (node.type === "dir") {
        caret.innerHTML = collapsed.has(node.path) ? ICONS.chevronRight : ICONS.chevronDown;
    } else {
        caret.classList.add("blank");
    }
    row.appendChild(caret);

    row.appendChild(node.type === "dir"
        ? createIcon("folder")
        : createIcon(kindOf(node.path)));

    const name = document.createElement("span");
    name.className = "name";
    name.textContent = node.name;
    name.title = node.path;
    row.appendChild(name);

    if (node.type === "file") {
        const size = document.createElement("span");
        size.className = "size";
        size.textContent = formatSize(node.size);
        row.appendChild(size);
    }

    const ops = document.createElement("span");
    ops.className = "ops";

    if (node.type === "dir") {
        const addBtn = document.createElement("button");
        addBtn.className = "icon-btn";
        addBtn.textContent = "+";
        addBtn.title = "添加文件或目录到此目录";
        addBtn.addEventListener("click", event => {
            event.stopPropagation();
            openAddPathModal(node.path);
        });
        ops.appendChild(addBtn);

        const exportBtn = document.createElement("button");
        exportBtn.className = "icon-btn";
        exportBtn.textContent = "导出";
        exportBtn.title = "导出该目录到 output";
        exportBtn.addEventListener("click", event => {
            event.stopPropagation();
            exportPath(node);
        });
        ops.appendChild(exportBtn);
    }

    const delBtn = document.createElement("button");
    delBtn.className = "icon-btn danger";
    delBtn.textContent = "删除";
    delBtn.title = node.type === "dir" ? "删除该目录及其中所有文件" : "删除该文件";
    delBtn.addEventListener("click", event => {
        event.stopPropagation();
        deletePath(node);
    });
    if (node.path) ops.appendChild(delBtn);

    row.appendChild(ops);

    row.addEventListener("click", () => {
        if (node.type === "dir") {
            if (collapsed.has(node.path)) collapsed.delete(node.path);
            else collapsed.add(node.path);
            renderTree();
            return;
        }
        selectedPath = node.path;
        markActive();
        preview(node.file);
    });

    return row;
}

function appendNode(list, node, depth) {
    const li = document.createElement("li");
    li.className = "tree-item";
    li.appendChild(buildRow(node, depth));
    list.appendChild(li);

    if (node.type === "dir" && !collapsed.has(node.path)) {
        sortedChildren(node).forEach(child => appendNode(list, child, depth + 1));
    }
}

function renderTree() {
    const treeEl = $("tree");
    treeEl.innerHTML = "";

    if (!token) {
        const empty = document.createElement("div");
        empty.className = "empty";
        empty.textContent = "请选择目录并点击「打开」";
        treeEl.appendChild(empty);
        return;
    }

    const root = buildTree(currentFiles);
    root.name = currentDir;

    const ul = document.createElement("ul");
    ul.className = "tree-node";
    appendNode(ul, root, 0);
    treeEl.appendChild(ul);

    if (!root.children.size) {
        const hint = document.createElement("div");
        hint.className = "empty";
        hint.textContent = "该目录为空，点击根目录的「+」添加文件或目录";
        ul.appendChild(hint);
    }

    markActive();
}

function markActive() {
    document.querySelectorAll(".tree-row.active").forEach(el => el.classList.remove("active"));
    if (!selectedPath) return;
    const row = [...document.querySelectorAll(".tree-row")].find(el => el.dataset.path === selectedPath);
    if (row) row.classList.add("active");
}

/* ---------------- 内容预览 ---------------- */

function showViewerMessage(text) {
    const viewer = $("viewer");
    viewer.innerHTML = "";
    const empty = document.createElement("div");
    empty.className = "empty";
    empty.textContent = text;
    viewer.appendChild(empty);
}

function viewerHeader(file) {
    const head = document.createElement("div");
    head.className = "viewer-head";

    const info = document.createElement("div");
    const title = document.createElement("div");
    title.className = "viewer-title";
    title.textContent = baseName(file.path);

    const meta = document.createElement("div");
    meta.className = "viewer-meta";
    meta.textContent = file.path + " · " + formatSize(file.size);

    info.appendChild(title);
    info.appendChild(meta);

    const download = document.createElement("button");
    download.className = "ghost tiny";
    download.textContent = "下载";
    download.addEventListener("click", () => {
        window.location.href = fileUrl(file.id, true);
    });

    head.appendChild(info);
    head.appendChild(download);
    return head;
}

async function preview(file) {
    const viewer = $("viewer");
    viewer.innerHTML = "";
    viewer.appendChild(viewerHeader(file));

    const stage = document.createElement("div");
    stage.className = "viewer-stage";
    viewer.appendChild(stage);

    const kind = kindOf(file.path);

    if (kind === "image") {
        const img = document.createElement("img");
        img.src = fileUrl(file.id, false);
        img.alt = file.path;
        stage.appendChild(img);
        return;
    }

    if (kind === "audio") {
        const audio = document.createElement("audio");
        audio.controls = true;
        audio.preload = "metadata";
        audio.volume = 0.5;
        audio.src = fileUrl(file.id, false);
        stage.appendChild(audio);
        return;
    }

    if (kind === "video") {
        const video = document.createElement("video");
        video.controls = true;
        video.preload = "metadata";
        video.volume = 0.5;
        video.src = fileUrl(file.id, false);
        stage.appendChild(video);
        return;
    }

    if (kind === "pdf") {
        const panel = document.createElement("div");
        panel.className = "empty stack";

        const note = document.createElement("div");
        note.textContent = "PDF 文件将由浏览器自带的阅读器显示";

        const open = document.createElement("button");
        open.className = "ghost";
        open.textContent = "在浏览器中打开 PDF";
        open.addEventListener("click", () => {
            window.open(fileUrl(file.id, false), "_blank");
        });

        panel.appendChild(note);
        panel.appendChild(open);
        stage.appendChild(panel);
        return;
    }

    if (kind === "text") {
        if (file.size > 4 * 1024 * 1024) {
            const note = document.createElement("div");
            note.className = "empty";
            note.textContent = "文件较大（" + formatSize(file.size) + "），请下载后查看";
            stage.appendChild(note);
            return;
        }
        const pre = document.createElement("pre");
        pre.className = "text-view";
        pre.textContent = "加载中…";
        stage.style.alignItems = "stretch";
        stage.style.justifyContent = "stretch";
        stage.appendChild(pre);

        try {
            const response = await fetch(fileUrl(file.id, false));
            if (!response.ok) {
                pre.textContent = "读取失败: HTTP " + response.status;
                return;
            }
            pre.textContent = await response.text();
        } catch (e) {
            pre.textContent = "读取失败: " + e.message;
        }
        return;
    }

    const note = document.createElement("div");
    note.className = "empty";
    note.textContent = "该文件类型不支持在线预览，请下载后查看";
    stage.appendChild(note);
}

/* ---------------- 加密任务 ---------------- */

function stateLabel(task) {
    if (task.state === "queued") return "排队中";
    if (task.state === "failed") return "失败";
    if (task.state === "running") {
        return { encrypt: "加密中", add: "添加中", remove: "删除中", export: "导出中" }[task.kind] || "处理中";
    }
    return task.state;
}

function computePercent(task) {
    if (task.bytesTotal > 0) {
        return Math.min(99, Math.floor(task.bytesDone * 100 / task.bytesTotal));
    }
    if (task.filesTotal > 0) {
        return Math.min(99, Math.floor(task.filesDone * 100 / task.filesTotal));
    }
    return 0;
}

function renderTasks(tasks) {
    const list = $("taskList");
    list.innerHTML = "";

    if (!tasks.length) {
        const empty = document.createElement("div");
        empty.className = "empty";
        empty.textContent = "暂无进行中的任务";
        list.appendChild(empty);
        return;
    }

    tasks.forEach(task => {
        const item = document.createElement("div");
        item.className = "task-item";

        const dir = document.createElement("div");
        dir.className = "task-dir";
        dir.textContent = task.title || baseName(task.dir);
        dir.title = task.dir;

        const state = document.createElement("div");
        const tag = document.createElement("span");
        tag.className = "status " + task.state;
        tag.textContent = stateLabel(task);
        state.appendChild(tag);

        const progress = document.createElement("div");
        const percent = computePercent(task);
        const barWrap = document.createElement("div");
        barWrap.className = "progress";
        const bar = document.createElement("span");
        bar.style.width = percent + "%";
        barWrap.appendChild(bar);

        const text = document.createElement("div");
        text.className = "progress-text";
        let info = "";
        if (task.state === "queued") {
            info = "排队等待中…";
        } else {
            info = percent + "% · " + task.filesDone + "/" + task.filesTotal + " 文件";
            if (task.bytesTotal > 0) {
                info += " · " + formatSize(task.bytesDone) + " / " + formatSize(task.bytesTotal);
            }
        }
        text.textContent = info;

        progress.appendChild(barWrap);
        progress.appendChild(text);

        const current = document.createElement("div");
        current.className = "task-file";
        const currentText = task.state === "failed"
            ? (task.error || "任务失败")
            : (task.current || (task.state === "queued" ? "等待执行…" : "准备中…"));
        current.textContent = currentText;
        current.title = currentText;

        const time = document.createElement("div");
        time.className = "task-time";
        time.textContent = task.elapsed.toFixed(1) + " s";

        item.appendChild(dir);
        item.appendChild(state);
        item.appendChild(progress);
        item.appendChild(current);
        item.appendChild(time);
        list.appendChild(item);
    });
}

function startPolling() {
    if (!taskTimer) taskTimer = setInterval(refreshTasks, 1200);
}

function stopPolling() {
    if (taskTimer) {
        clearInterval(taskTimer);
        taskTimer = null;
    }
}

async function refreshTasks() {
    const data = await callApi("/api/tasks");
    if (!data.ok) return;

    const tasks = data.tasks || [];
    renderTasks(tasks);

    const byId = new Map(tasks.map(task => [task.id, task]));
    let changed = false;
    [...watchedTasks].forEach(id => {
        const task = byId.get(id);
        if (!task || task.state === "failed") {
            watchedTasks.delete(id);
            changed = true;
        }
    });

    if (changed) refreshSessionFiles();

    if (lastFinished !== null && data.finished !== lastFinished) {
        loadStores();
    }
    lastFinished = data.finished;

    const active = tasks.some(task => task.state === "queued" || task.state === "running");
    if (active || isEncryptOpen() || isTaskOpen()) startPolling();
    else stopPolling();
}

async function refreshSessionFiles() {
    if (!token) return;

    const activeToken = token;
    const data = await callApi("/api/files?token=" + encodeURIComponent(token));
    if (activeToken !== token) return;
    if (!data.ok) {
        if (isSessionExpired(data)) resetSession();
        return;
    }

    const before = dirPaths(currentFiles);
    currentFiles = data.files || [];
    const after = dirPaths(currentFiles);

    after.forEach(path => {
        if (!before.has(path)) collapsed.add(path);
    });
    [...collapsed].forEach(path => {
        if (!after.has(path)) collapsed.delete(path);
    });

    if (selectedPath) {
        const stillExists = after.has(selectedPath) ||
            currentFiles.some(file => file.path === selectedPath ||
                file.path.startsWith(selectedPath + "/"));
        if (!stillExists) {
            selectedPath = "";
            showViewerMessage("左侧选择文件后在此预览");
        }
    }

    updateSessionInfo();
    renderTree();
}

/* ---------------- 目录列表 ---------------- */

async function loadStores() {
    const select = $("storeSel");
    const previous = select.value;
    select.innerHTML = "";

    const data = await callApi("/api/stores");
    if (!data.ok) {
        select.innerHTML = '<option value="">加载失败</option>';
        return;
    }
    if (!data.stores || !data.stores.length) {
        select.innerHTML = '<option value="">暂无加密目录</option>';
        return;
    }

    data.stores.forEach(name => {
        const option = document.createElement("option");
        option.value = name;
        option.textContent = name;
        select.appendChild(option);
    });

    if (previous && data.stores.includes(previous)) {
        select.value = previous;
    }
}

/* ---------------- 操作 ---------------- */

function openViewModal() {
    const dir = $("storeSel").value;
    if (!dir) {
        showModalOnlyMessage("请先在目录下拉框中选择加密目录");
        return;
    }

    openModal({
        title: "打开目录",
        submitText: "打开",
        fields: [
            { id: "dir", label: "目录", value: dir, readonly: true },
            { id: "password", label: "密码", type: "password", placeholder: "打开该目录的密码" }
        ],
        onSubmit: async () => {
            const password = modalField("password");
            if (!password) return "请输入密码";

            const data = await postJson("/api/open", { dir, password });
            if (!data.ok) return data.error || "打开失败";

            token = data.token;
            currentDir = data.dir;
            currentFiles = data.files || [];
            selectedPath = "";
            collapsed = dirPaths(currentFiles);

            updateSessionInfo();
            $("sidebarHead").classList.remove("hidden");

            renderTree();
            showViewerMessage("左侧选择文件后在此预览");
            return "";
        }
    });
}

function isEncryptOpen() {
    return !$("encryptMask").classList.contains("hidden");
}

function openEncryptModal() {
    $("encryptMask").classList.remove("hidden");
    hideMsg($("encMsg"));
    $("encSubmit").disabled = false;
    $("enc-dir").focus();
}

function closeEncryptModal() {
    $("encryptMask").classList.add("hidden");
    hideMsg($("encMsg"));
}

async function submitEncrypt() {
    const dir = $("enc-dir").value.trim();
    const password = $("enc-password").value.trim();
    const msg = $("encMsg");
    const btn = $("encSubmit");

    if (!dir) {
        showMsg(msg, "请输入待加密目录的绝对路径", "err");
        return;
    }
    if (!password) {
        showMsg(msg, "请输入加密密码", "err");
        return;
    }

    btn.disabled = true;
    hideMsg(msg);
    try {
        const data = await postJson("/api/encrypt", { dir, password });
        if (!data.ok) {
            showMsg(msg, data.error || "提交失败", "err");
            return;
        }
        $("enc-dir").value = "";
        $("enc-password").value = "";
        closeEncryptModal();
        showToast("已提交加密任务，可在「后台任务」查看进度");
        startPolling();
    } catch (e) {
        showMsg(msg, e.message || String(e), "err");
    } finally {
        btn.disabled = false;
    }
}

$("encryptClose").addEventListener("click", closeEncryptModal);
$("encryptMask").addEventListener("click", event => {
    if (event.target === $("encryptMask")) closeEncryptModal();
});
$("encSubmit").addEventListener("click", submitEncrypt);
["enc-dir", "enc-password"].forEach(id => {
    $(id).addEventListener("keydown", event => {
        if (event.key === "Enter") submitEncrypt();
    });
});

function isTaskOpen() {
    return !$("taskMask").classList.contains("hidden");
}

function openTaskModal() {
    $("taskMask").classList.remove("hidden");
    refreshTasks();
    startPolling();
}

function closeTaskModal() {
    $("taskMask").classList.add("hidden");
    refreshTasks();
}

$("tasksBtn").addEventListener("click", openTaskModal);
$("taskClose").addEventListener("click", closeTaskModal);
$("taskRefresh").addEventListener("click", refreshTasks);
$("taskMask").addEventListener("click", event => {
    if (event.target === $("taskMask")) closeTaskModal();
});

function openAddPathModal(targetDir) {
    if (!token) {
        showModalOnlyMessage("请先打开一个加密目录");
        return;
    }

    openModal({
        title: "添加到 " + (targetDir || "根目录"),
        submitText: "添加",
        fields: [
            { id: "target", label: "目标目录", value: targetDir || "根目录", readonly: true },
            { id: "path", label: "路径", placeholder: "要添加的本地文件或目录绝对路径" }
        ],
        onSubmit: async () => {
            const path = modalField("path");
            if (!path) return "请输入路径";

            const data = await postJson("/api/add", { token, target: targetDir, path });
            if (!data.ok) {
                if (isSessionExpired(data)) resetSession();
                return data.error || "添加失败";
            }

            if (data.id) watchedTasks.add(data.id);
            showToast("已加入任务队列：" + (baseName(path) || path));
            startPolling();
            await refreshTasks();
            return "";
        }
    });
}

async function exportPath(node) {
    if (!token) {
        showModalOnlyMessage("请先打开一个加密目录");
        return;
    }

    const data = await postJson("/api/export", { token, path: node.path });
    if (!data.ok) {
        if (isSessionExpired(data)) resetSession();
        window.alert(data.error || "导出失败");
        return;
    }

    if (data.id) watchedTasks.add(data.id);
    showToast("已加入任务队列：导出 " + (node.name || currentDir));
    startPolling();
    await refreshTasks();
}

async function deletePath(node) {
    if (!token) {
        showModalOnlyMessage("请先打开一个加密目录");
        return;
    }

    const tip = node.type === "dir"
        ? "确定删除目录「" + node.name + "」及其中的全部文件？"
        : "确定删除文件「" + node.name + "」？";
    if (!window.confirm(tip)) return;

    const data = await postJson("/api/delete", { token, path: node.path });
    if (!data.ok) {
        if (isSessionExpired(data)) resetSession();
        window.alert(data.error || "删除失败");
        return;
    }

    if (data.id) watchedTasks.add(data.id);
    showToast("已加入任务队列：删除 " + node.name);
    startPolling();
    await refreshTasks();
}

function updateSessionInfo() {
    $("sessionInfo").innerHTML = "已打开：<strong>" + escapeHtml(currentDir) +
        "</strong> · " + currentFiles.length + " 个文件";
}

function isSessionExpired(data) {
    return !data.ok && /会话/.test(data.error || "");
}

function resetSession() {
    token = "";
    currentDir = "";
    currentFiles = [];
    selectedPath = "";
    watchedTasks.clear();
    $("sidebarHead").classList.add("hidden");
    renderTree();
    showViewerMessage("会话已失效，请重新输入密码打开目录");
}

function showModalOnlyMessage(text) {
    openModal({
        title: "提示",
        submitText: "知道了",
        fields: [],
        onSubmit: async () => ""
    });
    showMsg($("modalMsg"), text, "err");
}

function escapeHtml(text) {
    return String(text).replace(/[&<>"']/g, ch => ({
        "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;"
    }[ch]));
}

$("viewBtn").addEventListener("click", openViewModal);
$("encryptBtn").addEventListener("click", openEncryptModal);
$("themeBtn").addEventListener("click", toggleTheme);

$("closeBtn").addEventListener("click", resetSession);

loadStores();
refreshTasks();