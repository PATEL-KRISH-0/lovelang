(function () {
    const anchors = document.querySelectorAll(".doc-anchor");
    const sidebarLinks = Array.from(document.querySelectorAll(".sb-link"));
    const searchInput = document.querySelector(".docs-search input");
    const copyButtons = Array.from(document.querySelectorAll(".cb-copy"));
    const faqItems = Array.from(document.querySelectorAll(".faq-item"));
    const nav = document.querySelector("nav");
    const navToggle = document.getElementById("nav-toggle");

    function highlightSidebarFromScroll() {
        if (!("IntersectionObserver" in window) || !anchors.length) {
            return;
        }

        const observer = new IntersectionObserver(
            function (entries) {
                entries.forEach(function (entry) {
                    if (!entry.isIntersecting) {
                        return;
                    }

                    const section = entry.target.parentElement;
                    const id = section ? section.id : "";
                    if (!id) {
                        return;
                    }

                    sidebarLinks.forEach(function (link) {
                        const href = link.getAttribute("href") || "";
                        link.classList.toggle("active", href === "#" + id);
                    });
                });
            },
            { rootMargin: "-80px 0px -60% 0px" }
        );

        anchors.forEach(function (anchor) {
            observer.observe(anchor);
        });
    }

    function wireSidebarSearch() {
        if (!searchInput) {
            return;
        }

        searchInput.addEventListener("input", function () {
            const query = searchInput.value.trim().toLowerCase();

            sidebarLinks.forEach(function (link) {
                const label = (link.textContent || "").toLowerCase();
                const match = !query || label.includes(query);
                link.style.display = match ? "flex" : "none";
            });
        });
    }

    function normalizeCodeText(text) {
        return String(text || "")
            .replace(/\u00a0/g, " ")
            .replace(/\n{3,}/g, "\n\n")
            .trim();
    }

    function wireCopyButtons() {
        copyButtons.forEach(function (button) {
            button.addEventListener("click", async function () {
                const block = button.closest(".code-block");
                const body = block ? block.querySelector(".cb-body") : null;
                if (!body) {
                    return;
                }

                const codeText = normalizeCodeText(body.innerText || body.textContent);
                const original = button.innerHTML;

                try {
                    if (!navigator.clipboard || !navigator.clipboard.writeText) {
                        throw new Error("Clipboard API unavailable");
                    }

                    await navigator.clipboard.writeText(codeText);
                    button.innerHTML = '<i class="ri-check-line"></i> copied';
                } catch (err) {
                    button.innerHTML = '<i class="ri-close-line"></i> failed';
                    window.console.warn("[lovelang] copy failed", err);
                }

                window.setTimeout(function () {
                    button.innerHTML = original;
                }, 1400);
            });
        });
    }

    function wireFaqAccordion() {
        if (!faqItems.length) {
            return;
        }

        faqItems.forEach(function (item, index) {
            const q = item.querySelector(".faq-q");
            const a = item.querySelector(".faq-a");
            const icon = q ? q.querySelector("i") : null;

            if (!q || !a) {
                return;
            }

            const expanded = index === 0;
            a.style.display = expanded ? "block" : "none";
            q.setAttribute("aria-expanded", expanded ? "true" : "false");
            if (icon) {
                icon.className = expanded ? "ri-arrow-up-s-line" : "ri-arrow-down-s-line";
            }

            q.addEventListener("click", function () {
                const isOpen = a.style.display !== "none";

                faqItems.forEach(function (other) {
                    const oq = other.querySelector(".faq-q");
                    const oa = other.querySelector(".faq-a");
                    const oi = oq ? oq.querySelector("i") : null;
                    if (!oq || !oa) {
                        return;
                    }
                    oa.style.display = "none";
                    oq.setAttribute("aria-expanded", "false");
                    if (oi) {
                        oi.className = "ri-arrow-down-s-line";
                    }
                });

                if (!isOpen) {
                    a.style.display = "block";
                    q.setAttribute("aria-expanded", "true");
                    if (icon) {
                        icon.className = "ri-arrow-up-s-line";
                    }
                }
            });
        });
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
                if (window.innerWidth <= 980) {
                    closeMenu();
                }
            });
        });

        window.addEventListener("resize", function () {
            if (window.innerWidth > 980) {
                closeMenu();
            }
        });

        setExpanded(false);
    }

    highlightSidebarFromScroll();
    wireSidebarSearch();
    wireCopyButtons();
    wireFaqAccordion();
    wireMobileNav();
})();
