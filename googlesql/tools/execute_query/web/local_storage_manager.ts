//
// Copyright 2019 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

/**
 * @fileoverview Manages values stored in local persistent storage, including
 * application settings.
 */

/**
 * Identifies a value stored in local persistent storage. Each key maps to its
 * own local storage entry (see `STORAGE_KEY_NAMES`); the format of each value
 * is owned by the code that uses it.
 */
export type StorageKey = 'settings' | 'history';

/** Local storage entry names for each `StorageKey`. */
const STORAGE_KEY_NAMES: Record<StorageKey, string> = {
  settings: 'execute-query-settings',
  history: 'execute-query-history',
};

/**
 * Application settings stored in local persistent storage.
 */
export declare interface AppSettings {
  [key: string]: string | undefined;
  theme?: string;
  split_left_width?: string;
  layout_mode?: string;
  default_options?: string;
}

/**
 * Manages values stored in local persistent storage. All access to local
 * storage from the web UI goes through this class, which handles unavailable
 * storage, malformed values, and storage errors.
 */
export class LocalStorageManager {
  /** Local storage entry name for application settings. */
  static readonly KEY = STORAGE_KEY_NAMES.settings;

  /**
   * Returns the parsed JSON value stored under `key`, or undefined if it is
   * missing, malformed, or storage is unavailable. Callers must validate the
   * shape of the result.
   */
  getJson(key: StorageKey): unknown {
    try {
      const item = globalThis.localStorage?.getItem(STORAGE_KEY_NAMES[key]);
      return item ? (JSON.parse(item) as unknown) : undefined;
    } catch {
      return undefined;
    }
  }

  /**
   * Stores `value` as JSON under `key`. Returns false if the write failed, e.g.
   * because the storage quota was exceeded or storage is disabled in
   * restricted browsing modes.
   */
  setJson(key: StorageKey, value: unknown): boolean {
    try {
      globalThis.localStorage?.setItem(
        STORAGE_KEY_NAMES[key],
        JSON.stringify(value),
      );
      return true;
    } catch {
      return false;
    }
  }

  /** Removes the value stored under `key`, if any. */
  remove(key: StorageKey): void {
    try {
      globalThis.localStorage?.removeItem(STORAGE_KEY_NAMES[key]);
    } catch {
      // Ignore security errors in restricted browsing modes.
    }
  }

  private getSettings(): AppSettings {
    const parsed = this.getJson('settings');
    return parsed && typeof parsed === 'object' ? (parsed as AppSettings) : {};
  }

  getSetting<K extends keyof AppSettings>(key: K): AppSettings[K] {
    return this.getSettings()[key];
  }

  updateSetting<K extends keyof AppSettings>(
    key: K,
    value: AppSettings[K],
  ): void {
    const settings = this.getSettings();
    settings[key] = value;
    // Storage errors are ignored; settings are best-effort.
    this.setJson('settings', settings);
  }
}

/**
 * Serializes form controls into a URL-encoded query string.
 */
export function serializeForm(
  form: HTMLFormElement,
  includeQuery = true,
): string {
  // URLSearchParams accepts FormData at runtime, though standard TypeScript DOM
  // typings omit FormData from the URLSearchParams constructor union.
  const params = new URLSearchParams(
    new FormData(form) as unknown as Record<string, string>,
  );
  if (!includeQuery) {
    params.delete('query');
  }
  return params.toString();
}

/**
 * Restores form controls from a URL-encoded query string.
 */
export function restoreForm(
  form: HTMLFormElement,
  serializedParams: string,
  includeQuery = true,
): void {
  const queryParams = new URLSearchParams(serializedParams);

  // Uncheck checkbox groups that are completely absent from queryParams, since
  // FormData omits unchecked checkboxes.
  for (const element of form.querySelectorAll<HTMLInputElement>(
    'input[type="checkbox"]',
  )) {
    if (element.name && !queryParams.has(element.name)) {
      element.checked = false;
    }
  }

  for (const param of new Set<string>(queryParams.keys())) {
    if (!includeQuery && param === 'query') continue;

    const elements = form.querySelectorAll(`[name='${param}']`);
    if (elements.length === 1) {
      const element = elements.item(0);
      const val = queryParams.get(param) ?? '';
      if (element instanceof HTMLTextAreaElement) {
        element.value = val;
        element.dispatchEvent(new Event('change', {bubbles: true}));
      } else if (element instanceof HTMLSelectElement) {
        if (
          Array.from(element.options).some((option) => option.value === val)
        ) {
          element.value = val;
        }
      } else if (element instanceof HTMLInputElement) {
        if (element.type === 'checkbox' || element.type === 'radio') {
          if (queryParams.getAll(param).includes(element.value)) {
            element.checked = true;
          }
        } else {
          element.value = val;
        }
      }
    } else if (elements.length > 1) {
      if (elements.item(0) instanceof HTMLInputElement) {
        const inputs = Array.from(elements as NodeListOf<HTMLInputElement>);
        const values = new Set(queryParams.getAll(param));
        if (inputs.some((input) => values.has(input.value))) {
          for (const input of inputs) {
            input.checked = values.has(input.value);
          }
        }
      }
    }
  }

  form.dispatchEvent(new Event('change'));
}

/**
 * Restores saved default options into the query form on initial page load and
 * wires the save-default-options button to persist all form options.
 */
export function restoreAndPersistFormOptions(
  form: HTMLFormElement,
  saveButton: HTMLButtonElement,
  storageManager: LocalStorageManager,
): void {
  const queryTextarea = form.querySelector<HTMLTextAreaElement>(
    'textarea[name="query"]',
  );

  // Restore saved defaults only on initial load when no query was submitted
  // and no shareable link hash is present in the URL.
  const savedDefaults = storageManager.getSetting('default_options');
  if (
    !queryTextarea?.value &&
    !window.location.hash &&
    savedDefaults !== undefined
  ) {
    restoreForm(form, savedDefaults, /* includeQuery= */ false);
  }

  // Persist current form options to local storage when the button is clicked.
  saveButton.addEventListener('click', () => {
    storageManager.updateSetting(
      'default_options',
      serializeForm(form, /* includeQuery= */ false),
    );
    saveButton.disabled = true;
    saveButton.textContent = 'Default options saved';
  });

  // Re-enable the save button when any form option (excluding query) changes.
  const onFormOptionChange = (event: Event): void => {
    if (event.target === queryTextarea) return;
    saveButton.disabled = false;
    saveButton.textContent = 'Save options as default';
  };

  form.addEventListener('change', onFormOptionChange);
  form.addEventListener('input', onFormOptionChange);
}
