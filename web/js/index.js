(function () {
    const installCode = document.getElementById("install-code");
    const installTabs = Array.from(document.querySelectorAll("#install-tabs .i-tab"));
    const copyButton = document.getElementById("copy-install-btn");
    const emailInput = document.querySelector('.email-form input[type="email"]');
    const emailButton = document.querySelector(".email-form button");
    const nav = document.querySelector("nav");
    const navToggle = document.getElementById("nav-toggle");

    const commandSets = {
        unix: [
            { prompt: "#", cmd: "install once globally", muted: true },
            { prompt: "$", cmd: "npm install -g lovelang-cli" },
            { prompt: "#", cmd: "interpret a .love file", muted: true, gap: true },
            { prompt: "$", cmd: "lovelang hello.love" },
            { prompt: "#", cmd: "run in shayari mood", muted: true, gap: true },
            { prompt: "$", cmd: "lovelang hello.love --mode shayari" },
            { prompt: "#", cmd: "compile to native binary (no external toolchain)", muted: true, gap: true },
            { prompt: "$", cmd: "lovelang hello.love --native --out ./hello" }
        ],
        windows: [
            { prompt: "#", cmd: "install once globally", muted: true },
            { prompt: "PS>", cmd: "npm install -g lovelang-cli" },
            { prompt: "#", cmd: "interpret a .love file", muted: true, gap: true },
            { prompt: "PS>", cmd: "lovelang hello.love" },
            { prompt: "#", cmd: "run in toxic mood", muted: true, gap: true },
            { prompt: "PS>", cmd: "lovelang hello.love --mode toxic" },
            { prompt: "#", cmd: "compile to native binary", muted: true, gap: true },
            { prompt: "PS>", cmd: "lovelang hello.love --native --out hello.exe" }
        ]
    };

    let activeOS = "unix";

    function escapeHtml(value) {
        return String(value)
            .replace(/&/g, "&amp;")
            .replace(/</g, "&lt;")
            .replace(/>/g, "&gt;")
            .replace(/\"/g, "&quot;")
            .replace(/'/g, "&#39;");
    }

    function linesToText(lines) {
        return lines
            .filter(function (line) { return !line.muted; })
            .map(function (line) { return line.cmd; })
            .join("\n");
    }

    function renderInstallCode(os) {
        if (!installCode || !commandSets[os]) {
            return;
        }

        installCode.innerHTML = commandSets[os]
            .map(function (line) {
                const cmdStyle = line.muted ? ' style="color:var(--muted)"' : "";
                const gapStyle = line.gap ? ' style="margin-top:8px"' : "";
                return (
                    '<div class="install-line"' +
                    gapStyle +
                    '><span class="prompt">' +
                    escapeHtml(line.prompt) +
                    '</span><span class="cmd"' +
                    cmdStyle +
                    ">" +
                    escapeHtml(line.cmd) +
                    "</span></div>"
                );
            })
            .join("");
    }

    function setActiveTab(os) {
        activeOS = os;
        installTabs.forEach(function (tab) {
            tab.classList.toggle("active", tab.dataset.os === os);
        });
        renderInstallCode(os);
        if (copyButton) {
            copyButton.innerHTML = '<i class="ri-file-copy-line"></i> Copy commands';
        }
    }

    async function copyCommands() {
        if (!copyButton) {
            return;
        }

        const lines = commandSets[activeOS] || [];
        const text = linesToText(lines);

        try {
            if (!navigator.clipboard || !navigator.clipboard.writeText) {
                throw new Error("Clipboard API unavailable");
            }
            await navigator.clipboard.writeText(text);
            copyButton.innerHTML = '<i class="ri-check-line"></i> Copied!';
        } catch (err) {
            copyButton.innerHTML = '<i class="ri-close-line"></i> Copy failed';
            window.console.warn("[lovelang] copy failed", err);
        }
    }

    function subscribeNewsletter() {
        if (!emailInput || !emailButton) {
            return;
        }

        const email = emailInput.value.trim();
        if (!email || !email.includes("@")) {
            emailButton.innerHTML = '<i class="ri-error-warning-line"></i> Enter valid email';
            return;
        }

        emailButton.innerHTML = '<i class="ri-heart-fill"></i> Subscribed';
        emailInput.value = "";
    }

    function wireMobileNav() {
        if (!nav || !navToggle) {
            return;
        }

        function setExpanded(expanded) {
            const icon = navToggle.querySelector("i");
            navToggle.setAttribute("aria-expanded", expanded ? "true" : "false");
            if (icon) {
                icon.className = expanded ? "ri-close-line" : "ri-menu-line";
            }
        }

        function closeMenu() {
            nav.classList.remove("mobile-open");
            setExpanded(false);
        }

        navToggle.addEventListener("click", function () {
            const expanded = nav.classList.toggle("mobile-open");
            setExpanded(expanded);
        });

        nav.querySelectorAll("a").forEach(function (link) {
            link.addEventListener("click", function () {
                if (window.innerWidth <= 900) {
                    closeMenu();
                }
            });
        });

        window.addEventListener("resize", function () {
            if (window.innerWidth > 900) {
                closeMenu();
            }
        });

        setExpanded(false);
    }

    installTabs.forEach(function (tab) {
        tab.addEventListener("click", function () {
            setActiveTab(tab.dataset.os || "unix");
        });
    });

    if (copyButton) {
        copyButton.addEventListener("click", copyCommands);
    }

    if (emailButton) {
        emailButton.addEventListener("click", subscribeNewsletter);
    }

    if (emailInput) {
        emailInput.addEventListener("keydown", function (event) {
            if (event.key === "Enter") {
                event.preventDefault();
                subscribeNewsletter();
            }
        });
    }

    wireMobileNav();
    setActiveTab(activeOS);
})();


/* ═══════════════════════════════════════════════════════
   Custom cursor
═══════════════════════════════════════════════════════ */
(function () {
    const dot = document.getElementById('cursor-dot');
    const ring = document.getElementById('cursor-ring');
    let mx = -200, my = -200;
    let rx = -200, ry = -200;

    document.addEventListener('mousemove', e => {
        mx = e.clientX; my = e.clientY;
        dot.style.left = mx + 'px';
        dot.style.top = my + 'px';
    });

    /* ring lags behind with lerp */
    (function lerp() {
        rx += (mx - rx) * 0.14;
        ry += (my - ry) * 0.14;
        ring.style.left = rx + 'px';
        ring.style.top = ry + 'px';
        requestAnimationFrame(lerp);
    })();

    document.addEventListener('mousedown', () => {
        dot.classList.add('clicking');
        ring.classList.add('clicking');
    });
    document.addEventListener('mouseup', () => {
        dot.classList.remove('clicking');
        ring.classList.remove('clicking');
    });

    /* grow on hoverable elements */
    document.querySelectorAll('a, button, [role="button"]').forEach(el => {
        el.addEventListener('mouseenter', () => {
            dot.classList.add('on-link');
            ring.classList.add('on-link');
        });
        el.addEventListener('mouseleave', () => {
            dot.classList.remove('on-link');
            ring.classList.remove('on-link');
        });
    });
})();

/* ═══════════════════════════════════════════════════════
   Background particle canvas
   Drifting hearts ❤ + glowing dots, very subtle
═══════════════════════════════════════════════════════ */
(function () {
    const canvas = document.getElementById('bg-canvas');
    const ctx = canvas.getContext('2d');
    const COLORS = ['#ff3c50', '#ff7088', '#ff9eb5', '#ce93d8', '#4fc3f7', '#f48fb1'];
    const COUNT = 75;
    const REPEL_RADIUS = 120;   /* px — cursor influence zone    */
    const REPEL_FORCE = 2.8;   /* strength of push              */
    let W, H, particles = [];
    let mouseX = -9999, mouseY = -9999;

    document.addEventListener('mousemove', e => {
        mouseX = e.clientX;
        mouseY = e.clientY;
    });
    document.addEventListener('mouseleave', () => { mouseX = -9999; mouseY = -9999; });

    function resize() {
        W = canvas.width = document.documentElement.clientWidth;
        H = canvas.height = document.documentElement.clientHeight;
    }
    window.addEventListener('resize', resize);
    resize();

    function rand(a, b) { return a + Math.random() * (b - a); }

    function Particle() {
        this.reset = function (fresh) {
            this.x = rand(0, W);
            this.y = fresh ? rand(0, H) : H + rand(20, 80);
            this.size = rand(8, 24);
            this.speed = rand(0.2, 0.7);
            this.vx = rand(-0.3, 0.3);
            this.alpha = rand(0.18, 0.48);
            this.color = COLORS[Math.floor(rand(0, COLORS.length))];
            this.type = Math.random() < 0.35 ? 'heart' : 'dot';
            this.angle = rand(0, Math.PI * 2);
            this.spin = rand(-0.005, 0.005);
            this.wobble = rand(0, Math.PI * 2);
            this.wobbleSpeed = rand(0.005, 0.02);
        };
        this.reset(true);
    }

    for (let i = 0; i < COUNT; i++) particles.push(new Particle());

    function drawHeart(ctx, x, y, size, color, alpha) {
        ctx.save();
        ctx.globalAlpha = alpha;
        ctx.fillStyle = color;
        ctx.beginPath();
        ctx.moveTo(x, y + size * 0.3);
        ctx.bezierCurveTo(x, y, x - size * 0.5, y, x - size * 0.5, y + size * 0.3);
        ctx.bezierCurveTo(x - size * 0.5, y + size * 0.65, x, y + size * 0.9, x, y + size);
        ctx.bezierCurveTo(x, y + size * 0.9, x + size * 0.5, y + size * 0.65, x + size * 0.5, y + size * 0.3);
        ctx.bezierCurveTo(x + size * 0.5, y, x, y, x, y + size * 0.3);
        ctx.fill();
        ctx.restore();
    }

    function drawDot(ctx, x, y, size, color, alpha) {
        ctx.save();
        ctx.globalAlpha = alpha;
        const g = ctx.createRadialGradient(x, y, 0, x, y, size);
        g.addColorStop(0, color);
        g.addColorStop(1, 'transparent');
        ctx.fillStyle = g;
        ctx.beginPath();
        ctx.arc(x, y, size, 0, Math.PI * 2);
        ctx.fill();
        ctx.restore();
    }

    function tick() {
        ctx.clearRect(0, 0, W, H);
        particles.forEach(p => {
            p.wobble += p.wobbleSpeed;
            p.angle += p.spin;
            p.x += p.vx + Math.sin(p.wobble) * 0.3;
            p.y -= p.speed;

            // Cursor repulsion interaction
            if (mouseX !== -9999 && mouseY !== -9999) {
                const dx = p.x - mouseX;
                const dy = p.y - mouseY;
                const dist = Math.hypot(dx, dy);
                if (dist < REPEL_RADIUS && dist > 0) {
                    const force = (1 - dist / REPEL_RADIUS) * REPEL_FORCE;
                    p.x += (dx / dist) * force;
                    p.y += (dy / dist) * force;
                }
            }

            if (p.y + p.size * 2 < 0) p.reset(false);

            ctx.save();
            ctx.translate(p.x, p.y);
            ctx.rotate(p.angle);
            if (p.type === 'heart') {
                drawHeart(ctx, -p.size * 0.5, -p.size * 0.5, p.size, p.color, p.alpha);
            } else {
                drawDot(ctx, 0, 0, p.size * 0.5, p.color, p.alpha);
            }
            ctx.restore();
        });
        requestAnimationFrame(tick);
    }
    tick();
})();