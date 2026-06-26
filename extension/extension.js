const vscode = require("vscode");
const path = require("path");

const KEYWORD_DOCS = {
  yaad: {
    detail: "Mutable declaration",
    doc: "Declare a mutable variable. `yaad_karo` is an alias keyword."
  },
  yaad_karo: {
    detail: "Mutable declaration alias",
    doc: "Alias of `yaad` for mutable variable declarations."
  },
  vada: {
    detail: "Immutable declaration",
    doc: "Declare an immutable variable (const-style)."
  },
  dil: {
    detail: "Legacy mutable declaration",
    doc: "Legacy mutable keyword kept for editor detection/highlighting."
  },
  mood: {
    detail: "Legacy mutable declaration",
    doc: "Legacy mutable keyword kept for editor detection/highlighting."
  },
  junoon: {
    detail: "Legacy mutable declaration",
    doc: "Legacy mutable keyword kept for editor detection/highlighting."
  },
  ishq: {
    detail: "Legacy immutable declaration",
    doc: "Legacy const keyword kept for editor detection/highlighting."
  },
  bolo: {
    detail: "Print statement",
    doc: "Print a value or expression."
  },
  typing: {
    detail: "Typing indicator",
    doc: "Print typing indicator. `typing...` is also accepted."
  },
  babu_miss_you: {
    detail: "Legacy expressive print",
    doc: "Legacy expressive print keyword kept for editor detection/highlighting."
  },
  shayarana: {
    detail: "Legacy poetic print",
    doc: "Legacy poetic print keyword kept for editor detection/highlighting."
  },
  status: {
    detail: "Legacy status print",
    doc: "Legacy status print keyword kept for editor detection/highlighting."
  },
  last_seen: {
    detail: "Legacy last-seen print",
    doc: "Legacy print keyword kept for editor detection/highlighting."
  },
  seen: {
    detail: "Legacy seen print",
    doc: "Legacy print keyword kept for editor detection/highlighting."
  },
  auto_reply: {
    detail: "Legacy auto-reply print",
    doc: "Legacy print keyword kept for editor detection/highlighting."
  },
  gaana: {
    detail: "Legacy music print",
    doc: "Legacy print keyword kept for editor detection/highlighting."
  },
  agar: {
    detail: "If branch",
    doc: "Conditional branch starter."
  },
  bewafa: {
    detail: "If branch alias",
    doc: "Dramatic alias of `agar`."
  },
  ye_karo: {
    detail: "Then block keyword",
    doc: "Starts then/loop block after condition."
  },
  vo_karo: {
    detail: "Else block keyword",
    doc: "Starts else block."
  },
  jabtak: {
    detail: "While loop",
    doc: "Loop while condition is true."
  },
  intezaar: {
    detail: "While loop alias",
    doc: "Alias of `jabtak`."
  },
  dhadkan: {
    detail: "Function declaration",
    doc: "Declare a function."
  },
  ehsaas: {
    detail: "Return statement",
    doc: "Return value from inside `dhadkan`."
  },
  festival: {
    detail: "Event/festival block",
    doc: "Declare and run a named festival block."
  },
  love_byeee: {
    detail: "Builtin stop call",
    doc: "Stop execution after farewell output."
  },
  love_you_baby_byeee: {
    detail: "Builtin stop alias",
    doc: "Alias for `love_byeee()`."
  },
  dil_se_pucho: {
    detail: "Builtin input",
    doc: "Read one line from stdin and return it as string."
  },
  input: {
    detail: "Builtin input alias",
    doc: "Alias for `dil_se_pucho(prompt?)`."
  },
  kismat: {
    detail: "Builtin random",
    doc: "Return random integer in inclusive range `kismat(min, max)`."
  },
  lafz_len: {
    detail: "Builtin length",
    doc: "Return text length of a value."
  },
  abhi_time: {
    detail: "Builtin time",
    doc: "Return current unix timestamp in seconds."
  },
  thoda_ruko: {
    detail: "Builtin sleep",
    doc: "Pause execution for given milliseconds."
  },
  import: {
    detail: "Module import",
    doc: "Preprocessor import. Inlines another .love file relative to current file."
  },
  export: {
    detail: "Module export marker",
    doc: "Preprocessor export marker. Kept for readable module declarations."
  },
  lambai: {
    detail: "Builtin length",
    doc: "Length helper for string, list, and map values."
  },
  len: {
    detail: "Builtin length alias",
    doc: "Alias of `lambai(value)` for string, list, and map values."
  },
  type_of: {
    detail: "Builtin type helper",
    doc: "Returns value type: int, bool, string, list, map, or null."
  },
  kya_type: {
    detail: "Builtin type helper alias",
    doc: "Alias of `type_of(value)` to get runtime type text."
  },
  to_text: {
    detail: "Builtin conversion",
    doc: "Convert value to text/string form."
  },
  text_banao: {
    detail: "Builtin conversion alias",
    doc: "Alias of `to_text(value)` for string conversion."
  },
  to_int: {
    detail: "Builtin conversion",
    doc: "Convert value to integer using Lovelang runtime rules."
  },
  int_banao: {
    detail: "Builtin conversion alias",
    doc: "Alias of `to_int(value)` for integer conversion."
  },
  to_bool: {
    detail: "Builtin conversion",
    doc: "Convert value to boolean using truthy/falsey rules."
  },
  bool_banao: {
    detail: "Builtin conversion alias",
    doc: "Alias of `to_bool(value)` for boolean conversion."
  },
  list_nayi: {
    detail: "Builtin list",
    doc: "Create an empty list."
  },
  pyaar_list: {
    detail: "Builtin list alias",
    doc: "Alias of `list_nayi()` to create an empty list."
  },
  list_daal: {
    detail: "Builtin list append",
    doc: "Append value to list. Returns updated length."
  },
  pyaar_daal: {
    detail: "Builtin list append alias",
    doc: "Alias of `list_daal(list, value)` append helper."
  },
  list_nikaal: {
    detail: "Builtin list pop",
    doc: "Pop and return last item. Errors on empty list."
  },
  list_lao: {
    detail: "Builtin list get",
    doc: "Get item at index from list."
  },
  list_set: {
    detail: "Builtin list set",
    doc: "Set item at index in list."
  },
  map_naya: {
    detail: "Builtin map",
    doc: "Create an empty map."
  },
  raaz_map: {
    detail: "Builtin map alias",
    doc: "Alias of `map_naya()` to create an empty map."
  },
  map_set: {
    detail: "Builtin map set",
    doc: "Set map key/value pair."
  },
  map_get: {
    detail: "Builtin map get",
    doc: "Get map value by key; returns null if key missing."
  },
  map_has: {
    detail: "Builtin map has",
    doc: "Check whether key exists in map."
  },
  map_keys: {
    detail: "Builtin map keys",
    doc: "Return list of map keys."
  },
  lafz_trim: {
    detail: "Builtin string trim",
    doc: "Trim leading and trailing whitespace from text."
  },
  lafz_lower: {
    detail: "Builtin string lower",
    doc: "Return lowercase text."
  },
  lafz_upper: {
    detail: "Builtin string upper",
    doc: "Return uppercase text."
  },
  lafz_contains: {
    detail: "Builtin string contains",
    doc: "Check if text contains needle."
  },
  lafz_replace: {
    detail: "Builtin string replace",
    doc: "Replace all occurrences of substring in text."
  },
  lafz_split: {
    detail: "Builtin string split",
    doc: "Split text by separator into a list."
  },
  lafz_join: {
    detail: "Builtin string join",
    doc: "Join list items into text using separator."
  },
  dil_khol_ke_padho: {
    detail: "Builtin file read",
    doc: "Read full text from file path."
  },
  file_padho: {
    detail: "Builtin file read alias",
    doc: "Alias of `dil_khol_ke_padho(path)` file read helper."
  },
  ishq_likhdo: {
    detail: "Builtin file write",
    doc: "Write text to file path (overwrite)."
  },
  file_likho: {
    detail: "Builtin file write alias",
    doc: "Alias of `ishq_likhdo(path, text)` overwrite helper."
  },
  ishq_joddo: {
    detail: "Builtin file append",
    doc: "Append text to file path."
  },
  file_jodo: {
    detail: "Builtin file append alias",
    doc: "Alias of `ishq_joddo(path, text)` append helper."
  },
  raasta_hai_kya: {
    detail: "Builtin file exists",
    doc: "Return whether file path exists."
  },
  file_hai_kya: {
    detail: "Builtin file exists alias",
    doc: "Alias of `raasta_hai_kya(path)` path-exists helper."
  },
  sach: {
    detail: "Boolean true",
    doc: "Boolean true value."
  },
  jhooth: {
    detail: "Boolean false",
    doc: "Boolean false value."
  },
  aur: {
    detail: "Logical AND",
    doc: "Word operator for logical AND."
  },
  ya: {
    detail: "Logical OR",
    doc: "Word operator for logical OR."
  },
  nahi: {
    detail: "Logical NOT",
    doc: "Word operator for logical NOT."
  },
  milan: {
    detail: "Arithmetic word operator",
    doc: "Word operator for `+`."
  },
  doori: {
    detail: "Arithmetic word operator",
    doc: "Word operator for `-`."
  },
  intense: {
    detail: "Arithmetic word operator",
    doc: "Word operator for `*`."
  },
  divide: {
    detail: "Arithmetic word operator",
    doc: "Word operator for `/`."
  }
};

