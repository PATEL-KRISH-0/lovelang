const samplePrograms = {
    "Romantic Hello": `yaad name hai "patel krish"
bolo "hello " milan name
love you baby byeee`,
    "Loop Count": `yaad i hai 0
intezaar i chhota hai 5 tab tak
  bolo i
  i hai i milan 1
bas itna hi
love you baby byeee`,
    "Conditionals": `yaad mood hai "shayari"
agar mood barabar hai "shayari" toh
  bolo "aaj lafzon mein baarish hai"
warna
  bolo "normal vibe"
bas itna hi
love you baby byeee`,
    "Collections": `yaad names hai list_nayi()
list_daal(names, "patel krish")
list_daal(names, "rani")
bolo lafz_join(names, ", ")
love you baby byeee`
};

const sampleSelect = document.getElementById("sample-select");
const modeSelect = document.getElementById("mode-select");
const codeEditor = document.getElementById("code-editor");
const loadRuntimeBtn = document.getElementById("load-runtime-btn");
const runBtn = document.getElementById("run-btn");
const clearOutputBtn = document.getElementById("clear-output-btn");
const outputConsole = document.getElementById("output-console");
const runtimeStatus = document.getElementById("runtime-status");
const runtimeStatusText = document.getElementById("runtime-status-text");
const nav = document.querySelector("nav");
const navToggle = document.getElementById("nav-toggle");

let moduleInstance = null;
let modulePromise = null;
let outputBuffer = [];

function setStatus(kind, text) {
    runtimeStatus.classList.remove("ready", "error");
    if (kind === "ready") {
        runtimeStatus.classList.add("ready");
    }
    if (kind === "error") {
        runtimeStatus.classList.add("error");
    }
    runtimeStatusText.textContent = text;
}

function clearOutput() {
    outputBuffer = [];
    outputConsole.textContent = "";
}

function appendOutput(line) {
    outputBuffer.push(String(line));
    outputConsole.textContent = outputBuffer.join("\n");
    outputConsole.scrollTop = outputConsole.scrollHeight;
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

    navToggle.addEventListener("click", () => {
        const expanded = nav.classList.toggle("mobile-open");
        setExpanded(expanded);
    });

    nav.querySelectorAll("a").forEach((link) => {
        link.addEventListener("click", () => {
            if (window.innerWidth <= 900) {
                closeMenu();
            }
        });
    });

    window.addEventListener("resize", () => {
        if (window.innerWidth > 900) {
            closeMenu();
        }
    });

    setExpanded(false);
}

function loadSamples() {
    Object.keys(samplePrograms).forEach((name) => {
        const option = document.createElement("option");
        option.value = name;
        option.textContent = name;
        sampleSelect.appendChild(option);
    });

    sampleSelect.addEventListener("change", () => {
        codeEditor.value = samplePrograms[sampleSelect.value] || "";
    });

    sampleSelect.value = Object.keys(samplePrograms)[0];
    codeEditor.value = samplePrograms[sampleSelect.value];
}

async function importFactory() {
    const mod = await import("../dist/lovelang.js");
    return mod.default || mod.createLovelangModule || mod.createModule;
}

async function ensureRuntimeLoaded() {
    if (moduleInstance) {
        return moduleInstance;
    }

    if (!modulePromise) {
        modulePromise = (async () => {
            setStatus("loading", "Loading WASM runtime...");
            const factory = await importFactory();
            if (typeof factory !== "function") {
                throw new Error("WASM module factory not found. Expected default export from dist/lovelang.js");
            }

            const mod = await factory({
                noInitialRun: true,
                print: (line) => appendOutput(line),
                printErr: (line) => appendOutput(line)
            });

            if (typeof mod.callMain !== "function") {
                throw new Error("Runtime loaded but callMain is unavailable. Rebuild WASM with callMain exported.");
            }

            moduleInstance = mod;
            setStatus("ready", "Runtime loaded");
            return moduleInstance;
        })().catch((err) => {
            modulePromise = null;
            setStatus("error", "Failed to load runtime");
            appendOutput("[load-error] " + (err && err.message ? err.message : String(err)));
            throw err;
        });
    }

    return modulePromise;
}

function safeUnlink(path) {
    try {
        if (moduleInstance.FS.analyzePath(path).exists) {
            moduleInstance.FS.unlink(path);
        }
    } catch (_) {
        // Ignore cleanup failures.
    }
}

function runOnce() {
    const source = codeEditor.value;
    if (!source.trim()) {
        appendOutput("[runner] Please write some Lovelang code first.");
        return;
    }

    clearOutput();

    const mode = modeSelect.value || "romantic";
    const filePath = "/playground_input.love";
    safeUnlink(filePath);
    moduleInstance.FS.writeFile(filePath, source);

    try {
        moduleInstance.callMain([filePath, "--mode=" + mode]);
    } catch (err) {
        const text = String(err && err.message ? err.message : err);
        if (!/ExitStatus/.test(text)) {
            appendOutput("[runtime-error] " + text);
            setStatus("error", "Runtime error while running");
            return;
        }
    } finally {
        safeUnlink(filePath);
    }

    if (outputBuffer.length === 0) {
        appendOutput("[runner] Program finished with no output.");
    }
    setStatus("ready", "Run completed");
}

async function runProgram() {
    runBtn.disabled = true;
    try {
        await ensureRuntimeLoaded();
        runOnce();
    } finally {
        runBtn.disabled = false;
    }
}

loadRuntimeBtn.addEventListener("click", async () => {
    loadRuntimeBtn.disabled = true;
    try {
        await ensureRuntimeLoaded();
    } finally {
        loadRuntimeBtn.disabled = false;
    }
});

runBtn.addEventListener("click", runProgram);
clearOutputBtn.addEventListener("click", clearOutput);

document.addEventListener("keydown", (event) => {
    const isEnter = event.key === "Enter";
    const withModifier = event.metaKey || event.ctrlKey;
    if (isEnter && withModifier) {
        event.preventDefault();
        runProgram();
    }
});

wireMobileNav();
loadSamples();
