const params = new URLSearchParams(window.location.search);
const code = document.querySelector("#source-code code");
const panel = document.querySelector("#source-code");
const lineNumbers = document.querySelector(".line-numbers");
const copy = document.querySelector(".copy-button");
const debugOverlay = document.querySelector(".debug-overlay");
const debugToggle = document.querySelector(".debug-toggle");
const debugValues = Object.fromEntries(
  [...document.querySelectorAll("[data-debug]")].map((item) => [
    item.dataset.debug,
    item
  ])
);
const tabs = [...document.querySelectorAll("[data-source]")];
const sources = {
  c: document.body.dataset.cSource,
  usl: document.body.dataset.uslSource,
  wgsl: document.body.dataset.wgslSource
};
const cache = new Map();
let debugExpanded = !params.has("embed") && params.get("debug") !== "0";
let debugLoopRunning = false;
let debugLastTime = 0;
let debugFrames = 0;

const keywords = {
  c: new Set([
    "auto", "break", "case", "const", "continue", "default", "do", "else",
    "enum", "extern", "for", "goto", "if", "inline", "register", "restrict",
    "return", "sizeof", "static", "struct", "switch", "typedef", "union",
    "volatile", "while", "_Alignas", "_Alignof", "_Atomic", "_Bool",
    "_Static_assert"
  ]),
  usl: new Set([
    "break", "case", "const", "continue", "discard", "else", "false", "for",
    "frag", "if", "in", "inout", "kern", "let", "out", "return", "struct",
    "switch", "true", "var", "vert", "while"
  ]),
  wgsl: new Set([
    "alias", "break", "case", "const", "const_assert", "continue", "continuing",
    "default", "diagnostic", "discard", "else", "enable", "false", "fn", "for",
    "if", "let", "loop", "override", "requires", "return", "struct", "switch",
    "true", "var", "while"
  ])
};

const scalarTypes = new Set([
  "bool", "char", "double", "f16", "f32", "f64", "float", "half", "i16",
  "i32", "i64", "i8", "int", "int16_t", "int32_t", "int64_t", "int8_t",
  "long", "short", "size_t", "u16", "u32", "u64", "u8", "uint",
  "uint16_t", "uint32_t", "uint64_t", "uint8_t", "void"
]);

function escapeHTML(value) {
  return value.replace(/[&<>]/g, (character) => ({
    "&": "&amp;", "<": "&lt;", ">": "&gt;"
  })[character]);
}

function token(className, value) {
  return `<span class="syntax-${className}">${escapeHTML(value)}</span>`;
}

function isIdentifierStart(character) {
  return /[A-Za-z_]/.test(character);
}

function isIdentifierPart(character) {
  return /[A-Za-z0-9_]/.test(character);
}