const PHRASE_HOVERS = [
  {
    regex: /\bbaby\s+ignore\s+karo\b/gi,
    title: "baby ignore karo",
    doc: "Natural comment phrase. Anything after it on the line is ignored."
  },
  {
    regex: /\bbaby\s+bo+lo\s+na+a*\b/gi,
    title: "baby bolo na/naa",
    doc: "Natural phrase alias that maps to `bolo`."
  },
  {
    regex: /\bbaby\s+ya+a?d\s+rakho\b/gi,
    title: "baby yaad rakho",
    doc: "Natural phrase alias that maps to immutable declaration keyword."
  },
  {
    regex: /\byaad\s+karo\b/gi,
    title: "yaad karo",
    doc: "Natural declaration phrase that maps to `yaad_karo`."
  },
  {
    regex: /\bbabu\s+miss\s+you\b/gi,
    title: "babu miss you",
    doc: "Natural phrase alias for expressive print style in legacy syntax."
  },
  {
    regex: /\blast\s+seen\b/gi,
    title: "last seen",
    doc: "Natural phrase alias for `last_seen` in legacy syntax."
  },
  {
    regex: /\bauto\s+reply\b/gi,
    title: "auto reply",
    doc: "Natural phrase alias for `auto_reply` in legacy syntax."
  },
  {
    regex: /\bdil\s+se\s+pucho\b/gi,
    title: "dil se pucho",
    doc: "Natural input phrase alias for `dil_se_pucho(<prompt>)`."
  },
  {
    regex: /\bpucho\b/gi,
    title: "pucho",
    doc: "Natural input phrase alias for `dil_se_pucho(<prompt>)`."
  },
  {
    regex: /\bthoda\s+ruko\b/gi,
    title: "thoda ruko",
    doc: "Natural pause phrase alias for `thoda_ruko(<ms>)`."
  },
  {
    regex: /\bsun\s+na\s+agar\b/gi,
    title: "sun na agar",
    doc: "One-line conversational conditional form."
  },
  {
    regex: /\bwarna\s+agar\b/gi,
    title: "warna agar",
    doc: "Natural else-if chain form."
  },
  {
    regex: /\blove\s+you\s+baby\s+bye+e*\b/gi,
    title: "love you baby byeee",
    doc: "Natural stop phrase that maps to `love_byeee()`."
  }
];

const SETTINGS_FIELDS = [
  {
    key: "runUseScript",
    label: "Use run script",
    section: "Run",
    type: "boolean",
    defaultValue: true
  },
  {
    key: "runScriptPath",
    label: "Run script path",
    section: "Run",
    type: "string",
    defaultValue: ""
  },
  {
    key: "runBinaryPath",
    label: "Lovelang binary path",
    section: "Run",
    type: "string",
    defaultValue: "./lovelang"
  },
  {
    key: "runMode",
    label: "Default mode",
    section: "Run",
    type: "string",
    defaultValue: "romantic"
  },
  {
    key: "runTokens",
    label: "Include --tokens",
    section: "Run",
    type: "boolean",
    defaultValue: false
  },
  {
    key: "runDebugLove",
    label: "Include --debug-love",
    section: "Run",
    type: "boolean",
    defaultValue: false
  },
  {
    key: "runExtraArgs",
    label: "Extra CLI args (JSON array)",
    section: "Run",
    type: "array",
    defaultValue: []
  },
  {
    key: "runInNewTerminal",
    label: "Run in new terminal",
    section: "Run",
    type: "boolean",
    defaultValue: true
  },
  {
    key: "suggestions.includeHumanPhrases",
    label: "Include human phrase snippets",
    section: "Editor",
    type: "boolean",
    defaultValue: true
  },
  {
    key: "autoAssociateLoveFiles",
    label: "Auto associate .love files",
    section: "Editor",
    type: "boolean",
    defaultValue: true
  },
  {
    key: "forceDecorationHighlighting",
    label: "Force fallback highlighting",
    section: "Editor",
    type: "boolean",
    defaultValue: true
  },
  {
    key: "compileTarget",
    label: "Cross-compile Target",
    section: "Compile",
    type: "string",
    defaultValue: "host"
  },
  {
    key: "compileOutputPath",
    label: "Compile output path (empty = auto)",
    section: "Compile",
    type: "string",
    defaultValue: ""
  },
  {
    key: "compileRunAfter",
    label: "Run binary after compile",
    section: "Compile",
    type: "boolean",
    defaultValue: false
  },
  {
    key: "compileRunInNewTerminal",
    label: "Compile in new terminal each time",
    section: "Compile",
    type: "boolean",
    defaultValue: true
  }
];

const FUNCTION_CALL_EXCLUSIONS = new Set([
  "agar",
  "bewafa",
  "jabtak",
  "intezaar",
  "dhadkan",
  "festival",
  "love_byeee",
  "love_you_baby_byeee",
  "dil_se_pucho",
  "input",
  "kismat",
  "lafz_len",
  "lambai",
  "len",
  "type_of",
  "kya_type",
  "to_text",
  "text_banao",
  "to_int",
  "int_banao",
  "to_bool",
  "bool_banao",
  "list_nayi",
  "pyaar_list",
  "list_daal",
  "pyaar_daal",
  "list_nikaal",
  "list_lao",
  "list_set",
  "map_naya",
  "raaz_map",
  "map_set",
  "map_get",
  "map_has",
  "map_keys",
  "lafz_trim",
  "lafz_lower",
  "lafz_upper",
  "lafz_contains",
  "lafz_replace",
  "lafz_split",
  "lafz_join",
  "dil_khol_ke_padho",
  "file_padho",
  "ishq_likhdo",
  "file_likho",
  "ishq_joddo",
  "file_jodo",
  "raasta_hai_kya",
  "file_hai_kya",
  "abhi_time",
  "thoda_ruko"
]);

let gRunTerminal = null;

function cfg() {
  return vscode.workspace.getConfiguration("lovelang");
}

function setting(key, fallbackValue) {
  return cfg().get(key, fallbackValue);
}

