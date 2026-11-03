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
 * @fileoverview A dialog showing the full query history, with a list of
 * entries, a preview of the selected entry, and actions to load, run, or
 * delete it.
 */

import {
  createEntryRow,
  HistoryEntry,
  HistoryStore,
  restoreEntry,
} from './history';

/**
 * Returns the summary of an option that has a preset (`selectName`) and a
 * free-form, comma-separated list of additions and removals (`textName`), e.g.
 * "MAXIMUM +FEATURE_A, -FEATURE_B". Modifiers are separated by ", " so the
 * summary can wrap between them. Returns an empty string if neither is set.
 */
export function formatOptionSummary(
  options: string,
  selectName: string,
  textName: string,
): string {
  const params = new URLSearchParams(options);
  const modifiers = (params.get(textName) ?? '')
    .split(/[\s,]+/)
    .filter((modifier) => !!modifier)
    .join(', ');
  return [params.get(selectName)?.trim(), modifiers]
    .filter((part) => !!part)
    .join(' ');
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
 * Shows all stored history entries in a modal dialog. The list on the left
 * uses the same rows as the history menu (query, time, and catalog), with a
 * "Clear all history" action below it. The preview on the right shows the full
 * query and, in one muted line, the modes, language features, and rewrites it
 * ran with, which the rows don't show.
 *
 * Load fills in the form; Run fills in the form and submits it; Delete removes
 * the entry. Keyboard: Up/Down (and Home/End) change the selection, Enter
 * loads, Ctrl/Cmd+Enter runs, and Escape closes the dialog.
 */
export class HistoryDialog {
  private readonly list: HTMLElement;
  private readonly count: HTMLElement;
  private readonly preview: HTMLElement;
  private readonly previewQuery: HTMLElement;
  private readonly previewSettings: HTMLElement;
  private readonly clearButton: HTMLButtonElement;
  private readonly loadButton: HTMLButtonElement;
  private readonly runButton: HTMLButtonElement;
  private readonly deleteButton: HTMLButtonElement;
  private readonly closeButton: HTMLButtonElement;

  private entries: HistoryEntry[] = [];
  private selectedIndex = -1;

  constructor(
    private readonly form: HTMLFormElement,
    private readonly dialog: HTMLDialogElement,
    private readonly history: HistoryStore,
    private readonly now: () => number = () => Date.now(),
  ) {
    // These elements are always present in the history dialog markup.
    const find = <T extends HTMLElement>(selector: string) =>
      dialog.querySelector<T>(selector)!;
    this.list = find('.history-dialog-list');
    this.count = find('.history-dialog-count');
    this.preview = find('.history-preview');
    this.previewQuery = find('.history-preview-query');
    this.previewSettings = find('.history-preview-settings');
    this.clearButton = find('.history-dialog-clear');
    this.loadButton = find('.history-load');
    this.runButton = find('.history-run');
    this.deleteButton = find('.history-delete');
    this.closeButton = find('.history-dialog-close');
  }

  init(): void {
    this.loadButton.addEventListener('click', () => {
      this.load();
    });
    this.runButton.addEventListener('click', () => {
      this.run();
    });
    this.deleteButton.addEventListener('click', () => {
      this.deleteSelected();
    });
    this.closeButton.addEventListener('click', () => {
      this.close();
    });
    this.clearButton.addEventListener('click', () => {
      this.clearAll();
    });
    this.list.addEventListener('keydown', (event) => {
      this.onListKeydown(event);
    });
  }

  isOpen(): boolean {
    return this.dialog.open;
  }

  open(): void {
    this.entries = this.history.getEntries();
    this.renderList();
    this.select(0);
    this.dialog.showModal();
    this.list.focus();
  }

  close(): void {
    this.dialog.close();
  }

  private selectedEntry(): HistoryEntry | undefined {
    return this.entries[this.selectedIndex];
  }

  /** Fills in the form with the selected entry and closes the dialog. */
  private load(): void {
    const entry = this.selectedEntry();
    if (!entry) return;
    restoreEntry(this.form, entry);
    this.close();
  }

  /** Fills in the form with the selected entry, closes, and submits. */
  private run(): void {
    const entry = this.selectedEntry();
    if (!entry) return;
    restoreEntry(this.form, entry);
    this.close();
    // requestSubmit fires the submit event, so the query is recorded in the
    // history and the editor contents are synced as for a normal submit.
    this.form.requestSubmit();
  }

  private deleteSelected(): void {
    const entry = this.selectedEntry();
    if (!entry) return;
    this.history.remove(entry.query);
    this.entries = this.history.getEntries();
    this.renderList();
    // Keep the selection at the same position, i.e. on the next entry, or on
    // the previous one if the last entry was deleted.
    this.select(Math.min(this.selectedIndex, this.entries.length - 1));
    this.list.focus();
  }

  private clearAll(): void {
    if (!window.confirm('Clear all query history?')) return;
    this.history.clear();
    this.entries = [];
    this.renderList();
    this.select(-1);
  }

  private renderList(): void {
    const now = this.now();
    const n = this.entries.length;
    this.count.textContent =
      n === 0 ? '' : `${n} ${n === 1 ? 'entry' : 'entries'}`;
    this.clearButton.disabled = n === 0;
    this.list.replaceChildren();
    if (this.entries.length === 0) {
      const empty = document.createElement('div');
      empty.className = 'history-empty';
      empty.textContent = 'No history yet';
      this.list.appendChild(empty);
      return;
    }
    this.entries.forEach((entry, index) => {
      const item = createEntryRow(entry, now);
      item.id = `history-dialog-item-${index}`;
      item.setAttribute('role', 'option');
      item.setAttribute('aria-selected', 'false');
      // Rows are selected via the list's keyboard handling rather than being
      // individual tab stops.
      item.tabIndex = -1;
      item.addEventListener('click', () => {
        this.select(index);
        this.list.focus();
      });
      item.addEventListener('dblclick', () => {
        this.select(index);
        this.load();
      });
      this.list.appendChild(item);
    });
  }

  /** Selects the entry at `index`, or nothing if there are no entries. */
  private select(index: number): void {
    this.list
      .querySelector('[aria-selected="true"]')
      ?.setAttribute('aria-selected', 'false');
    this.selectedIndex =
      this.entries.length === 0
        ? -1
        : Math.max(0, Math.min(index, this.entries.length - 1));

    const item = this.list.querySelector<HTMLElement>(
      `#history-dialog-item-${this.selectedIndex}`,
    );
    if (item) {
      item.setAttribute('aria-selected', 'true');
      this.list.setAttribute('aria-activedescendant', item.id);
      item.scrollIntoView({block: 'nearest'});
    } else {
      this.list.removeAttribute('aria-activedescendant');
    }
    this.renderPreview();
  }

  private renderPreview(): void {
    const entry = this.selectedEntry();
    this.preview.hidden = !entry;
    for (const button of [this.loadButton, this.runButton, this.deleteButton]) {
      button.disabled = !entry;
    }
    if (!entry) return;

    this.previewQuery.textContent = entry.query;
    this.previewQuery.scrollTop = 0;

    // One muted line, e.g. "Mode Execute, Analyze · Features MAXIMUM +A ·
    // Rewrites DEFAULTS". The catalog and time are already shown in the list
    // row.
    this.previewSettings.replaceChildren();
    const settings = [
      ['Mode', formatModes(entry.options)],
      [
        'Features',
        formatOptionSummary(
          entry.options,
          'language-features',
          'language-features-text',
        ),
      ],
      [
        'Rewrites',
        formatOptionSummary(entry.options, 'ast-rewrites', 'ast-rewrites-text'),
      ],
    ].filter(([, value]) => !!value);
    settings.forEach(([label, value], index) => {
      if (index > 0) {
        this.previewSettings.append(' \u00b7 ');
      }
      const labelElement = document.createElement('span');
      labelElement.className = 'history-preview-setting-label';
      labelElement.textContent = label;
      const valueElement = document.createElement('span');
      valueElement.className = 'history-preview-setting-value';
      valueElement.textContent = value;
      this.previewSettings.append(labelElement, ' ', valueElement);
    });
    this.previewSettings.hidden = settings.length === 0;
  }

  private onListKeydown(event: KeyboardEvent): void {
    switch (event.key) {
      case 'ArrowDown':
        event.preventDefault();
        this.select(this.selectedIndex + 1);
        break;
      case 'ArrowUp':
        event.preventDefault();
        this.select(this.selectedIndex - 1);
        break;
      case 'Home':
        event.preventDefault();
        this.select(0);
        break;
      case 'End':
        event.preventDefault();
        this.select(this.entries.length - 1);
        break;
      case 'Enter':
        event.preventDefault();
        if (event.ctrlKey || event.metaKey) {
          this.run();
        } else {
          this.load();
        }
        break;
      default:
        break;
    }
  }
}