function highlightSource(source, kind) {
  let output = "";
  let index = 0;
  let lineStart = true;

  while (index < source.length) {
    const character = source[index];
    const next = source[index + 1];

    if (character === "\n") {
      output += "\n";
      index++;
      lineStart = true;
      continue;
    }

    if (/\s/.test(character)) {
      output += character;
      index++;
      continue;
    }

    if (character === "/" && next === "/") {
      const end = source.indexOf("\n", index);
      const limit = end < 0 ? source.length : end;
      output += token("comment", source.slice(index, limit));
      index = limit;
      lineStart = false;
      continue;
    }

    if (character === "/" && next === "*") {
      const end = source.indexOf("*/", index + 2);
      const limit = end < 0 ? source.length : end + 2;
      output += token("comment", source.slice(index, limit));
      lineStart = source[limit - 1] === "\n";
      index = limit;
      continue;
    }

    if (kind === "c" && lineStart && character === "#") {
      const end = source.indexOf("\n", index);
      const limit = end < 0 ? source.length : end;
      output += token("preprocessor", source.slice(index, limit));
      index = limit;
      lineStart = false;
      continue;
    }

    if ((character === "@" || character === "#") && isIdentifierStart(next)) {
      let end = index + 2;
      while (end < source.length && isIdentifierPart(source[end])) end++;
      output += token("attribute", source.slice(index, end));
      index = end;
      lineStart = false;
      continue;
    }

    if (character === "\"" || character === "'") {
      let end = index + 1;
      while (end < source.length) {
        if (source[end] === "\\") {
          end += 2;
          continue;
        }
        if (source[end++] === character) break;
      }
      output += token("string", source.slice(index, end));
      index = end;
      lineStart = false;
      continue;
    }

    if (/[0-9]/.test(character) || (character === "." && /[0-9]/.test(next))) {
      let end = index + 1;
      while (end < source.length && /[A-Fa-f0-9._xX+-]/.test(source[end])) end++;
      output += token("number", source.slice(index, end));
      index = end;
      lineStart = false;
      continue;
    }

    if (isIdentifierStart(character)) {
      let end = index + 1;
      while (end < source.length && isIdentifierPart(source[end])) end++;
      const value = source.slice(index, end);
      let className = "";

      if (keywords[kind].has(value)) className = "keyword";
      else if (scalarTypes.has(value) || /^(GPU|USL?)[A-Z]/.test(value) || /_t$/.test(value)) className = "type";
      else if (/^\s*\(/.test(source.slice(end))) className = "function";

      output += className ? token(className, value) : value;
      index = end;
      lineStart = false;
      continue;
    }

    output += escapeHTML(character);
    index++;
    lineStart = false;
  }

  return output;
}

if (params.has("embed")) {
  document.body.classList.add("embed");
}

function formatBytes(bytes) {
  const units = ["B", "KiB", "MiB", "GiB"];
  let value = bytes;
  let unit = 0;

  while (value >= 1024 && unit < units.length - 1) {
    value /= 1024;
    unit++;
  }
  return `${value.toFixed(unit > 1 ? 1 : 0)} ${units[unit]}`;
}

function debugFrame(now) {
  if (!debugExpanded) {
    debugLoopRunning = false;
    return;
  }

  debugFrames++;
  if (debugLastTime === 0) debugLastTime = now;
  const elapsed = now - debugLastTime;
  if (elapsed >= 500) {
    const fps = debugFrames * 1000 / elapsed;
    const frameMs = elapsed / debugFrames;
    const canvas = document.querySelector("#canvas");
    const heapBytes = typeof HEAP8 !== "undefined" && HEAP8.buffer
      ? HEAP8.buffer.byteLength
      : 0;

    debugValues.fps.textContent = `${fps.toFixed(1)} FPS`;
    debugValues.frame.textContent = `${frameMs.toFixed(2)} ms frame`;
    debugValues.heap.textContent = `${heapBytes ? formatBytes(heapBytes) : "n/a"} Wasm heap`;
    debugValues.canvas.textContent = `${canvas.width} × ${canvas.height} px`;
    debugFrames = 0;
    debugLastTime = now;
  }
  requestAnimationFrame(debugFrame);
}

function setDebugExpanded(expanded) {
  debugExpanded = expanded;
  debugOverlay.dataset.expanded = String(expanded);
  debugToggle.setAttribute("aria-expanded", String(expanded));
  if (expanded && !debugLoopRunning) {
    debugLoopRunning = true;
    debugLastTime = 0;
    debugFrames = 0;
    requestAnimationFrame(debugFrame);
  }
}

async function showSource(kind) {
  tabs.forEach((tab) => {
    const active = tab.dataset.source === kind;
    tab.setAttribute("aria-selected", String(active));
    tab.tabIndex = active ? 0 : -1;
  });
  panel.setAttribute("aria-labelledby", `tab-${kind}`);
  code.textContent = "Loading source…";
  lineNumbers.textContent = "";

  try {
    if (!cache.has(kind)) {
      const response = await fetch(sources[kind]);
      if (!response.ok) throw new Error(`${response.status} ${response.statusText}`);
      cache.set(kind, await response.text());
    }
    const source = cache.get(kind);
    code.className = `language-${kind}`;
    code.innerHTML = highlightSource(source, kind);
    lineNumbers.textContent = Array.from(
      { length: source.split("\n").length },
      (_, index) => index + 1
    ).join("\n");
    panel.scrollTop = 0;
  } catch (error) {
    code.textContent = `Source unavailable: ${error.message}`;
    lineNumbers.textContent = "";
  }
}

tabs.forEach((tab, index) => {
  tab.addEventListener("click", () => showSource(tab.dataset.source));
  tab.addEventListener("keydown", (event) => {
    if (event.key !== "ArrowLeft" && event.key !== "ArrowRight") return;
    event.preventDefault();
    const direction = event.key === "ArrowRight" ? 1 : -1;
    const next = tabs[(index + direction + tabs.length) % tabs.length];
    next.focus();
    showSource(next.dataset.source);
  });
});

copy?.addEventListener("click", async () => {
  await navigator.clipboard.writeText(code.textContent);
  copy.textContent = "Copied";
  setTimeout(() => { copy.textContent = "Copy"; }, 1200);
});

debugToggle?.addEventListener("click", () => {
  setDebugExpanded(!debugExpanded);
});

document.addEventListener("keydown", (event) => {
  const interactive = event.target instanceof Element &&
    event.target.closest("button, input, textarea");
  if (event.key.toLowerCase() !== "d" || interactive) return;
  setDebugExpanded(!debugExpanded);
});

setDebugExpanded(debugExpanded);
if (!params.has("embed")) showSource("c");
