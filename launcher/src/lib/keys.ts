/* Browser KeyboardEvent.code -> the names SDL_GetScancodeFromName accepts.
 *
 * The two vocabularies overlap but do not match: the browser says "KeyZ" and
 * "ArrowLeft" where SDL says "Z" and "Left". Getting this wrong produces a
 * binding that looks right in the UI and does nothing in the game, so the
 * conversion is explicit and anything it cannot convert is refused rather than
 * passed through hopefully. */
const EXACT: Record<string, string> = {
  Enter: "Return",
  NumpadEnter: "Keypad Enter",
  Escape: "Escape",
  Backspace: "Backspace",
  Tab: "Tab",
  Space: "Space",
  ArrowLeft: "Left",
  ArrowRight: "Right",
  ArrowUp: "Up",
  ArrowDown: "Down",
  ShiftLeft: "Left Shift",
  ShiftRight: "Right Shift",
  ControlLeft: "Left Ctrl",
  ControlRight: "Right Ctrl",
  AltLeft: "Left Alt",
  AltRight: "Right Alt",
  MetaLeft: "Left GUI",
  MetaRight: "Right GUI",
  CapsLock: "CapsLock",
  Insert: "Insert",
  Home: "Home",
  End: "End",
  PageUp: "PageUp",
  PageDown: "PageDown",
  Delete: "Delete",
  Minus: "-",
  Equal: "=",
  BracketLeft: "[",
  BracketRight: "]",
  Backslash: "\\",
  Semicolon: ";",
  Quote: "'",
  Backquote: "`",
  Comma: ",",
  Period: ".",
  Slash: "/",
  NumpadDivide: "Keypad /",
  NumpadMultiply: "Keypad *",
  NumpadSubtract: "Keypad -",
  NumpadAdd: "Keypad +",
  NumpadDecimal: "Keypad .",
  NumLock: "Numlock",
};

export function sdlKeyName(code: string): string | null {
  if (EXACT[code]) return EXACT[code];
  if (/^Key[A-Z]$/.test(code)) return code.slice(3);
  if (/^Digit[0-9]$/.test(code)) return code.slice(5);
  if (/^F([1-9]|1[0-9]|2[0-4])$/.test(code)) return code;
  if (/^Numpad[0-9]$/.test(code)) return `Keypad ${code.slice(6)}`;
  return null;
}

/* A comma-separated binding, shown the way a person reads it. */
export function prettyKeys(value: string): string {
  if (!value) return "—";
  return value
    .split(",")
    .map((k) => k.trim())
    .filter(Boolean)
    .join("  ·  ");
}
