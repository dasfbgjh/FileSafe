(function () {
    var THEME_KEY = "filesafe-theme";

    function systemTheme() {
        try {
            if (window.matchMedia && window.matchMedia("(prefers-color-scheme: light)").matches) {
                return "light";
            }
        } catch (e) { }
        return "dark";
    }

    function resolveTheme(value) {
        return (value === "light" || value === "dark") ? value : systemTheme();
    }

    function applyTheme(theme) {
        document.documentElement.setAttribute("data-theme", theme);
    }

    var saved = null;
    try {
        saved = localStorage.getItem(THEME_KEY);
    } catch (e) { }
    applyTheme(resolveTheme(saved));

    window.addEventListener("storage", function (event) {
        if (event.key !== THEME_KEY && event.key !== null) return;
        applyTheme(resolveTheme(event.newValue));
    });
})();
