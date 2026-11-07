(function () {
    const VIDEO_EXT = ["mp4", "webm", "ogv", "m4v", "mov", "mkv", "avi"];
    const BATCH_SIZE = 12;
    const PLAY_ICON = '<svg viewBox="0 0 16 16" fill="currentColor" stroke="none">' +
        '<path d="M5.4 3.6 12 8l-6.6 4.4Z"/></svg>';

    const params = new URLSearchParams(window.location.search);
    const token = params.get("token") || "";
    const selfId = params.get("id") || "";

    const grid = document.getElementById("grid");
    const sentinel = document.getElementById("sentinel");
    const emptyBox = document.getElementById("empty");
    const dirText = document.getElementById("dirText");
    const countText = document.getElementById("countText");
    const refreshBtn = document.getElementById("refreshBtn");
    const player = document.getElementById("player");
    const playerVideo = document.getElementById("playerVideo");
    const playerCaption = document.getElementById("playerCaption");

    let pendingFiles = [];
    let renderedCount = 0;
    let videoObserver = null;
    let sentinelObserver = null;

    function extOf(path) {
        const name = String(path).split("/").pop() || "";
        const index = name.lastIndexOf(".");
        return index > 0 ? name.slice(index + 1).toLowerCase() : "";
    }

    function baseName(path) {
        const parts = String(path).split("/").filter(Boolean);
        return parts.length ? parts[parts.length - 1] : path;
    }

    function dirOf(path) {
        const normalized = String(path).replace(/\\/g, "/");
        const index = normalized.lastIndexOf("/");
        return index > 0 ? normalized.slice(0, index) : "";
    }

    function fileUrl(id) {
        return "/api/file?token=" + encodeURIComponent(token) + "&id=" + id;
    }

    function showEmpty(message) {
        resetList();
        countText.textContent = "";
        emptyBox.textContent = message;
        emptyBox.classList.remove("hidden");
    }

    function hideEmpty() {
        emptyBox.classList.add("hidden");
    }

    function openPlayer(file) {
        playerCaption.textContent = baseName(file.path) + "  ·  点击背景关闭";
        player.classList.remove("hidden");
        playerVideo.src = fileUrl(file.id);
        const promise = playerVideo.play();
        if (promise && typeof promise.catch === "function") promise.catch(function () { });
    }

    function closePlayer() {
        player.classList.add("hidden");
        playerVideo.pause();
        playerVideo.removeAttribute("src");
        playerVideo.load();
    }

    player.addEventListener("click", function (event) {
        if (event.target === playerVideo) return;
        closePlayer();
    });

    document.addEventListener("keydown", function (event) {
        if (event.key === "Escape") closePlayer();
    });

    function loadVideo(video) {
        if (!video.dataset.src) return;
        video.src = video.dataset.src;
        delete video.dataset.src;
    }

    function createItem(file) {
        const item = document.createElement("figure");
        item.className = "item";
        item.title = file.path;
        item.addEventListener("click", function () {
            openPlayer(file);
        });

        const video = document.createElement("video");
        video.preload = "metadata";
        video.muted = true;
        video.playsInline = true;
        video.dataset.src = fileUrl(file.id);
        video.addEventListener("error", function () {
            item.classList.add("broken");
        });

        const play = document.createElement("span");
        play.className = "play";
        play.innerHTML = PLAY_ICON;

        const name = document.createElement("div");
        name.className = "name";
        name.textContent = baseName(file.path);

        item.appendChild(video);
        item.appendChild(play);
        item.appendChild(name);

        if (videoObserver) {
            videoObserver.observe(video);
        } else {
            loadVideo(video);
        }

        return item;
    }

    function render(videos) {
        hideEmpty();
        countText.textContent = videos.length + " 个";
        resetList();

        pendingFiles = videos;
        renderedCount = 0;

        if (!("IntersectionObserver" in window)) {
            videos.forEach(function (file) {
                grid.appendChild(createItem(file));
            });
            return;
        }

        videoObserver = new IntersectionObserver(function (entries, observer) {
            entries.forEach(function (entry) {
                if (!entry.isIntersecting) return;
                const video = entry.target;
                observer.unobserve(video);
                loadVideo(video);
            });
        }, { rootMargin: "400px 0px" });

        sentinelObserver = new IntersectionObserver(function (entries) {
            entries.forEach(function (entry) {
                if (entry.isIntersecting) renderBatch();
            });
        }, { rootMargin: "120px 0px" });

        sentinelObserver.observe(sentinel);
        renderBatch();
    }

    function renderBatch() {
        if (renderedCount >= pendingFiles.length) {
            sentinel.classList.add("hidden");
            return;
        }

        const next = pendingFiles.slice(renderedCount, renderedCount + BATCH_SIZE);
        renderedCount += next.length;

        const fragment = document.createDocumentFragment();
        next.forEach(function (file) {
            fragment.appendChild(createItem(file));
        });
        grid.appendChild(fragment);

        if (renderedCount >= pendingFiles.length) {
            sentinel.classList.add("hidden");
        } else {
            sentinel.classList.remove("hidden");
        }
    }

    function resetList() {
        if (videoObserver) {
            videoObserver.disconnect();
            videoObserver = null;
        }
        if (sentinelObserver) {
            sentinelObserver.disconnect();
            sentinelObserver = null;
        }
        pendingFiles = [];
        renderedCount = 0;
        grid.innerHTML = "";
        sentinel.classList.add("hidden");
    }

    let loading = false;

    async function load() {
        if (loading) return;
        loading = true;
        refreshBtn.classList.add("spinning");
        try {
            await loadData();
        } finally {
            loading = false;
            refreshBtn.classList.remove("spinning");
        }
    }

    async function loadData() {
        if (!token) {
            dirText.textContent = "—";
            showEmpty("缺少会话令牌，请从主页面打开该文件");
            return;
        }

        dirText.textContent = "加载中…";

        let files = [];
        try {
            const response = await fetch("/api/files?token=" + encodeURIComponent(token));
            if (!response.ok) {
                dirText.textContent = "—";
                showEmpty("读取文件列表失败: HTTP " + response.status);
                return;
            }
            const data = await response.json();
            files = data.files || [];
        } catch (e) {
            dirText.textContent = "—";
            showEmpty("读取文件列表失败: " + e.message);
            return;
        }

        const current = selfId
            ? files.find(function (file) { return String(file.id) === String(selfId); })
            : null;

        if (!current) {
            dirText.textContent = "—";
            showEmpty("未找到当前文件，无法确定视频目录");
            return;
        }

        const baseDir = dirOf(current.path);

        dirText.textContent = baseDir || "/";

        const videos = files
            .filter(function (file) {
                const path = String(file.path).replace(/\\/g, "/");
                return dirOf(path) === baseDir && VIDEO_EXT.includes(extOf(path));
            })
            .sort(function (a, b) {
                return String(a.path).localeCompare(String(b.path));
            });

        if (!videos.length) {
            showEmpty("目录 " + (baseDir || "/") + " 中没有视频");
            return;
        }

        render(videos);
    }

    refreshBtn.addEventListener("click", load);

    load();

})();