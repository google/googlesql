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
 * @fileoverview Maintains a history of recently submitted queries (along with
 * all form options) in local storage, and renders the most recent ones as a
 * dropdown menu in the page header.
 */

import {
  LocalStorageManager,
  restoreForm,
  serializeForm,
} from './local_storage_manager';

/** Maximum number of history entries retained in storage. */
export const MAX_STORED_ENTRIES = 100;

/** Number of most recent history entries shown in the history menu. */
export const MENU_ENTRY_COUNT = 8;

/** Version of the stored history format; stored data of other versions is ignored. */
export const HISTORY_FORMAT_VERSION = 1;

/** Number of query lines shown for each entry in the history menu. */
const PREVIEW_LINE_COUNT = 2;

/**
 * A single history entry.
 */
export declare interface HistoryEntry {
  /** The query text, as entered. */
  query: string;
  /**
   * All other form controls (modes, catalog, language features, etc.),
   * URL-encoded as by `serializeForm(form, false)`.
   */
  options: string;
  /** Time the entry was last run, in milliseconds since the epoch. */
  timestamp: number;
}

/** The value stored under the 'history' storage key. */
declare interface StoredHistory {
  version: number;
  entries: HistoryEntry[];
}

function isHistoryEntry(value: unknown): value is HistoryEntry {
  if (!value || typeof value !== 'object') return false;
  const entry = value as Partial<HistoryEntry>;
  return (
    typeof entry.query === 'string' &&
    typeof entry.options === 'string' &&
    typeof entry.timestamp === 'number'
  );
}

/**
 * Returns the key used to identify history entries: the query text with
 * leading and trailing whitespace removed. Options are not part of the key.
 */
function getEntryKey(query: string): string {
  return query.trim();
}

/**
 * Stores the most recently used queries and their form options, most recent
 * first, under the 'history' storage key.
 *
 * Entries are identified by the query text alone (ignoring leading and
 * trailing whitespace). Form options such as modes, catalog, and language
 * features are stored with each entry so they can be restored, but are NOT
 * used to distinguish entries: running the same query with different options
 * replaces the existing entry rather than adding a new one. This keeps the
 * menu, which only shows the query and mode, free of identical-looking
 * rows.
 */
export class HistoryStore {
  constructor(
    private readonly storageManager: LocalStorageManager,
    private readonly now: () => number = () => Date.now(),
  ) {}

  /** Returns all stored history entries, most recent first. */
  getEntries(): HistoryEntry[] {
    const stored = this.storageManager.getJson('history');
    if (!stored || typeof stored !== 'object') return [];
    const {version, entries} = stored as Partial<StoredHistory>;
    if (version !== HISTORY_FORMAT_VERSION || !Array.isArray(entries)) {
      return [];
    }
    return entries.filter(isHistoryEntry).slice(0, MAX_STORED_ENTRIES);
  }

  /**
   * Adds `query` with its `options` to the top of the history. Any existing
   * entry with the same query (ignoring leading and trailing whitespace) is
   * replaced, regardless of its options or catalog, so the most recent options
   * win. Only the most recent `MAX_STORED_ENTRIES` entries are retained.
   */
  add(query: string, options: string): void {
    const key = getEntryKey(query);
    const entries = this.getEntries().filter(
      (entry) => getEntryKey(entry.query) !== key,
    );
    entries.unshift({query, options, timestamp: this.now()});
    this.save(entries.slice(0, MAX_STORED_ENTRIES));
  }

  /**
   * Removes the entry for `query` (matched ignoring leading and trailing
   * whitespace), if any.
   */
  remove(query: string): void {
    const key = getEntryKey(query);
    const entries = this.getEntries();
    const remaining = entries.filter(
      (entry) => getEntryKey(entry.query) !== key,
    );
    if (remaining.length !== entries.length) {
      this.save(remaining);
    }
  }

  /** Removes all history entries, including those not shown in the menu. */
  clear(): void {
    this.storageManager.remove('history');
  }