function settingBool(key, fallbackValue) {
  return !!setting(key, fallbackValue);
}

function createNonce() {
  return String(Date.now()) + String(Math.random()).slice(2);
}

function formatShellArg(raw) {
  const value = String(raw ?? "");
  if (!value) {
    return process.platform === "win32" ? '""' : "''";
  }

  if (/^[A-Za-z0-9_./:-]+$/.test(value)) {
    return value;
  }

  if (process.platform === "win32") {
    return `"${value.replace(/"/g, '\\"')}"`;
  }

  return `'${value.replace(/'/g, `'"'"'`)}'`;
}

function isAbsolutePath(rawPath) {
  return path.isAbsolute(rawPath) || /^[A-Za-z]:[\\/]/.test(rawPath);
}

function getDefaultRunScriptPath() {
  if (process.platform === "win32") {
    return "./run_love.cmd";
  }
  return "./run_love.sh";
}

async function pathExists(workspaceFolderUri, rawPath) {
  const pathText = String(rawPath || "").trim();
  if (!pathText) {
    return false;
  }

  const uri = isAbsolutePath(pathText)
    ? vscode.Uri.file(pathText)
    : vscode.Uri.joinPath(workspaceFolderUri, pathText);

  try {
    await vscode.workspace.fs.stat(uri);
    return true;
  } catch {
    return false;
  }
}

async function resolveRunnerPath(workspaceFolderUri) {
  const useScript = settingBool("runUseScript", true);
  if (useScript) {
    const configuredScriptPath = String(setting("runScriptPath", "") || "").trim();
    const scriptPath = configuredScriptPath || getDefaultRunScriptPath();

    if (await pathExists(workspaceFolderUri, scriptPath)) {
      return scriptPath;
    }

    if (configuredScriptPath) {
      vscode.window.showWarningMessage(
        `Lovelang run script not found at ${configuredScriptPath}. Falling back to binary.`
      );
    }
  }

  return String(setting("runBinaryPath", "./lovelang") || "./lovelang").trim() || "./lovelang";
}

function normalizedExtraArgs() {
  const rawValue = setting("runExtraArgs", []);
  if (!Array.isArray(rawValue)) {
    return [];
  }

  return rawValue
    .map((item) => String(item ?? "").trim())
    .filter((item) => item.length > 0);
}

function createDecorationTypes() {
  return {
    control: vscode.window.createTextEditorDecorationType({ color: "#C586C0" }),
    declaration: vscode.window.createTextEditorDecorationType({ color: "#4EC9B0" }),
    output: vscode.window.createTextEditorDecorationType({ color: "#DCDCAA" }),
    builtin: vscode.window.createTextEditorDecorationType({ color: "#DCDCAA" }),
    functionName: vscode.window.createTextEditorDecorationType({ color: "#9CDCFE" }),
    eventName: vscode.window.createTextEditorDecorationType({ color: "#CE9178" }),
    operator: vscode.window.createTextEditorDecorationType({ color: "#569CD6" }),
    bool: vscode.window.createTextEditorDecorationType({ color: "#B5CEA8" }),
    humanPhrase: vscode.window.createTextEditorDecorationType({ color: "#C586C0" })
  };
}

function addRegexMatches(ranges, lineNumber, lineText, regex, groupIndex = 0, filter = null) {
  regex.lastIndex = 0;

  let match = regex.exec(lineText);
  while (match) {
    const raw = match[groupIndex] || match[0];
    if (raw && (!filter || filter(raw, match))) {
      const rel = groupIndex > 0 ? match[0].indexOf(raw) : 0;
      const start = match.index + Math.max(rel, 0);
      const end = start + raw.length;
      if (end > start) {
        ranges.push(new vscode.Range(lineNumber, start, lineNumber, end));
      }
    }

    if (regex.lastIndex === match.index) {
      regex.lastIndex++;
    }
    match = regex.exec(lineText);
  }
}

function clearAllDecorations(editor, decorationTypes) {
  for (const key of Object.keys(decorationTypes)) {
    editor.setDecorations(decorationTypes[key], []);
  }
}

