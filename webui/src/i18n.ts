import en from './locales/en.json';

export type LocaleMessages = Record<string, string>;

const messages: LocaleMessages = en as LocaleMessages;

/** Lookup a flattened catalog key (e.g. `web.ui.cancel`). Missing keys return the key. */
export function t(key: string): string {
  const v = messages[key];
  return v !== undefined ? v : key;
}

/** Replace `{placeholders}` in a catalog string. */
export function tf(key: string, vars: Record<string, string | number>): string {
  let s = t(key);
  for (const [k, val] of Object.entries(vars)) {
    s = s.split(`{${k}}`).join(String(val));
  }
  return s;
}