  /**
   * Saves `entries`. If the write fails (e.g. because the storage quota is
   * exceeded), the oldest half of the entries is dropped and the write is
   * retried, down to a single entry.
   */
  private save(entries: HistoryEntry[]): void {
    let toSave = entries;
    for (;;) {
      const stored: StoredHistory = {
        version: HISTORY_FORMAT_VERSION,
        entries: toSave,
      };
      if (
        this.storageManager.setJson('history', stored) ||
        toSave.length <= 1
      ) {
        return;
      }
      toSave = toSave.slice(0, Math.ceil(toSave.length / 2));
    }
  }
}

/**
 * Returns up to the first two non-blank lines of `query`, trimmed, for display
 * in the history menu.
 */
export function getPreviewLines(query: string): string[] {
  return query
    .split(/\r?\n/)
    .map((line) => line.trim())
    .filter((line) => line.length > 0)
    .slice(0, PREVIEW_LINE_COUNT);
}

/**
 * Formats the time elapsed since `timestamp` compactly, e.g. "now", "5m",
 * "3h", or "2d".
 */
export function formatRelativeTime(timestamp: number, now: number): string {
  const minutes = Math.floor(Math.max(0, now - timestamp) / 60_000);
  if (minutes < 1) return 'now';
  if (minutes < 60) return `${minutes}m`;
  const hours = Math.floor(minutes / 60);
  if (hours < 24) return `${hours}h`;
  return `${Math.floor(hours / 24)}d`;
}

/**
 * Fills in `form` with the query and options of `entry`. Does not submit the
 * form or change the saved default options.
 */
export function restoreEntry(form: HTMLFormElement, entry: HistoryEntry): void {
  const params = new URLSearchParams(entry.options);
  params.set('query', entry.query);
  restoreForm(form, params.toString(), /* includeQuery= */ true);
}

/**
 * Returns the selected execution modes from `options`, capitalized and joined
 * with ", ", e.g. "Execute, Analyze". Returns an empty string if none are set.
 */
function formatModes(options: string): string {
  return new URLSearchParams(options)
    .getAll('mode')
    .map((mode) => mode.trim())
    .filter((mode) => !!mode)
    .map((mode) => mode[0].toUpperCase() + mode.slice(1))
    .join(', ');
}

/**
 * Creates a row showing the first lines of `entry`'s query, how long ago it
 * ran, and its execution modes. Used by both the history menu and the history
 * dialog; callers add the ARIA role and event handlers.
 */
export function createEntryRow(
  entry: HistoryEntry,
  now: number,
): HTMLButtonElement {
  const item = document.createElement('button');
  item.type = 'button';
  item.className = 'history-item';

  const queryElement = document.createElement('span');
  queryElement.className = 'history-query';
  const lines = getPreviewLines(entry.query);
  for (let i = 0; i < PREVIEW_LINE_COUNT; i++) {
    const line = document.createElement('span');
    line.className = 'history-line';
    line.textContent = lines[i] ?? '';
    queryElement.appendChild(line);
  }
  item.appendChild(queryElement);

  const meta = document.createElement('span');
  meta.className = 'history-meta';
  const time = document.createElement('span');
  time.className = 'history-time';
  time.textContent = formatRelativeTime(entry.timestamp, now);
  meta.appendChild(time);
  const mode = formatModes(entry.options);
  if (mode) {
    const modeElement = document.createElement('span');
    modeElement.className = 'history-mode';
    modeElement.textContent = mode;
    meta.appendChild(modeElement);
  }
  item.appendChild(meta);

  return item;
}

/**
 * Renders the query history as a dropdown menu, records submitted queries, and
 * restores the form when an entry is picked.
 *
 * Picking an entry only fills in the form; it does not submit it, reorder the
 * history, or change the saved default options. Running the query moves its
 * entry to the top.
 */
export class HistoryMenu {
  private readonly list: HTMLElement;
  private readonly footer: HTMLElement;
  private readonly moreButton: HTMLButtonElement;