function applyFallbackDecorations(editor, decorationTypes) {
  if (!editor) {
    return;
  }

  const enabled = vscode.workspace
    .getConfiguration("lovelang")
    .get("forceDecorationHighlighting", true);

  const document = editor.document;
  const eligible = document.languageId === "lovelang" || isLoveFile(document);
  if (!enabled || !eligible) {
    clearAllDecorations(editor, decorationTypes);
    return;
  }

  const ranges = {
    control: [],
    declaration: [],
    output: [],
    builtin: [],
    functionName: [],
    eventName: [],
    operator: [],
    bool: [],
    humanPhrase: []
  };

  for (let i = 0; i < document.lineCount; i++) {
    const lineText = document.lineAt(i).text;

    if (/^\s*(\/\/|#|~~)/.test(lineText) || /\bbaby\s+ignore\s+karo\b/i.test(lineText)) {
      continue;
    }

    addRegexMatches(ranges.declaration, i, lineText, /\b(?:yaad|yaad_karo|vada|dil|mood|junoon|ishq|baby_yad_rakho|baby_yaad_rakho)\b/gi);
    addRegexMatches(ranges.output, i, lineText, /\b(?:bolo|typing|baby_bolo_na|baby_bolo_naa|babu_miss_you|shayarana|status|last_seen|seen|auto_reply|gaana)\b/gi);
    addRegexMatches(
      ranges.builtin,
      i,
      lineText,
      /\b(?:love_byeee|love_you_baby_byeee|dil_se_pucho|input|kismat|lafz_len|lambai|len|type_of|kya_type|to_text|text_banao|to_int|int_banao|to_bool|bool_banao|list_nayi|pyaar_list|list_daal|pyaar_daal|list_nikaal|list_lao|list_set|map_naya|raaz_map|map_set|map_get|map_has|map_keys|lafz_trim|lafz_lower|lafz_upper|lafz_contains|lafz_replace|lafz_split|lafz_join|dil_khol_ke_padho|file_padho|ishq_likhdo|file_likho|ishq_joddo|file_jodo|raasta_hai_kya|file_hai_kya|abhi_time|thoda_ruko)\b/gi
    );
    addRegexMatches(ranges.bool, i, lineText, /\b(?:sach|jhooth)\b/gi);

    addRegexMatches(
      ranges.control,
      i,
      lineText,
      /\b(?:agar|bewafa|ye_karo|vo_karo|jabtak|intezaar|dhadkan|ehsaas|festival|import|export|toh|warna|hai)\b/gi
    );
    addRegexMatches(
      ranges.humanPhrase,
      i,
      lineText,
      /\b(?:sun\s+na\s+agar|warna\s+agar|tab\s+tak|bas\s+itna\s+hi|yaad\s+karo|baby\s+bo+lo\s+na+a*|baby\s+ya+a?d\s+rakho|dil\s+se\s+pucho|thoda\s+ruko|pucho|love\s+you\s+baby\s+bye+e*)\b/gi
    );

    addRegexMatches(
      ranges.operator,
      i,
      lineText,
      /\b(?:barabar\s+hai|same\s+hai|equal\s+hai|alag\s+hai|chhota\s+hai|bada\s+hai|chhota\s+ya\s+barabar\s+hai|bada\s+ya\s+barabar\s+hai|milan|doori|intense|divide|aur|ya|nahi)\b/gi
    );
    addRegexMatches(ranges.operator, i, lineText, /==|!=|<=|>=|&&|\|\||[=+\-*/%<>!]/g);

    addRegexMatches(ranges.functionName, i, lineText, /\bdhadkan\s+([A-Za-z_][A-Za-z0-9_]*)/gi, 1);
    addRegexMatches(ranges.eventName, i, lineText, /\bfestival\s+([A-Za-z_][A-Za-z0-9_]*)/gi, 1);

    addRegexMatches(
      ranges.functionName,
      i,
      lineText,
      /\b([A-Za-z_][A-Za-z0-9_]*)\s*(?=\()/g,
      1,
      (raw) => !FUNCTION_CALL_EXCLUSIONS.has(raw.toLowerCase())
    );
  }

  editor.setDecorations(decorationTypes.control, ranges.control);
  editor.setDecorations(decorationTypes.declaration, ranges.declaration);
  editor.setDecorations(decorationTypes.output, ranges.output);
  editor.setDecorations(decorationTypes.builtin, ranges.builtin);
  editor.setDecorations(decorationTypes.functionName, ranges.functionName);
  editor.setDecorations(decorationTypes.eventName, ranges.eventName);
  editor.setDecorations(decorationTypes.operator, ranges.operator);
  editor.setDecorations(decorationTypes.bool, ranges.bool);
  editor.setDecorations(decorationTypes.humanPhrase, ranges.humanPhrase);
}

function registerDecorationHighlighting(context) {
  const decorationTypes = createDecorationTypes();
  context.subscriptions.push(...Object.values(decorationTypes));

  const refreshEditor = (editor) => {
    if (editor) {
      applyFallbackDecorations(editor, decorationTypes);
    }
  };

  const refreshActive = () => {
    refreshEditor(vscode.window.activeTextEditor);
  };

  context.subscriptions.push(
    vscode.window.onDidChangeActiveTextEditor((editor) => refreshEditor(editor)),
    vscode.workspace.onDidOpenTextDocument((document) => {
      const editor = vscode.window.visibleTextEditors.find((e) => e.document === document);
      if (editor) {
        refreshEditor(editor);
      }
    }),
    vscode.workspace.onDidChangeTextDocument((event) => {
      const editor = vscode.window.visibleTextEditors.find((e) => e.document === event.document);
      if (editor) {
        refreshEditor(editor);
      }
    }),
    vscode.workspace.onDidChangeConfiguration((event) => {
      if (event.affectsConfiguration("lovelang.forceDecorationHighlighting")) {
        refreshActive();
      }
    })
  );

  for (const editor of vscode.window.visibleTextEditors) {
    refreshEditor(editor);
  }
}

function mkKeywordItem(label, detail, snippet, doc) {
  const item = new vscode.CompletionItem(label, vscode.CompletionItemKind.Keyword);
  item.insertText = new vscode.SnippetString(snippet);
  item.detail = detail;
  item.documentation = new vscode.MarkdownString(doc);
  return item;
}

function mkSnippetItem(label, snippet, detail, doc) {
  const item = new vscode.CompletionItem(label, vscode.CompletionItemKind.Snippet);
  item.insertText = new vscode.SnippetString(snippet);
  item.detail = detail;
  item.documentation = new vscode.MarkdownString(doc);
  return item;
}

function keywordCompletionItems() {
  return [
    mkKeywordItem("yaad", "Mutable declaration", "yaad ${1:name} = ${2:value};", "Declare mutable variable."),
    mkKeywordItem("yaad_karo", "Mutable declaration alias", "yaad_karo ${1:name} = ${2:value};", "Alias of `yaad`."),
    mkKeywordItem("vada", "Immutable declaration", "vada ${1:name} = ${2:value};", "Const-style declaration."),
    mkKeywordItem("dil", "Legacy mutable declaration", "dil ${1:name} = ${2:value};", "Legacy syntax keyword."),
    mkKeywordItem("mood", "Legacy mutable declaration", "mood ${1:name} = ${2:value};", "Legacy syntax keyword."),
    mkKeywordItem("junoon", "Legacy mutable declaration", "junoon ${1:name} = ${2:value};", "Legacy syntax keyword."),
    mkKeywordItem("ishq", "Legacy immutable declaration", "ishq ${1:name} = ${2:value};", "Legacy syntax keyword."),
    mkKeywordItem("bolo", "Print", "bolo ${1:\"hello\"};", "Print output."),
    mkKeywordItem("typing", "Typing indicator", "typing...", "Typing indicator line."),
    mkKeywordItem("babu_miss_you", "Legacy expressive print", "babu_miss_you ${1:name};", "Legacy expressive print."),
    mkKeywordItem("shayarana", "Legacy poetic print", "shayarana ${1:\"line\"};", "Legacy poetic print."),
    mkKeywordItem("status", "Legacy status print", "status ${1:\"online\"};", "Legacy status print."),
    mkKeywordItem("last_seen", "Legacy last-seen print", "last_seen ${1:\"2 min ago\"};", "Legacy last-seen print."),
    mkKeywordItem("seen", "Legacy seen print", "seen ${1:name};", "Legacy seen print."),
    mkKeywordItem("auto_reply", "Legacy auto-reply print", "auto_reply ${1:\"brb\"};", "Legacy auto-reply print."),
    mkKeywordItem("gaana", "Legacy music print", "gaana ${1:\"track.mp3\"};", "Legacy gaana print."),
    mkKeywordItem("agar", "If branch", "agar (${1:condition}) ye_karo {\n  ${2:bolo \"then\";}\n} vo_karo {\n  ${3:bolo \"else\";}\n}", "Conditional branch."),
    mkKeywordItem("bewafa", "If branch alias", "bewafa (${1:condition}) ye_karo {\n  ${2:bolo \"drama\";}\n}", "Alias of `agar`."),
    mkKeywordItem("ye_karo", "Then block keyword", "ye_karo", "Starts then/loop block."),
    mkKeywordItem("vo_karo", "Else block keyword", "vo_karo", "Starts else block."),
    mkKeywordItem("jabtak", "While loop", "jabtak (${1:condition}) ye_karo {\n  ${2:bolo \"loop\";}\n}", "Loop while condition is true."),
    mkKeywordItem("intezaar", "While loop alias", "intezaar (${1:condition}) ye_karo {\n  ${2:bolo \"wait\";}\n}", "Alias of `jabtak`."),
    mkKeywordItem("dhadkan", "Function declaration", "dhadkan ${1:name}(${2:arg}) {\n  ${3:ehsaas sach;}\n}", "Function declaration."),
    mkKeywordItem("ehsaas", "Return", "ehsaas ${1:value};", "Return from function."),
    mkKeywordItem("festival", "Festival event block", "festival ${1:diwali} {\n  ${2:bolo \"vibes\";}\n}", "Named festival block."),
    mkKeywordItem("import", "Module import", "import \"${1:modules/file.love}\"", "Preprocessor import for module files."),
    mkKeywordItem("export", "Module export marker", "export ${1:dhadkan helper() {\n  ehsaas sach\n}}", "Preprocessor export marker for reusable module declarations."),
    mkKeywordItem("love_byeee", "Builtin stop", "love_byeee();", "Stop execution with farewell."),
    mkKeywordItem("love_you_baby_byeee", "Builtin stop alias", "love_you_baby_byeee();", "Alias stop call."),
    mkKeywordItem("dil_se_pucho", "Builtin input", "dil_se_pucho(${1:\"> \"});", "Read one line from stdin."),
    mkKeywordItem("input", "Builtin input alias", "input(${1:\"> \"});", "Alias for `dil_se_pucho`."),
    mkKeywordItem("kismat", "Builtin random", "kismat(${1:1}, ${2:100})", "Inclusive random integer between min and max."),
    mkKeywordItem("lafz_len", "Builtin length", "lafz_len(${1:value})", "Length of value converted to text."),
    mkKeywordItem("lambai", "Builtin length", "lambai(${1:value})", "Length helper for string/list/map."),
    mkKeywordItem("len", "Builtin length alias", "len(${1:value})", "Alias of `lambai(value)`."),
    mkKeywordItem("type_of", "Builtin type helper", "type_of(${1:value})", "Return runtime type text."),
    mkKeywordItem("kya_type", "Builtin type helper alias", "kya_type(${1:value})", "Alias of `type_of(value)`."),
    mkKeywordItem("to_text", "Builtin conversion", "to_text(${1:value})", "Convert value to text."),
    mkKeywordItem("text_banao", "Builtin conversion alias", "text_banao(${1:value})", "Alias of `to_text(value)`."),
    mkKeywordItem("to_int", "Builtin conversion", "to_int(${1:value})", "Convert value to integer."),
    mkKeywordItem("int_banao", "Builtin conversion alias", "int_banao(${1:value})", "Alias of `to_int(value)`."),
    mkKeywordItem("to_bool", "Builtin conversion", "to_bool(${1:value})", "Convert value to boolean."),
    mkKeywordItem("bool_banao", "Builtin conversion alias", "bool_banao(${1:value})", "Alias of `to_bool(value)`."),
    mkKeywordItem("list_nayi", "Builtin list", "list_nayi()", "Create an empty list."),
    mkKeywordItem("pyaar_list", "Builtin list alias", "pyaar_list()", "Alias of `list_nayi()`."),
    mkKeywordItem("list_daal", "Builtin list append", "list_daal(${1:list}, ${2:value})", "Append value to list."),
    mkKeywordItem("pyaar_daal", "Builtin list append alias", "pyaar_daal(${1:list}, ${2:value})", "Alias of `list_daal`."),
    mkKeywordItem("list_nikaal", "Builtin list pop", "list_nikaal(${1:list})", "Pop last item from list."),
    mkKeywordItem("list_lao", "Builtin list get", "list_lao(${1:list}, ${2:index})", "Get item from list by index."),
    mkKeywordItem("list_set", "Builtin list set", "list_set(${1:list}, ${2:index}, ${3:value})", "Set list item at index."),
    mkKeywordItem("map_naya", "Builtin map", "map_naya()", "Create an empty map."),
    mkKeywordItem("raaz_map", "Builtin map alias", "raaz_map()", "Alias of `map_naya()`."),
    mkKeywordItem("map_set", "Builtin map set", "map_set(${1:map}, ${2:\"key\"}, ${3:value})", "Set key/value in map."),
    mkKeywordItem("map_get", "Builtin map get", "map_get(${1:map}, ${2:\"key\"})", "Get value by key from map."),
    mkKeywordItem("map_has", "Builtin map has", "map_has(${1:map}, ${2:\"key\"})", "Check map key existence."),
    mkKeywordItem("map_keys", "Builtin map keys", "map_keys(${1:map})", "Get list of map keys."),
    mkKeywordItem("lafz_trim", "Builtin string trim", "lafz_trim(${1:text})", "Trim whitespace from text."),
    mkKeywordItem("lafz_lower", "Builtin string lower", "lafz_lower(${1:text})", "Lowercase conversion."),
    mkKeywordItem("lafz_upper", "Builtin string upper", "lafz_upper(${1:text})", "Uppercase conversion."),
    mkKeywordItem("lafz_contains", "Builtin string contains", "lafz_contains(${1:text}, ${2:needle})", "Check if text contains needle."),
    mkKeywordItem("lafz_replace", "Builtin string replace", "lafz_replace(${1:text}, ${2:from}, ${3:to})", "Replace substring occurrences."),
    mkKeywordItem("lafz_split", "Builtin string split", "lafz_split(${1:text}, ${2:sep})", "Split text into list."),
    mkKeywordItem("lafz_join", "Builtin string join", "lafz_join(${1:list}, ${2:sep})", "Join list into text."),
    mkKeywordItem("dil_khol_ke_padho", "Builtin file read", "dil_khol_ke_padho(${1:path})", "Read full text file content."),
    mkKeywordItem("file_padho", "Builtin file read alias", "file_padho(${1:path})", "Alias of `dil_khol_ke_padho(path)`."),
    mkKeywordItem("ishq_likhdo", "Builtin file write", "ishq_likhdo(${1:path}, ${2:text})", "Write/overwrite file text."),
    mkKeywordItem("file_likho", "Builtin file write alias", "file_likho(${1:path}, ${2:text})", "Alias of `ishq_likhdo(path, text)`."),
    mkKeywordItem("ishq_joddo", "Builtin file append", "ishq_joddo(${1:path}, ${2:text})", "Append text to file."),
    mkKeywordItem("file_jodo", "Builtin file append alias", "file_jodo(${1:path}, ${2:text})", "Alias of `ishq_joddo(path, text)`."),
    mkKeywordItem("raasta_hai_kya", "Builtin file exists", "raasta_hai_kya(${1:path})", "Check if file path exists."),
    mkKeywordItem("file_hai_kya", "Builtin file exists alias", "file_hai_kya(${1:path})", "Alias of `raasta_hai_kya(path)`."),
    mkKeywordItem("abhi_time", "Builtin time", "abhi_time()", "Current unix timestamp in seconds."),
    mkKeywordItem("thoda_ruko", "Builtin sleep", "thoda_ruko(${1:500})", "Pause execution for milliseconds."),
    mkKeywordItem("sach", "Boolean", "sach", "Boolean true."),
    mkKeywordItem("jhooth", "Boolean", "jhooth", "Boolean false."),
    mkKeywordItem("aur", "Logical operator", "aur", "Logical AND."),
    mkKeywordItem("ya", "Logical operator", "ya", "Logical OR."),
    mkKeywordItem("nahi", "Logical operator", "nahi", "Logical NOT."),
    mkKeywordItem("milan", "Word operator", "milan", "Word operator for `+`."),
    mkKeywordItem("doori", "Word operator", "doori", "Word operator for `-`."),
    mkKeywordItem("intense", "Word operator", "intense", "Word operator for `*`."),
    mkKeywordItem("divide", "Word operator", "divide", "Word operator for `/`.")
  ];
}

function humanPhraseCompletionItems() {
  return [
    mkSnippetItem(
      "baby ignore karo",
      "baby ignore karo ${1:explain this section}",
      "Natural comment phrase",
      "Line comment alias for documentation-style comments."
    ),
    mkSnippetItem(
      "~~ comment",
      "~~ ${1:quick note}",
      "Symbol comment",
      "Symbol-based line comment style."
    ),
    mkSnippetItem(
      "baby bolo na",
      "baby bolo na ${1:\"i love you\"}",
      "Natural phrase alias",
      "Maps to `bolo` print statement."
    ),
    mkSnippetItem(
      "baby bolo naa",
      "baby bolo naa ${1:\"i love you\"}",
      "Natural phrase alias",
      "Maps to `bolo` print statement."
    ),
    mkSnippetItem(
      "baby yaad rakho",
      "baby yaad rakho ${1:piya} hai ${2:\"forever\"}",
      "Natural phrase alias",
      "Maps to immutable declaration keyword."
    ),
    mkSnippetItem(
      "yaad karo",
      "yaad karo ${1:name} hai ${2:\"remember\"}",
      "Natural declaration phrase",
      "Maps to `yaad_karo`."
    ),
    mkSnippetItem(
      "babu miss you",
      "babu miss you ${1:jaan}",
      "Legacy natural phrase",
      "Legacy expressive print alias."
    ),
    mkSnippetItem(
      "last seen",
      "last seen ${1:\"2 min ago\"}",
      "Legacy natural phrase",
      "Legacy alias for `last_seen`."
    ),
    mkSnippetItem(
      "auto reply",
      "auto reply ${1:\"brb\"}",
      "Legacy natural phrase",
      "Legacy alias for `auto_reply`."
    ),
    mkSnippetItem(
      "pucho",
      "pucho ${1:\"> \"}",
      "Natural input phrase",
      "Maps to `dil_se_pucho(...)`."
    ),
    mkSnippetItem(
      "dil se pucho",
      "dil se pucho ${1:\"> \"}",
      "Natural input phrase",
      "Maps to `dil_se_pucho(...)`."
    ),
    mkSnippetItem(
      "thoda ruko",
      "thoda ruko ${1:500}",
      "Natural pause phrase",
      "Maps to `thoda_ruko(...)`."
    ),
    mkSnippetItem(
      "sun na agar",
      "sun na agar ${1:state equal hai \"romantic\"} toh ${2:bolo \"i love you\"} warna ${3:bolo \"so ja\"}",
      "Conversational one-liner",
      "Expands to full if/else block in preprocessor."
    ),
    mkSnippetItem(
      "agar ... toh ... warna ... bas itna hi",
      "agar ${1:state same hai \"ignore\"} toh\n  ${2:bolo \"reply nahi aaya\"}\nwarna\n  ${3:bolo \"reply aa gaya\"}\nbas itna hi",
      "Natural if/else",
      "Human-mode if/else block."
    ),
    mkSnippetItem(
      "warna agar chain",
      "agar ${1:a barabar hai \"x\"} toh\n  ${2:bolo \"x\"}\nwarna agar ${3:a equal hai \"y\"} toh\n  ${4:bolo \"y\"}\nwarna\n  ${5:bolo \"z\"}\nbas itna hi",
      "Natural else-if chain",
      "Chain with `warna agar ... toh`."
    ),
    mkSnippetItem(
      "intezaar ... tab tak",
      "intezaar ${1:i chhota hai 3} tab tak\n  ${2:bolo \"wait\"}\n  ${3:i hai i milan 1}\nbas itna hi",
      "Natural loop",
      "Human-mode loop alias."
    ),
    mkSnippetItem(
      "love you baby byeee",
      "love you baby byeee",
      "Natural stop phrase",
      "Maps to `love_byeee()` and stops further execution."
    )
  ];
}

function providePhraseHover(lineText, charIndex) {
  for (const phrase of PHRASE_HOVERS) {
    phrase.regex.lastIndex = 0;
    let match = phrase.regex.exec(lineText);
    while (match) {
      const start = match.index;
      const end = start + match[0].length;
      if (charIndex >= start && charIndex <= end) {
        return {
          title: phrase.title,
          doc: phrase.doc,
          start,
          end
        };
      }
      match = phrase.regex.exec(lineText);
    }
  }
  return null;
}

function isLoveFile(document) {
  if (!document) {
    return false;
  }

  if (document.uri.scheme !== "file" && document.uri.scheme !== "untitled") {
    return false;
  }

  return document.fileName.toLowerCase().endsWith(".love");
}

async function ensureLovelangMode(document) {
  const autoAssociate = vscode.workspace
    .getConfiguration("lovelang")
    .get("autoAssociateLoveFiles", true);

  if (!autoAssociate || !isLoveFile(document) || document.languageId === "lovelang") {
    return;
  }

  try {
    await vscode.languages.setTextDocumentLanguage(document, "lovelang");
  } catch {
    // Ignore failures quietly; this is a best-effort UX enhancement.
  }
}

function registerAutoAssociation(context) {
  const openSubscription = vscode.workspace.onDidOpenTextDocument((document) => {
    void ensureLovelangMode(document);
  });

  const activeEditorSubscription = vscode.window.onDidChangeActiveTextEditor((editor) => {
    if (editor && editor.document) {
      void ensureLovelangMode(editor.document);
    }
  });

  context.subscriptions.push(openSubscription, activeEditorSubscription);

  for (const document of vscode.workspace.textDocuments) {
    void ensureLovelangMode(document);
  }
}

function readSettingsState() {
  const state = {};
  for (const field of SETTINGS_FIELDS) {
    state[field.key] = setting(field.key, field.defaultValue);
  }
  return state;
}

async function saveSettingsState(state) {
  const configuration = cfg();
  const target = vscode.workspace.workspaceFolders?.length
    ? vscode.ConfigurationTarget.Workspace
    : vscode.ConfigurationTarget.Global;

  for (const field of SETTINGS_FIELDS) {
    if (!(field.key in state)) {
      continue;
    }

    let value = state[field.key];
    if (field.type === "boolean") {
      value = !!value;
    } else if (field.type === "number") {
      const asNumber = Number(value);
      if (!Number.isFinite(asNumber)) {
        continue;
      }
      value = asNumber;
    } else if (field.type === "array") {
      if (Array.isArray(value)) {
        value = value.map((item) => String(item));
      } else if (typeof value === "string") {
        try {
          const parsed = JSON.parse(value);
          value = Array.isArray(parsed) ? parsed.map((item) => String(item)) : [];
        } catch {
          value = [];
        }
      } else {
        value = [];
      }
    } else {
      value = String(value ?? "");
    }

    await configuration.update(field.key, value, target);
  }
}

async function openExtensionReadme(context) {
  const uri = vscode.Uri.joinPath(context.extensionUri, "README.md");
  const doc = await vscode.workspace.openTextDocument(uri);
  await vscode.window.showTextDocument(doc, { preview: false });
}

async function runCurrentLoveFile() {
  const editor = vscode.window.activeTextEditor;
  if (!editor || !(isLoveFile(editor.document) || editor.document.languageId === "lovelang")) {
    vscode.window.showWarningMessage("Open a .love file first.");
    return;
  }

  const workspaceFolder =
    vscode.workspace.getWorkspaceFolder(editor.document.uri) || vscode.workspace.workspaceFolders?.[0];
  if (!workspaceFolder) {
    vscode.window.showErrorMessage("No workspace folder found.");
    return;
  }

  const relativePath = vscode.workspace.asRelativePath(editor.document.uri, false);
  const runnerPath = await resolveRunnerPath(workspaceFolder.uri);

  const parts = [formatShellArg(runnerPath), formatShellArg(relativePath)];

  const mode = String(setting("runMode", "romantic") || "").trim();
  if (mode.length > 0) {
    parts.push("--mode", formatShellArg(mode));
  }

  if (settingBool("runTokens", false)) {
    parts.push("--tokens");
  }

  if (settingBool("runDebugLove", false)) {
    parts.push("--debug-love");
  }

  for (const arg of normalizedExtraArgs()) {
    parts.push(formatShellArg(arg));
  }

  const terminalName = "Lovelang Run";
  const useNewTerminal = settingBool("runInNewTerminal", true) || !gRunTerminal;
  if (useNewTerminal) {
    gRunTerminal = vscode.window.createTerminal({ name: terminalName });
  }

  gRunTerminal.show(true);
  gRunTerminal.sendText(parts.join(" "), true);
}

/** Tracks the last compiled binary path so we can re-run it */
let gLastBinary = "";
/** Dedicated terminal for compile output (separate from run terminal) */
let gCompileTerminal = null;

/**
 * Derive the output binary path from the source file path.
 * e.g. "examples/01-romantic-hello.love" → "build/01-romantic-hello"
 */
function deriveOutputPath(relativePath) {
  const base = path.basename(relativePath, ".love");
  return `build/${base}`;
}

async function compileCurrentLoveFile({ andRun = false } = {}) {
  const editor = vscode.window.activeTextEditor;
  if (!editor || !(isLoveFile(editor.document) || editor.document.languageId === "lovelang")) {
    vscode.window.showWarningMessage("Open a .love file first.");
    return;
  }

  const workspaceFolder =
    vscode.workspace.getWorkspaceFolder(editor.document.uri) || vscode.workspace.workspaceFolders?.[0];
  if (!workspaceFolder) {
    vscode.window.showErrorMessage("No workspace folder found.");
    return;
  }

  const relativePath = vscode.workspace.asRelativePath(editor.document.uri, false);
  const binaryPath = String(setting("runBinaryPath", "./lovelang") || "./lovelang").trim() || "./lovelang";

  // Resolve output path — setting overrides auto-derive
  let outPath = String(setting("compileOutputPath", "") || "").trim();
  if (!outPath) {
    outPath = deriveOutputPath(relativePath);
  }
  gLastBinary = outPath;

  const parts = [formatShellArg(binaryPath), formatShellArg(relativePath), "--compile", "--out", formatShellArg(outPath)];

  const target = String(setting("compileTarget", "host") || "host").trim();
  let envPrefix = "";
  if (target !== "host" && (target === "macos" || target === "linux" || target === "windows")) {
    if (process.platform === "win32") {
      envPrefix = `set LOVELANG_TARGET=${target} && `;
    } else {
      envPrefix = `LOVELANG_TARGET=${target} `;
    }
  }

  const useNewTerminal = settingBool("compileRunInNewTerminal", true) || !gCompileTerminal;
  if (useNewTerminal) {
    gCompileTerminal = vscode.window.createTerminal({ name: "Lovelang Compile" });
  }
  gCompileTerminal.show(true);

  // Build the command string
  let cmd = envPrefix + parts.join(" ");

  // Optionally chain running the binary
  const shouldRun = andRun || settingBool("compileRunAfter", false);
  if (shouldRun) {
    const runCmd = process.platform === "win32" ? formatShellArg(outPath) : `./${outPath}`;
    cmd += ` && echo '' && echo '─── running binary ───' && ${runCmd}`;
  }

  gCompileTerminal.sendText(cmd, true);
}

async function compileAndRunCurrentLoveFile() {
  await compileCurrentLoveFile({ andRun: true });
}

async function runLastBinary() {
  if (!gLastBinary) {
    vscode.window.showWarningMessage("No compiled binary yet. Use Compile first.");
    return;
  }
  const runCmd = process.platform === "win32" ? formatShellArg(gLastBinary) : `./${gLastBinary}`;
  const useNewTerminal = settingBool("compileRunInNewTerminal", true) || !gCompileTerminal;
  if (useNewTerminal) {
    gCompileTerminal = vscode.window.createTerminal({ name: "Lovelang Run Binary" });
  }
  gCompileTerminal.show(true);
  gCompileTerminal.sendText(runCmd, true);
}

function openSettingsPanel(context) {
  const panel = vscode.window.createWebviewPanel(
    "loveSettings",
    "Lovelang Settings",
    vscode.ViewColumn.Active,
    { enableScripts: true }
  );

  panel.webview.html = getSettingsWebviewHtml(panel.webview);

  panel.webview.onDidReceiveMessage(async (message) => {
    if (message.type === "load") {
      panel.webview.postMessage({ type: "state", state: readSettingsState() });
      return;
    }

    if (message.type === "save") {
      await saveSettingsState(message.state || {});
      vscode.window.showInformationMessage("Lovelang settings saved.");
      panel.webview.postMessage({ type: "state", state: readSettingsState() });
      return;
    }

    if (message.type === "run") {
      await runCurrentLoveFile();
      return;
    }

    if (message.type === "compile") {
      await compileCurrentLoveFile();
      return;
    }

    if (message.type === "compileAndRun") {
      await compileAndRunCurrentLoveFile();
      return;
    }

    if (message.type === "runBinary") {
      await runLastBinary();
      return;
    }

    if (message.type === "openReadme") {
      await openExtensionReadme(context);
    }
  }, null, context.subscriptions);
}

function getSettingsWebviewHtml(webview) {
  const nonce = createNonce();
  const serializedFields = JSON.stringify(SETTINGS_FIELDS);

  return `<!doctype html>
<html lang="en">
<head>
  <meta charset="UTF-8" />
  <meta http-equiv="Content-Security-Policy" content="default-src 'none'; style-src ${webview.cspSource} 'unsafe-inline'; script-src 'nonce-${nonce}';" />
  <meta name="viewport" content="width=device-width, initial-scale=1.0" />
  <title>Lovelang Settings</title>
  <style>
    :root {
      --accent: #f97316;
      --accent-soft: color-mix(in srgb, var(--accent) 18%, transparent);
      --line: color-mix(in srgb, var(--vscode-foreground) 16%, transparent);
      --panel: color-mix(in srgb, var(--vscode-editor-background) 92%, white 8%);
      --radius: 10px;
      --radius-sm: 8px;
    }

    * { box-sizing: border-box; }

    body {
      margin: 0;
      font-family: ui-sans-serif, -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif;
      color: var(--vscode-foreground);
      background:
        radial-gradient(circle at 16% 0%, color-mix(in srgb, var(--accent) 13%, transparent), transparent 42%),
        radial-gradient(circle at 84% 100%, color-mix(in srgb, #22d3ee 10%, transparent), transparent 45%),
        var(--vscode-editor-background);
      min-height: 100vh;
      display: flex;
      flex-direction: column;
    }

    .hero {
      padding: 18px 24px;
      border-bottom: 1px solid var(--line);
      display: flex;
      align-items: center;
      gap: 12px;
    }

    .dot {
      width: 12px;
      height: 12px;
      border-radius: 999px;
      background: var(--accent);
      box-shadow: 0 0 0 6px var(--accent-soft);
      flex: 0 0 auto;
    }

    .hero h1 {
      margin: 0;
      font-size: 16px;
      font-weight: 700;
      letter-spacing: 0.1px;
    }

    .hero p {
      margin: 4px 0 0;
      font-size: 12px;
      opacity: 0.82;
    }

    .main {
      padding: 16px 20px 8px;
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(340px, 1fr));
      gap: 12px;
      flex: 1;
    }

    .section {
      border: 1px solid var(--line);
      border-radius: var(--radius);
      background: var(--panel);
      overflow: hidden;
    }

    .section-head {
      padding: 10px 12px;
      border-bottom: 1px solid var(--line);
      font-size: 11px;
      text-transform: uppercase;
      letter-spacing: 0.45px;
      opacity: 0.8;
      font-weight: 700;
    }

    .section-body {
      padding: 10px 12px 12px;
      display: flex;
      flex-direction: column;
      gap: 10px;
    }

    .field {
      display: flex;
      flex-direction: column;
      gap: 5px;
    }

    .field.inline {
      flex-direction: row;
      align-items: center;
      justify-content: space-between;
      gap: 12px;
    }

    .field label {
      font-size: 12px;
      font-weight: 600;
    }

    .field input[type='text'],
    .field input[type='number'] {
      width: 100%;
      padding: 7px 9px;
      border-radius: var(--radius-sm);
      border: 1px solid var(--vscode-input-border, #555);
      background: var(--vscode-input-background);
      color: var(--vscode-input-foreground);
      font-size: 12px;
    }

    .field input[type='checkbox'] {
      width: 16px;
      height: 16px;
    }

    .footer {
      border-top: 1px solid var(--line);
      padding: 12px 20px;
      display: flex;
      flex-wrap: wrap;
      gap: 8px;
      align-items: center;
    }

    button {
      border-radius: var(--radius-sm);
      padding: 7px 12px;
      font-size: 12px;
      font-weight: 700;
      cursor: pointer;
      border: 1px solid transparent;
      transition: opacity 0.15s ease;
    }

    button:hover { opacity: 0.88; }

    #save {
      background: var(--accent);
      color: #fff;
    }

    .secondary {
      background: color-mix(in srgb, var(--vscode-editor-background) 80%, white 20%);
      border-color: var(--line);
      color: var(--vscode-foreground);
    }

    .tip {
      margin-left: auto;
      font-size: 11px;
      opacity: 0.72;
    }

    .toast {
      position: fixed;
      bottom: 18px;
      right: 18px;
      border-radius: 999px;
      padding: 8px 13px;
      background: #10b981;
      color: #fff;
      font-size: 12px;
      font-weight: 700;
      opacity: 0;
      transform: translateY(8px);
      transition: opacity 0.2s ease, transform 0.2s ease;
      pointer-events: none;
    }

    .toast.show {
      opacity: 1;
      transform: translateY(0);
    }
  </style>
</head>
<body>
  <div class="hero">
    <div class="dot"></div>
    <div>
      <h1>Lovelang Settings</h1>
      <p>Run options, auto association, fallback highlighting, and IntelliSense behavior.</p>
    </div>
  </div>

  <div id="settings-grid" class="main"></div>

  <div class="footer">
    <button id="save">Save</button>
    <button id="reload" class="secondary">Reload</button>
    <button id="run" class="secondary">▶ Run</button>
    <button id="compile" class="secondary">⚙ Compile</button>
    <button id="compileAndRun" class="secondary">⚡ Compile &amp; Run</button>
    <button id="runBinary" class="secondary">▶▶ Run Binary</button>
    <button id="readme" class="secondary">README</button>
    <span class="tip">Tip: search <strong>lovelang.</strong> in VS Code Settings</span>
  </div>

  <div class="toast" id="toast">Saved</div>

  <script nonce="${nonce}">
    const vscode = acquireVsCodeApi();
    const fields = ${serializedFields};

    function renderFields() {
      const container = document.getElementById('settings-grid');
      container.innerHTML = '';

      const grouped = new Map();
      for (const field of fields) {
        if (!grouped.has(field.section)) grouped.set(field.section, []);
        grouped.get(field.section).push(field);
      }

      for (const [section, sectionFields] of grouped.entries()) {
        const sectionEl = document.createElement('div');
        sectionEl.className = 'section';

        const head = document.createElement('div');
        head.className = 'section-head';
        head.textContent = section;
        sectionEl.appendChild(head);

        const body = document.createElement('div');
        body.className = 'section-body';

        for (const field of sectionFields) {
          const row = document.createElement('div');
          row.className = field.type === 'boolean' ? 'field inline' : 'field';

          const label = document.createElement('label');
          label.htmlFor = field.key;
          label.textContent = field.label;

          let input;
          if (field.type === 'boolean') {
            input = document.createElement('input');
            input.type = 'checkbox';
            input.id = field.key;
            row.appendChild(label);
            row.appendChild(input);
          } else {
            input = document.createElement('input');
            input.type = field.type === 'number' ? 'number' : 'text';
            input.id = field.key;
            row.appendChild(label);
            row.appendChild(input);
          }

          body.appendChild(row);
        }

        sectionEl.appendChild(body);
        container.appendChild(sectionEl);
      }
    }

    function readFormState() {
      const state = {};
      for (const field of fields) {
        const input = document.getElementById(field.key);
        if (!input) continue;

        if (field.type === 'boolean') {
          state[field.key] = !!input.checked;
        } else if (field.type === 'number') {
          state[field.key] = Number(input.value || field.defaultValue || 0);
        } else if (field.type === 'array') {
          try {
            const parsed = JSON.parse(input.value || '[]');
            state[field.key] = Array.isArray(parsed) ? parsed : [];
          } catch {
            state[field.key] = [];
          }
        } else {
          state[field.key] = input.value;
        }
      }
      return state;
    }

    function applyState(state) {
      for (const field of fields) {
        const input = document.getElementById(field.key);
        if (!input) continue;

        const value = state && Object.prototype.hasOwnProperty.call(state, field.key)
          ? state[field.key]
          : field.defaultValue;

        if (field.type === 'boolean') {
          input.checked = !!value;
        } else if (field.type === 'array') {
          input.value = JSON.stringify(Array.isArray(value) ? value : []);
        } else {
          input.value = value == null ? '' : String(value);
        }
      }
    }

    function showToast(text) {
      const toast = document.getElementById('toast');
      toast.textContent = text;
      toast.classList.add('show');
      setTimeout(() => toast.classList.remove('show'), 1800);
    }

    window.addEventListener('message', (event) => {
      const data = event.data;
      if (!data || data.type !== 'state') return;
      applyState(data.state || {});
    });

    document.getElementById('save').addEventListener('click', () => {
      vscode.postMessage({ type: 'save', state: readFormState() });
      showToast('Saved');
    });

    document.getElementById('reload').addEventListener('click', () => {
      vscode.postMessage({ type: 'load' });
    });

    document.getElementById('run').addEventListener('click', () => {
      vscode.postMessage({ type: 'run' });
    });

    document.getElementById('compile').addEventListener('click', () => {
      vscode.postMessage({ type: 'compile' });
    });

    document.getElementById('compileAndRun').addEventListener('click', () => {
      vscode.postMessage({ type: 'compileAndRun' });
    });

    document.getElementById('runBinary').addEventListener('click', () => {
      vscode.postMessage({ type: 'runBinary' });
    });

    document.getElementById('readme').addEventListener('click', () => {
      vscode.postMessage({ type: 'openReadme' });
    });

    renderFields();
    vscode.postMessage({ type: 'load' });
  </script>
</body>
</html>`;
}

function activate(context) {
  registerAutoAssociation(context);
  registerDecorationHighlighting(context);

  context.subscriptions.push(
    vscode.commands.registerCommand("loveLanguage.runCurrentFile", () => {
      void runCurrentLoveFile();
    }),
    vscode.commands.registerCommand("loveLanguage.compileCurrentFile", () => {
      void compileCurrentLoveFile();
    }),
    vscode.commands.registerCommand("loveLanguage.compileAndRunCurrentFile", () => {
      void compileAndRunCurrentLoveFile();
    }),
    vscode.commands.registerCommand("loveLanguage.runLastBinary", () => {
      void runLastBinary();
    }),
    vscode.commands.registerCommand("loveLanguage.openSettingsPanel", () => {
      openSettingsPanel(context);
    }),
    vscode.commands.registerCommand("loveLanguage.openExtensionReadme", () => {
      void openExtensionReadme(context);
    })
  );

  const completionProvider = vscode.languages.registerCompletionItemProvider(
    { language: "lovelang" },
    {
      provideCompletionItems() {
        const includeHuman = vscode.workspace
          .getConfiguration("lovelang")
          .get("suggestions.includeHumanPhrases", true);

        return includeHuman
          ? [...keywordCompletionItems(), ...humanPhraseCompletionItems()]
          : keywordCompletionItems();
      }
    },
    "_",
    ".",
    " "
  );

  const hoverProvider = vscode.languages.registerHoverProvider(
    { language: "lovelang" },
    {
      provideHover(document, position) {
        const line = document.lineAt(position.line).text;
        const phrase = providePhraseHover(line, position.character);
        if (phrase) {
          const range = new vscode.Range(
            new vscode.Position(position.line, phrase.start),
            new vscode.Position(position.line, phrase.end)
          );
          const md = new vscode.MarkdownString(
            `**${phrase.title}**\n\n${phrase.doc}`
          );
          return new vscode.Hover(md, range);
        }

        const range = document.getWordRangeAtPosition(position, /[A-Za-z_]+/);
        if (!range) {
          return undefined;
        }

        const word = document.getText(range).toLowerCase();
        const info = KEYWORD_DOCS[word];
        if (!info) {
          return undefined;
        }

        const md = new vscode.MarkdownString(
          `**${word}** (${info.detail})\n\n${info.doc}`
        );
        return new vscode.Hover(md, range);
      }
    }
  );

  context.subscriptions.push(completionProvider, hoverProvider);
  console.log("Love Lang Tools activated with IntelliSense");
}

function deactivate() {}

module.exports = { activate, deactivate };