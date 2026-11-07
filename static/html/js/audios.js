(function () {
    const AUDIO_EXT = ["mp3", "wav", "ogg", "oga", "m4a", "aac", "flac", "opus"];
    const LRC_EXT = ["lrc"];
    const STORE_KEY = "filesafe-audio";
    const ICON_PLAY = '<path d="M8 5v14l11-7z"></path>';
    const ICON_PAUSE = '<path d="M6 4h4v16H6zM14 4h4v16h-4z"></path>';
    const ICON_LOOP = '<polyline points="17 1 21 5 17 9"></polyline>' +
        '<path d="M3 11V9a4 4 0 0 1 4-4h14"></path>' +
        '<polyline points="7 23 3 19 7 15"></polyline>' +
        '<path d="M21 13v2a4 4 0 0 1-4 4H3"></path>';
    const ICON_ONE = ICON_LOOP +
        '<text x="9.5" y="16" font-size="8" font-family="sans-serif" fill="currentColor" stroke="none">1</text>';
    const ICON_SHUFFLE = '<polyline points="16 3 21 3 21 8"></polyline>' +
        '<line x1="4" y1="20" x2="21" y2="3"></line>' +
        '<polyline points="21 16 21 21 16 21"></polyline>' +
        '<line x1="15" y1="15" x2="21" y2="21"></line>' +
        '<line x1="4" y1="4" x2="9" y2="9"></line>';
    const NOTE_ICON = '<svg viewBox="0 0 24 24" width="14" height="14" fill="currentColor">' +
        '<path d="M12 3v10.55A4 4 0 1 0 14 17V7h4V3h-6z"/></svg>';

    const params = new URLSearchParams(window.location.search);
    const token = params.get("token") || "";
    const selfId = params.get("id") || "";

    const dirText = document.getElementById("dirText");
    const countText = document.getElementById("countText");
    const listBox = document.getElementById("audioList");
    const searchInput = document.getElementById("audioSearch");
    const sortBtn = document.getElementById("sortBtn");
    const sortMenu = document.getElementById("sortMenu");
    const refreshBtn = document.getElementById("refreshBtn");

    const cover = document.getElementById("audioCover");
    const coverImg = document.getElementById("coverImg");
    const coverFallback = document.getElementById("coverFallback");
    const infoBox = document.getElementById("audioInfo");
    const nowTitle = document.getElementById("nowTitle");
    const nowDir = document.getElementById("nowDir");
    const btnPlay = document.getElementById("btnPlay");
    const btnPrev = document.getElementById("btnPrev");
    const btnNext = document.getElementById("btnNext");
    const btnMode = document.getElementById("btnMode");
    const iconMode = document.getElementById("iconMode");
    const iconPlay = document.getElementById("iconPlay");
    const progress = document.getElementById("audioProgress");
    const timeCur = document.getElementById("timeCur");
    const timeTotal = document.getElementById("timeTotal");
    const barLyrics = document.getElementById("barLyrics");
    const volume = document.getElementById("audioVolume");
    const volIcon = document.getElementById("volIcon");

    const fullpage = document.getElementById("fullpage");
    const fpBg = document.getElementById("fpBg");
    const fpClose = document.getElementById("fpClose");
    const fpDisc = document.getElementById("fpDisc");
    const discCover = document.getElementById("discCover");
    const discFallback = document.getElementById("discFallback");
    const fpTitle = document.getElementById("fpTitle");
    const fpDir = document.getElementById("fpDir");
    const fpSpectrum = document.getElementById("fpSpectrum");
    const fpLyrics = document.getElementById("fpLyrics");
    const fpProgress = document.getElementById("fpProgress");
    const fpTimeCur = document.getElementById("fpTimeCur");
    const fpTimeTotal = document.getElementById("fpTimeTotal");
    const fpPlay = document.getElementById("fpPlay");
    const fpIconPlay = document.getElementById("fpIconPlay");
    const fpPrev = document.getElementById("fpPrev");
    const fpNext = document.getElementById("fpNext");
    const fpVolume = document.getElementById("fpVolume");
    const fpVolIcon = document.getElementById("fpVolIcon");

    const audio = new Audio();
    audio.preload = "metadata";

    const state = {
        all: [],
        view: [],
        currentTrack: null,
        search: "",
        sortBy: "",
        sortOrder: "asc",
        mode: "loop",
        lyrics: [],
        lyricIndex: -1,
        userScrollingLyrics: false,
        fullpageOpen: false,
        pendingSeek: null,
        lrcMap: new Map()
    };

    let lastVolume = 0.8;
    let loading = false;
    let saveTimer = 0;
    let coverRequestId = 0;
    let activeCoverUrl = null;
    const coverCache = new Map();

    function applyCover(blob) {
        if (activeCoverUrl) {
            URL.revokeObjectURL(activeCoverUrl);
            activeCoverUrl = null;
        }

        if (blob) {
            activeCoverUrl = URL.createObjectURL(blob);
            coverImg.src = activeCoverUrl;
            coverImg.classList.remove("hidden");
            coverFallback.classList.add("hidden");
            discCover.src = activeCoverUrl;
            discCover.classList.remove("hidden");
            discFallback.classList.add("hidden");
            fpBg.style.backgroundImage = 'url("' + activeCoverUrl + '")';
            fpBg.classList.add("has-cover");
            return;
        }

        coverImg.removeAttribute("src");
        coverImg.classList.add("hidden");
        coverFallback.classList.remove("hidden");
        discCover.removeAttribute("src");
        discCover.classList.add("hidden");
        discFallback.classList.remove("hidden");
        fpBg.style.backgroundImage = "";
        fpBg.classList.remove("has-cover");
    }

    function loadCover(track) {
        const key = String(track.id);
        const requestId = ++coverRequestId;

        if (coverCache.has(key)) {
            applyCover(coverCache.get(key));
            return;
        }

        applyCover(null);

        if (typeof jsmediatags === "undefined") {
            coverCache.set(key, null);
            return;
        }

        try {
            jsmediatags.read(location.origin + fileUrl(track.id), {
                onSuccess: function (tag) {
                    const picture = tag && tag.tags ? tag.tags.picture : null;
                    let blob = null;
                    if (picture && picture.data && picture.data.length) {
                        let type = String(picture.format || "image/jpeg").toLowerCase();
                        if (type === "image/jpg") type = "image/jpeg";
                        blob = new Blob([new Uint8Array(picture.data)], { type: type });
                    }
                    coverCache.set(key, blob);
                    if (requestId === coverRequestId) {
                        applyCover(blob);
                    }
                },
                onError: function () {
                    coverCache.set(key, null);
                    if (requestId === coverRequestId) {
                        applyCover(null);
                    }
                }
            });
        } catch (error) {
            coverCache.set(key, null);
            applyCover(null);
        }
    }

    function extOf(path) {
        const name = String(path).replace(/\\/g, "/").split("/").pop() || "";
        const index = name.lastIndexOf(".");
        return index > 0 ? name.slice(index + 1).toLowerCase() : "";
    }

    function baseName(path) {
        const parts = String(path).replace(/\\/g, "/").split("/").filter(Boolean);
        return parts.length ? parts[parts.length - 1] : path;
    }

    function stemOf(path) {
        const name = baseName(path);
        const index = name.lastIndexOf(".");
        return index > 0 ? name.slice(0, index) : name;
    }

    function dirOf(path) {
        const normalized = String(path).replace(/\\/g, "/");
        const index = normalized.lastIndexOf("/");
        return index > 0 ? normalized.slice(0, index) : "";
    }

    function fileUrl(id) {
        return "/api/file?token=" + encodeURIComponent(token) + "&id=" + id;
    }

    function fmtSize(bytes) {
        const value = Number(bytes) || 0;
        if (value < 1024) return value + " B";
        const units = ["KB", "MB", "GB", "TB"];
        let size = value / 1024;
        let index = 0;
        while (size >= 1024 && index < units.length - 1) {
            size /= 1024;
            index++;
        }
        return size.toFixed(size >= 10 ? 0 : 1) + " " + units[index];
    }

    function fmtTime(seconds) {
        const total = Math.max(0, Math.floor(Number(seconds) || 0));
        const hour = Math.floor(total / 3600);
        const minute = Math.floor((total % 3600) / 60);
        const second = total % 60;
        const mm = hour > 0 ? String(minute).padStart(2, "0") : String(minute);
        const ss = String(second).padStart(2, "0");
        return hour > 0 ? hour + ":" + mm + ":" + ss : mm + ":" + ss;
    }

    function currentIndex() {
        if (!state.currentTrack) return -1;
        return state.view.findIndex(function (track) {
            return String(track.id) === String(state.currentTrack.id);
        });
    }

    function applyView() {
        const keyword = state.search.trim().toLowerCase();
        let list = state.all.slice();

        if (keyword) {
            list = list.filter(function (track) {
                return stemOf(track.path).toLowerCase().indexOf(keyword) >= 0;
            });
        }

        if (state.sortBy === "name") {
            list.sort(function (a, b) {
                return String(a.path).localeCompare(String(b.path));
            });
        } else if (state.sortBy === "size") {
            list.sort(function (a, b) {
                return (Number(a.size) || 0) - (Number(b.size) || 0);
            });
        }

        if (state.sortBy && state.sortOrder === "desc") {
            list.reverse();
        }

        state.view = list;
        renderList();
    }

    function renderList() {
        listBox.innerHTML = "";

        if (!state.view.length) {
            const empty = document.createElement("div");
            empty.className = "empty";
            empty.textContent = state.all.length ? "没有匹配的曲目" : "该目录下没有音频文件";
            listBox.appendChild(empty);
            countText.textContent = state.all.length + " 首";
            return;
        }

        const fragment = document.createDocumentFragment();

        state.view.forEach(function (track, index) {
            const row = document.createElement("div");
            row.className = "track";
            row.title = track.path;
            if (state.currentTrack && String(track.id) === String(state.currentTrack.id)) {
                row.classList.add("active");
            }

            const icon = document.createElement("span");
            icon.className = "track-icon";
            icon.innerHTML = NOTE_ICON;

            const title = document.createElement("span");
            title.className = "track-title";
            title.textContent = stemOf(track.path);

            const size = document.createElement("span");
            size.className = "track-size";
            size.textContent = fmtSize(track.size);

            row.appendChild(icon);
            row.appendChild(title);
            row.appendChild(size);

            row.addEventListener("click", function () {
                if (state.currentTrack && String(track.id) === String(state.currentTrack.id)) {
                    togglePlay();
                } else {
                    playTrack(index);
                }
            });

            fragment.appendChild(row);
        });

        listBox.appendChild(fragment);
        countText.textContent = state.view.length + " 首";
    }

    function updateNowPlaying(track) {
        const title = stemOf(track.path);
        const dir = dirOf(track.path) || "/";
        nowTitle.textContent = title;
        nowDir.textContent = dir;
        fpTitle.textContent = title;
        fpDir.textContent = dir;
        document.title = title + " - 音频库";
    }

    function playTrack(index, seekTime, autoplay) {
        const track = state.view[index];
        if (!track) return;

        state.currentTrack = track;
        state.pendingSeek = typeof seekTime === "number" ? seekTime : null;
        state.lyrics = [];
        state.lyricIndex = -1;
        barLyrics.textContent = "—";
        renderLyrics();

        audio.src = fileUrl(track.id);
        updateNowPlaying(track);
        renderList();
        loadLyrics(track);
        loadCover(track);

        if (autoplay !== false) {
            const promise = audio.play();
            if (promise && typeof promise.catch === "function") promise.catch(function () { });
        }

        saveState();
    }

    function togglePlay() {
        if (!state.currentTrack) {
            if (state.view.length) playTrack(0);
            return;
        }
        if (audio.paused) {
            const promise = audio.play();
            if (promise && typeof promise.catch === "function") promise.catch(function () { });
        } else {
            audio.pause();
        }
    }

    function playPrev() {
        if (!state.view.length) return;
        const index = currentIndex();
        if (index < 0) {
            playTrack(0);
            return;
        }
        const prev = index <= 0 ? state.view.length - 1 : index - 1;
        playTrack(prev);
    }

    function playNext(auto) {
        if (!state.view.length) return;

        if (state.mode === "shuffle" && state.view.length > 1) {
            const index = currentIndex();
            let next = index;
            while (next === index) {
                next = Math.floor(Math.random() * state.view.length);
            }
            playTrack(next);
            return;
        }

        const index = currentIndex();
        if (index < 0) {
            playTrack(0);
            return;
        }

        if (auto && state.mode === "one") {
            audio.currentTime = 0;
            const promise = audio.play();
            if (promise && typeof promise.catch === "function") promise.catch(function () { });
            return;
        }

        playTrack(index + 1 >= state.view.length ? 0 : index + 1);
    }

    function cycleMode() {
        state.mode = state.mode === "loop" ? "one" : (state.mode === "one" ? "shuffle" : "loop");
        updateModeIcon();
        saveState();
    }

    function updateModeIcon() {
        if (state.mode === "one") {
            iconMode.innerHTML = ICON_ONE;
            btnMode.title = "单曲循环";
        } else if (state.mode === "shuffle") {
            iconMode.innerHTML = ICON_SHUFFLE;
            btnMode.title = "随机播放";
        } else {
            iconMode.innerHTML = ICON_LOOP;
            btnMode.title = "列表循环";
        }
        btnMode.classList.toggle("active", state.mode !== "loop");
    }

    function syncPlayIcon() {
        const playing = !audio.paused && !!state.currentTrack;
        iconPlay.innerHTML = playing ? ICON_PAUSE : ICON_PLAY;
        fpIconPlay.innerHTML = playing ? ICON_PAUSE : ICON_PLAY;
        fpDisc.classList.toggle("playing", playing);
    }

    function setVolume(value) {
        const next = Math.min(1, Math.max(0, Number(value) || 0));
        audio.volume = next;
        volume.value = Math.round(next * 100);
        fpVolume.value = Math.round(next * 100);
        updateVolIcon(next);
        if (next > 0) lastVolume = next;
        saveState();
    }

    function updateVolIcon(value) {
        const muted = value <= 0;
        [volIcon, fpVolIcon].forEach(function (icon) {
            if (!icon) return;
            icon.classList.toggle("muted", muted);
            const wave = icon.querySelector(".vol-wave");
            const cross = icon.querySelector(".vol-x");
            if (wave) wave.style.display = muted ? "none" : "";
            if (cross) cross.style.display = muted ? "" : "none";
        });
    }

    function toggleMute() {
        if (audio.volume > 0) {
            setVolume(0);
        } else {
            setVolume(lastVolume || 0.8);
        }
    }

    function parseLrc(text) {
        const lines = [];
        String(text).split(/\r?\n/).forEach(function (raw) {
            const tags = raw.match(/\[\d{1,2}:\d{1,2}(?:[.:]\d{1,3})?\]/g);
            if (!tags) return;
            const content = raw.replace(/\[[^\]]*\]/g, "").trim();
            if (!content) return;
            tags.forEach(function (tag) {
                const match = tag.match(/\[(\d{1,2}):(\d{1,2})(?:[.:](\d{1,3}))?\]/);
                if (!match) return;
                const minutes = parseInt(match[1], 10);
                const seconds = parseInt(match[2], 10);
                const fraction = match[3] ? parseInt(match[3], 10) / Math.pow(10, match[3].length) : 0;
                lines.push({ time: minutes * 60 + seconds + fraction, text: content });
            });
        });
        lines.sort(function (a, b) { return a.time - b.time; });
        return lines;
    }

    function renderLyrics() {
        fpLyrics.innerHTML = "";

        if (!state.lyrics.length) {
            const empty = document.createElement("div");
            empty.className = "lyrics-empty";
            empty.textContent = state.currentTrack ? "暂无歌词" : "未播放";
            fpLyrics.appendChild(empty);
            return;
        }

        const fragment = document.createDocumentFragment();
        state.lyrics.forEach(function (line, index) {
            const el = document.createElement("div");
            el.className = "lrc-line";
            el.dataset.index = String(index);
            el.dataset.time = String(line.time);
            el.textContent = line.text;
            fragment.appendChild(el);
        });
        fpLyrics.appendChild(fragment);
        state.lyricIndex = -1;
    }

    function loadLyrics(track) {
        const lrcId = state.lrcMap.get(stemOf(track.path).toLowerCase());
        if (!lrcId) {
            renderLyrics();
            return;
        }

        fetch(fileUrl(lrcId))
            .then(function (response) {
                if (!response.ok) throw new Error("HTTP " + response.status);
                return response.text();
            })
            .then(function (text) {
                if (!state.currentTrack || String(state.currentTrack.id) !== String(track.id)) return;
                state.lyrics = parseLrc(text);
                renderLyrics();
                updateLyricHighlight(true);
            })
            .catch(function () {
                if (!state.currentTrack || String(state.currentTrack.id) !== String(track.id)) return;
                state.lyrics = [];
                renderLyrics();
            });
    }

    function updateLyricHighlight(force) {
        if (!state.lyrics.length) {
            barLyrics.textContent = "—";
            return;
        }

        const time = audio.currentTime;
        let index = -1;
        for (let i = 0; i < state.lyrics.length; i++) {
            if (state.lyrics[i].time <= time) index = i;
            else break;
        }

        if (!force && index === state.lyricIndex) return;
        state.lyricIndex = index;

        barLyrics.textContent = index >= 0 ? state.lyrics[index].text : "—";

        const lines = fpLyrics.querySelectorAll(".lrc-line");
        lines.forEach(function (el, i) {
            el.classList.toggle("active", i === index);
        });

        if (!state.fullpageOpen || state.userScrollingLyrics || index < 0) return;
        const el = lines[index];
        if (!el) return;
        const top = el.offsetTop - fpLyrics.offsetTop;
        fpLyrics.scrollTop = top - fpLyrics.clientHeight / 2 + el.offsetHeight / 2;
    }

    function openFullpage() {
        if (!state.currentTrack) return;
        fullpage.classList.remove("hidden");
        state.fullpageOpen = true;
        updateLyricHighlight(true);
        startSpectrum();
    }

    function closeFullpage() {
        fullpage.classList.add("hidden");
        state.fullpageOpen = false;
        stopSpectrum();
    }

    let audioCtx = null;
    let analyser = null;
    let freqData = null;
    let rafId = 0;

    function ensureAudioGraph() {
        if (audioCtx) {
            if (audioCtx.state === "suspended") audioCtx.resume();
            return;
        }
        const Ctor = window.AudioContext || window.webkitAudioContext;
        if (!Ctor) return;
        try {
            audioCtx = new Ctor();
            const source = audioCtx.createMediaElementSource(audio);
            analyser = audioCtx.createAnalyser();
            analyser.fftSize = 128;
            analyser.smoothingTimeConstant = 0.8;
            freqData = new Uint8Array(analyser.frequencyBinCount);
            source.connect(analyser);
            analyser.connect(audioCtx.destination);
        } catch (e) {
            audioCtx = null;
            analyser = null;
        }
    }

    function startSpectrum() {
        ensureAudioGraph();
        if (!analyser || rafId) return;

        const ctx = fpSpectrum.getContext("2d");
        if (!ctx) return;
        const bars = 48;
        const step = Math.max(1, Math.floor(freqData.length / bars));
        const barWidth = fpSpectrum.width / bars;

        function draw() {
            rafId = requestAnimationFrame(draw);
            analyser.getByteFrequencyData(freqData);
            ctx.clearRect(0, 0, fpSpectrum.width, fpSpectrum.height);

            for (let i = 0; i < bars; i++) {
                const value = (freqData[i * step] || 0) / 255;
                const height = Math.max(2, value * fpSpectrum.height * 0.95);
                const gradient = ctx.createLinearGradient(0, fpSpectrum.height, 0, fpSpectrum.height - height);
                gradient.addColorStop(0, "rgba(59, 130, 246, 0.85)");
                gradient.addColorStop(1, "rgba(147, 197, 253, 0.9)");
                ctx.fillStyle = gradient;
                ctx.fillRect(i * barWidth + 1, fpSpectrum.height - height, barWidth - 2, height);
            }
        }

        draw();
    }

    function stopSpectrum() {
        if (rafId) {
            cancelAnimationFrame(rafId);
            rafId = 0;
        }
        const ctx = fpSpectrum.getContext("2d");
        if (ctx) ctx.clearRect(0, 0, fpSpectrum.width, fpSpectrum.height);
    }

    function saveState() {
        try {
            localStorage.setItem(STORE_KEY, JSON.stringify({
                volume: audio.volume,
                mode: state.mode,
                sortBy: state.sortBy,
                sortOrder: state.sortOrder,
                trackId: state.currentTrack ? state.currentTrack.id : null,
                time: audio.currentTime || 0
            }));
        } catch (e) { }
    }

    function scheduleSave() {
        if (saveTimer) return;
        saveTimer = setTimeout(function () {
            saveTimer = 0;
            saveState();
        }, 3000);
    }

    function restoreState() {
        try {
            const raw = localStorage.getItem(STORE_KEY);
            return raw ? JSON.parse(raw) : null;
        } catch (e) {
            return null;
        }
    }

    function updateSortActive() {
        sortMenu.querySelectorAll(".sort-item").forEach(function (item) {
            const sortBy = item.getAttribute("data-sort-by");
            const sortOrder = item.getAttribute("data-sort-order");
            if (sortBy !== null) {
                item.classList.toggle("active", sortBy === state.sortBy);
            } else if (sortOrder !== null) {
                item.classList.toggle("active", sortOrder === state.sortOrder);
            }
        });
        sortBtn.classList.toggle("active", !!state.sortBy);
    }

    function closeSortMenu() {
        sortMenu.classList.remove("open");
    }

    function bindEvents() {
        sortBtn.addEventListener("click", function (event) {
            event.stopPropagation();
            sortMenu.classList.toggle("open");
        });

        sortMenu.addEventListener("click", function (event) {
            event.stopPropagation();
            const item = event.target.closest(".sort-item");
            if (!item) return;

            const sortBy = item.getAttribute("data-sort-by");
            const sortOrder = item.getAttribute("data-sort-order");
            if (sortBy !== null) state.sortBy = sortBy;
            if (sortOrder !== null) state.sortOrder = sortOrder;

            updateSortActive();
            closeSortMenu();
            applyView();
            saveState();
        });

        document.addEventListener("click", closeSortMenu);

        searchInput.addEventListener("input", function () {
            state.search = searchInput.value;
            applyView();
        });

        searchInput.addEventListener("keydown", function (event) {
            if (event.key === "Escape") {
                searchInput.value = "";
                state.search = "";
                applyView();
            }
        });

        refreshBtn.addEventListener("click", function () {
            load();
        });

        btnPlay.addEventListener("click", togglePlay);
        btnPrev.addEventListener("click", playPrev);
        btnNext.addEventListener("click", playNext);
        btnMode.addEventListener("click", cycleMode);

        fpPlay.addEventListener("click", togglePlay);
        fpPrev.addEventListener("click", playPrev);
        fpNext.addEventListener("click", playNext);
        fpClose.addEventListener("click", closeFullpage);

        cover.addEventListener("click", openFullpage);
        infoBox.addEventListener("click", openFullpage);

        progress.addEventListener("input", function () {
            if (audio.duration) {
                audio.currentTime = (progress.value / 1000) * audio.duration;
            }
        });

        fpProgress.addEventListener("input", function () {
            if (audio.duration) {
                audio.currentTime = (fpProgress.value / 1000) * audio.duration;
            }
        });

        volume.addEventListener("input", function () {
            setVolume(volume.value / 100);
        });

        fpVolume.addEventListener("input", function () {
            setVolume(fpVolume.value / 100);
        });

        volIcon.addEventListener("click", toggleMute);
        fpVolIcon.addEventListener("click", toggleMute);

        let drag = { active: false, moved: false, startY: 0, startScroll: 0 };

        fpLyrics.addEventListener("mousedown", function (event) {
            drag.active = true;
            drag.moved = false;
            drag.startY = event.clientY;
            drag.startScroll = fpLyrics.scrollTop;
            state.userScrollingLyrics = true;
            fpLyrics.classList.add("dragging");
        });

        document.addEventListener("mousemove", function (event) {
            if (!drag.active) return;
            const delta = drag.startY - event.clientY;
            if (Math.abs(delta) > 3) drag.moved = true;
            fpLyrics.scrollTop = drag.startScroll + delta;
        });

        document.addEventListener("mouseup", function (event) {
            if (!drag.active) return;
            drag.active = false;
            fpLyrics.classList.remove("dragging");

            if (!drag.moved) {
                const line = event.target.closest(".lrc-line");
                if (line && audio.duration) {
                    const time = parseFloat(line.getAttribute("data-time"));
                    if (!isNaN(time)) audio.currentTime = Math.min(time, audio.duration);
                }
            }

            setTimeout(function () {
                state.userScrollingLyrics = false;
            }, 3000);
        });

        fpLyrics.addEventListener("wheel", function () {
            state.userScrollingLyrics = true;
            clearTimeout(fpLyrics.wheelTimer);
            fpLyrics.wheelTimer = setTimeout(function () {
                state.userScrollingLyrics = false;
            }, 3000);
        });

        audio.addEventListener("play", function () {
            ensureAudioGraph();
            syncPlayIcon();
            renderList();
        });

        audio.addEventListener("pause", function () {
            syncPlayIcon();
            renderList();
            saveState();
        });

        audio.addEventListener("ended", function () {
            if (state.mode === "one") {
                audio.currentTime = 0;
                const promise = audio.play();
                if (promise && typeof promise.catch === "function") promise.catch(function () { });
            } else {
                playNext(true);
            }
            saveState();
        });

        audio.addEventListener("timeupdate", function () {
            timeCur.textContent = fmtTime(audio.currentTime);
            fpTimeCur.textContent = fmtTime(audio.currentTime);
            if (audio.duration) {
                const ratio = (audio.currentTime / audio.duration) * 1000;
                progress.value = Math.round(ratio);
                fpProgress.value = Math.round(ratio);
            }
            updateLyricHighlight(false);
            scheduleSave();
        });

        audio.addEventListener("loadedmetadata", function () {
            timeTotal.textContent = fmtTime(audio.duration);
            fpTimeTotal.textContent = fmtTime(audio.duration);
            if (state.pendingSeek !== null && audio.duration) {
                audio.currentTime = Math.min(state.pendingSeek, audio.duration);
                state.pendingSeek = null;
            }
        });

        audio.addEventListener("error", function () {
            if (!state.currentTrack) return;
            nowTitle.textContent = "播放失败";
            fpTitle.textContent = "播放失败";
        });

        document.addEventListener("keydown", function (event) {
            const tag = event.target.tagName;
            if (tag === "INPUT" || tag === "TEXTAREA") {
                if (event.key === "Escape" && state.fullpageOpen) closeFullpage();
                return;
            }

            if (event.code === "Space") {
                event.preventDefault();
                togglePlay();
            } else if (event.code === "ArrowRight") {
                if (audio.duration) audio.currentTime = Math.min(audio.duration, audio.currentTime + 5);
            } else if (event.code === "ArrowLeft") {
                audio.currentTime = Math.max(0, audio.currentTime - 5);
            } else if (event.code === "ArrowUp") {
                event.preventDefault();
                setVolume(Math.min(1, audio.volume + 0.05));
            } else if (event.code === "ArrowDown") {
                event.preventDefault();
                setVolume(Math.max(0, audio.volume - 0.05));
            } else if (event.code === "Escape") {
                if (state.fullpageOpen) closeFullpage();
                else closeSortMenu();
            }
        });
    }

    async function loadData() {
        if (!token) {
            dirText.textContent = "—";
            state.all = [];
            state.view = [];
            renderList();
            return;
        }

        dirText.textContent = "加载中…";

        let files = [];
        try {
            const response = await fetch("/api/files?token=" + encodeURIComponent(token));
            if (!response.ok) {
                dirText.textContent = "—";
                state.all = [];
                state.view = [];
                renderList();
                return;
            }
            const data = await response.json();
            files = data.files || [];
        } catch (e) {
            dirText.textContent = "—";
            state.all = [];
            state.view = [];
            renderList();
            return;
        }

        const current = selfId
            ? files.find(function (file) { return String(file.id) === String(selfId); })
            : null;

        if (!current) {
            dirText.textContent = "—";
            state.all = [];
            state.view = [];
            renderList();
            return;
        }

        const baseDir = dirOf(current.path);
        dirText.textContent = baseDir || "/";

        state.lrcMap = new Map();
        files.forEach(function (file) {
            const path = String(file.path).replace(/\\/g, "/");
            if (dirOf(path) !== baseDir) return;
            if (!LRC_EXT.includes(extOf(path))) return;
            state.lrcMap.set(stemOf(path).toLowerCase(), file.id);
        });

        state.all = files
            .filter(function (file) {
                const path = String(file.path).replace(/\\/g, "/");
                return dirOf(path) === baseDir && AUDIO_EXT.includes(extOf(path));
            })
            .sort(function (a, b) {
                return String(a.path).localeCompare(String(b.path));
            });

        applyView();
    }

    async function load(onDone) {
        if (loading) return;
        loading = true;
        refreshBtn.classList.add("spinning");
        try {
            await loadData();
        } finally {
            loading = false;
            refreshBtn.classList.remove("spinning");
            if (typeof onDone === "function") onDone();
        }
    }

    function init() {
        bindEvents();

        if (typeof jsmediatags !== "undefined" && jsmediatags.Config &&
            typeof jsmediatags.Config.EXPERIMENTAL_avoidHeadRequests === "function") {
            jsmediatags.Config.EXPERIMENTAL_avoidHeadRequests();
        }

        const saved = restoreState();
        if (saved) {
            if (typeof saved.volume === "number") {
                audio.volume = Math.min(1, Math.max(0, saved.volume));
                if (audio.volume > 0) lastVolume = audio.volume;
            }
            if (saved.mode) state.mode = saved.mode;
            if (saved.sortBy) state.sortBy = saved.sortBy;
            if (saved.sortOrder) state.sortOrder = saved.sortOrder;
        }

        volume.value = Math.round(audio.volume * 100);
        fpVolume.value = Math.round(audio.volume * 100);
        updateVolIcon(audio.volume);
        updateModeIcon();
        updateSortActive();
        syncPlayIcon();

        load(function () {
            if (!saved || saved.trackId === null || saved.trackId === undefined) return;
            const index = state.view.findIndex(function (track) {
                return String(track.id) === String(saved.trackId);
            });
            if (index >= 0) {
                playTrack(index, saved.time || 0, false);
            }
        });
    }

    init();

})();