  /**
   * @param showAllHistory Called when the user clicks "View all history" (e.g.
   *     to open the history dialog, which is also where history is cleared).
   */
  constructor(
    private readonly form: HTMLFormElement,
    private readonly toggle: HTMLButtonElement,
    private readonly menu: HTMLElement,
    private readonly history: HistoryStore,
    private readonly showAllHistory: () => void,
    private readonly now: () => number = () => Date.now(),
  ) {
    // These elements are always present in the history menu markup.
    this.list = menu.querySelector<HTMLElement>('.history-list')!;
    this.footer = menu.querySelector<HTMLElement>('.history-footer')!;
    this.moreButton = menu.querySelector<HTMLButtonElement>('.history-more')!;
  }

  init(): void {
    this.form.addEventListener('submit', () => {
      this.recordCurrentForm();
    });

    this.toggle.addEventListener('click', () => {
      if (this.isOpen()) {
        this.close();
      } else {
        this.open();
      }
    });

    this.toggle.addEventListener('keydown', (event) => {
      if (event.key === 'ArrowDown' && !this.isOpen()) {
        event.preventDefault();
        this.open();
      }
    });

    this.menu.addEventListener('keydown', (event) => {
      this.onMenuKeydown(event);
    });

    this.moreButton.addEventListener('click', () => {
      this.close(/* restoreFocus= */ false);
      this.showAllHistory();
    });

    // Close the menu when clicking anywhere outside of it.
    document.addEventListener('click', (event) => {
      const target = event.target as Node | null;
      if (
        this.isOpen() &&
        target &&
        !this.menu.contains(target) &&
        !this.toggle.contains(target)
      ) {
        this.close(/* restoreFocus= */ false);
      }
    });
  }

  isOpen(): boolean {
    return !this.menu.hidden;
  }

  open(): void {
    this.render();
    this.menu.hidden = false;
    this.toggle.setAttribute('aria-expanded', 'true');
    this.getFocusableItems()[0]?.focus();
  }

  close(restoreFocus = true): void {
    this.menu.hidden = true;
    this.toggle.setAttribute('aria-expanded', 'false');
    if (restoreFocus) {
      this.toggle.focus();
    }
  }

  private recordCurrentForm(): void {
    const params = new URLSearchParams(
      serializeForm(this.form, /* includeQuery= */ true),
    );
    const query = params.get('query') ?? '';
    params.delete('query');
    if (query.trim()) {
      this.history.add(query, params.toString());
    }
  }

  private pick(entry: HistoryEntry): void {
    // Only fill in the form. The entry moves to the top (with a fresh
    // timestamp) when the query is actually run.
    restoreEntry(this.form, entry);
    this.close();
  }

  private render(): void {
    const entries = this.history.getEntries().slice(0, MENU_ENTRY_COUNT);
    const now = this.now();
    this.list.replaceChildren();

    if (entries.length === 0) {
      const empty = document.createElement('div');
      empty.className = 'history-empty';
      empty.textContent = 'No history yet';
      this.list.appendChild(empty);
    }
    for (const entry of entries) {
      const item = createEntryRow(entry, now);
      item.setAttribute('role', 'menuitem');
      item.addEventListener('click', () => {
        this.pick(entry);
      });
      this.list.appendChild(item);
    }
    this.footer.hidden = entries.length === 0;
  }

  private getFocusableItems(): HTMLButtonElement[] {
    return Array.from(
      this.menu.querySelectorAll<HTMLButtonElement>(
        '.history-item, .history-more',
      ),
    ).filter((element) => !element.hidden && !element.closest('[hidden]'));
  }

  private onMenuKeydown(event: KeyboardEvent): void {
    const items = this.getFocusableItems();
    const index = items.indexOf(document.activeElement as HTMLButtonElement);
    switch (event.key) {
      case 'Escape':
        event.preventDefault();
        this.close();
        break;
      case 'Tab':
        this.close(/* restoreFocus= */ false);
        break;
      case 'ArrowDown':
        event.preventDefault();
        items[(index + 1) % items.length]?.focus();
        break;
      case 'ArrowUp':
        event.preventDefault();
        items[(index - 1 + items.length) % items.length]?.focus();
        break;
      case 'Home':
        event.preventDefault();
        items[0]?.focus();
        break;
      case 'End':
        event.preventDefault();
        items[items.length - 1]?.focus();
        break;
      default:
        break;
    }
  }
}
