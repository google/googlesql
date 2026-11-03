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
 * @fileoverview Main entry point initializing UI components for Execute Query
 * web UI.
 */

import {decorateAllResolvedASTTrees} from './decorate_tree';
import {HistoryMenu, HistoryStore} from './history';
import {HistoryDialog} from './history_dialog';
import {
  LocalStorageManager,
  restoreAndPersistFormOptions,
} from './local_storage_manager';
import {initRewriteSteps} from './rewrite_steps';
import {SplitterManager} from './splitter';
import {ThemeManager} from './theme';

/**
 * Initializes web UI components including theme manager, draggable splitter,
 * query form options persistence, query history, rewrite step viewer, and
 * resolved AST column
 * hover highlighting.
 */
function init(): void {
  const storageManager = new LocalStorageManager();

  const darkModeToggle = document.getElementById('dark-mode-toggle');
  if (darkModeToggle) {
    new ThemeManager(darkModeToggle, storageManager).init();
  }

  const splitter = document.getElementById('splitter');
  const main = document.querySelector('main');
  const layoutToggle = document.getElementById('layout-toggle');
  if (splitter && main) {
    new SplitterManager(splitter, main, layoutToggle, storageManager).init();
  }

  const form = document.getElementById('form') as HTMLFormElement | null;
  const saveDefaultOptionsBtn = document.getElementById(
    'save-default-options',
  ) as HTMLButtonElement | null;
  if (form && saveDefaultOptionsBtn) {
    restoreAndPersistFormOptions(form, saveDefaultOptionsBtn, storageManager);
  }

  const historyToggle = document.getElementById(
    'history-toggle',
  ) as HTMLButtonElement | null;
  const historyMenu = document.getElementById('history-menu');
  const historyDialogElement = document.getElementById(
    'history-dialog',
  ) as HTMLDialogElement | null;
  if (form && historyToggle && historyMenu && historyDialogElement) {
    const historyStore = new HistoryStore(storageManager);
    const historyDialog = new HistoryDialog(
      form,
      historyDialogElement,
      historyStore,
    );
    historyDialog.init();
    new HistoryMenu(form, historyToggle, historyMenu, historyStore, () => {
      historyDialog.open();
    }).init();
  }

  // Disable submit when the query textarea is empty.
  const queryTextarea = document.getElementById(
    'query',
  ) as HTMLTextAreaElement | null;
  const submitBtn = document.getElementById(
    'submit',
  ) as HTMLInputElement | null;
  if (queryTextarea && submitBtn) {
    const updateSubmitButtonState = () => {
      submitBtn.disabled = !queryTextarea.value.trim();
    };
    queryTextarea.addEventListener('input', updateSubmitButtonState);
    queryTextarea.addEventListener('change', updateSubmitButtonState);
    updateSubmitButtonState();
  }

  initRewriteSteps(document);
  decorateAllResolvedASTTrees(document);
}

if (typeof document !== 'undefined') {
  init();
}
