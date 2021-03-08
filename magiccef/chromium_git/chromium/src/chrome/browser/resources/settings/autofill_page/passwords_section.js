// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * @fileoverview 'passwords-section' is the collapsible section containing
 * the list of saved passwords as well as the list of sites that will never
 * save any passwords.
 */

/** @typedef {!{model: !{item: !PasswordManagerProxy.UiEntryWithPassword}}} */
let PasswordUiEntryEvent;

/** @typedef {!{model: !{item: !chrome.passwordsPrivate.ExceptionEntry}}} */
let ExceptionEntryEntryEvent;

(function() {
'use strict';

/**
 * Checks if an HTML element is an editable. An editable is either a text
 * input or a text area.
 * @param {!Element} element
 * @return {boolean}
 */
function isEditable(element) {
  const nodeName = element.nodeName.toLowerCase();
  return element.nodeType === Node.ELEMENT_NODE &&
      (nodeName === 'textarea' ||
       (nodeName === 'input' &&
        /^(?:text|search|email|number|tel|url|password)$/i.test(element.type)));
}

Polymer({
  is: 'passwords-section',

  behaviors: [
    I18nBehavior,
    ListPropertyUpdateBehavior,
    Polymer.IronA11yKeysBehavior,
    settings.GlobalScrollTargetBehavior,
  ],

  properties: {
    /** Preferences state. */
    prefs: {
      type: Object,
      notify: true,
    },

    /**
     * An array of passwords to display.
     * @type {!Array<!PasswordManagerProxy.UiEntryWithPassword>}
     */
    savedPasswords: {
      type: Array,
      value: () => [],
    },

    /**
     * An array of sites to display.
     * @type {!Array<!PasswordManagerProxy.ExceptionEntry>}
     */
    passwordExceptions: {
      type: Array,
      value: () => [],
    },

    /**
     * Duration of the undo toast in ms
     * @private
     */
    toastDuration_: {
      type: Number,
      value: 5000,
    },

    /** @override */
    subpageRoute: {
      type: Object,
      value: settings.routes.PASSWORDS,
    },

    /**
     * The model for any password related action menus or dialogs.
     * @private {?PasswordListItemElement}
     */
    activePassword: Object,

    /** The target of the key bindings defined below. */
    keyEventTarget: {
      type: Object,
      value: () => document,
    },

    /** @private */
    showExportPasswords_: {
      type: Boolean,
      computed: 'hasPasswords_(savedPasswords.splices)',
    },

    /** @private */
    showImportPasswords_: {
      type: Boolean,
      value: function() {
        return loadTimeData.valueExists('showImportPasswords') &&
            loadTimeData.getBoolean('showImportPasswords');
      }
    },

    /** @private */
    showPasswordEditDialog_: Boolean,

    /** Filter on the saved passwords and exceptions. */
    filter: {
      type: String,
      value: '',
    },

    /** @private {!PasswordManagerProxy.UiEntryWithPassword} */
    lastFocused_: Object,

    /** @private */
    listBlurred_: Boolean,

    // <if expr="chromeos">
    /**
     * Auth token for retrieving passwords if required by OS.
     * @private
     */
    authToken_: {
      type: String,
      value: '',
      observer: 'onAuthTokenChanged_',
    },

    /** @private */
    showPasswordPromptDialog_: Boolean,

    /** @private {settings.BlockingRequestManager} */
    tokenRequestManager_: Object
    // </if>
  },

  listeners: {
    'password-menu-tap': 'onPasswordMenuTap_',
  },

  keyBindings: {
    // <if expr="is_macosx">
    'meta+z': 'onUndoKeyBinding_',
    // </if>
    // <if expr="not is_macosx">
    'ctrl+z': 'onUndoKeyBinding_',
    // </if>
  },

  /**
   * The element to return focus to, when the currently active dialog is
   * closed.
   * @private {?HTMLElement}
   */
  activeDialogAnchor_: null,

  /**
   * @type {PasswordManagerProxy}
   * @private
   */
  passwordManager_: null,

  /**
   * @type {?function(!Array<PasswordManagerProxy.PasswordUiEntry>):void}
   * @private
   */
  setSavedPasswordsListener_: null,

  /**
   * @type {?function(!Array<PasswordManagerProxy.ExceptionEntry>):void}
   * @private
   */
  setPasswordExceptionsListener_: null,

  /** @override */
  attached: function() {
    // Create listener functions.
    const setSavedPasswordsListener = list => {
      const newList = list.map(entry => ({entry: entry, password: ''}));
      // Because the backend guarantees that item.entry.id uniquely identifies a
      // given entry and is stable with regard to mutations to the list, it is
      // sufficient to just use this id to create a item uid.
      this.updateList('savedPasswords', item => item.entry.id, newList);
    };

    const setPasswordExceptionsListener = list => {
      this.passwordExceptions = list;
    };

    this.setSavedPasswordsListener_ = setSavedPasswordsListener;
    this.setPasswordExceptionsListener_ = setPasswordExceptionsListener;

    // Set the manager. These can be overridden by tests.
    this.passwordManager_ = PasswordManagerImpl.getInstance();

    // <if expr="chromeos">
    this.tokenRequestManager_ = new settings.BlockingRequestManager(
        () => this.showPasswordPromptDialog_ = true);
    // </if>

    // Request initial data.
    this.passwordManager_.getSavedPasswordList(setSavedPasswordsListener);
    this.passwordManager_.getExceptionList(setPasswordExceptionsListener);

    // Listen for changes.
    this.passwordManager_.addSavedPasswordListChangedListener(
        setSavedPasswordsListener);
    this.passwordManager_.addExceptionListChangedListener(
        setPasswordExceptionsListener);

    this.notifySplices('savedPasswords', []);

    Polymer.RenderStatus.afterNextRender(this, function() {
      Polymer.IronA11yAnnouncer.requestAvailability();
    });
  },

  /** @override */
  detached: function() {
    this.passwordManager_.removeSavedPasswordListChangedListener(
        /**
         * @type {function(!Array<PasswordManagerProxy.PasswordUiEntry>):void}
         */
        (this.setSavedPasswordsListener_));
    this.passwordManager_.removeExceptionListChangedListener(
        /**
         * @type {function(!Array<PasswordManagerProxy.ExceptionEntry>):void}
         */
        (this.setPasswordExceptionsListener_));

    if (this.$.undoToast.open) {
      this.$.undoToast.hide();
    }
  },

  // <if expr="chromeos">
  /**
   * When |authToken_| changes to a new non-empty value, it means that the
   * password-prompt-dialog succeeded in creating a fresh token in the
   * quickUnlockPrivate API. Because new tokens can only ever be created
   * immediately following a GAIA password check, the passwordsPrivate API can
   * now safely grant requests for secure data (i.e. saved passwords) for a
   * limited time. This observer resolves the request, triggering a callback
   * that requires a fresh auth token to succeed and that was provided to the
   * BlockingRequestManager by another DOM element seeking secure data.
   *
   * @param {string} newToken The newly created auth token. Note that its
   *     precise value is not relevant here, only the facts that it changed and
   *     that it is non-empty (i.e. not expired).
   * @private
   */
  onAuthTokenChanged_: function(newToken) {
    if (newToken) {
      this.tokenRequestManager_.resolve();
    }
  },

  onPasswordPromptClosed_: function() {
    this.showPasswordPromptDialog_ = false;
  },
  // </if>

  /**
   * Shows the edit password dialog.
   * @param {!Event} e
   * @private
   */
  onMenuEditPasswordTap_: function(e) {
    e.preventDefault();
    /** @type {CrActionMenuElement} */ (this.$.menu).close();
    this.showPasswordEditDialog_ = true;
  },

  /** @private */
  onPasswordEditDialogClosed_: function() {
    this.showPasswordEditDialog_ = false;
    cr.ui.focusWithoutInk(assert(this.activeDialogAnchor_));
    this.activeDialogAnchor_ = null;

    // Trigger a re-evaluation of the activePassword as the visibility state of
    // the password might have changed.
    this.activePassword.notifyPath('item.password');
  },

  /**
   * @param {string} filter
   * @return {!Array<!PasswordManagerProxy.UiEntryWithPassword>}
   * @private
   */
  getFilteredPasswords_: function(filter) {
    if (!filter) {
      return this.savedPasswords.slice();
    }

    return this.savedPasswords.filter(
        p => [p.entry.urls.shown, p.entry.username].some(
            term => term.toLowerCase().includes(filter.toLowerCase())));
  },

  /**
   * @param {string} filter
   * @return {function(!chrome.passwordsPrivate.ExceptionEntry): boolean}
   * @private
   */
  passwordExceptionFilter_: function(filter) {
    return exception => exception.urls.shown.toLowerCase().includes(
               filter.toLowerCase());
  },

  /**
   * Fires an event that should delete the saved password.
   * @private
   */
  onMenuRemovePasswordTap_: function() {
    this.passwordManager_.removeSavedPassword(
        this.activePassword.item.entry.id);
    this.fire('iron-announce', {text: this.$.undoLabel.textContent});
    this.$.undoToast.show();
    /** @type {CrActionMenuElement} */ (this.$.menu).close();
  },

  /**
   * Handle the undo shortcut.
   * @param {!Event} event
   * @private
   */
  onUndoKeyBinding_: function(event) {
    const activeElement = getDeepActiveElement();
    if (!activeElement || !isEditable(activeElement)) {
      this.passwordManager_.undoRemoveSavedPasswordOrException();
      this.$.undoToast.hide();
      // Preventing the default is necessary to not conflict with a possible
      // search action.
      event.preventDefault();
    }
  },

  onUndoButtonTap_: function() {
    this.passwordManager_.undoRemoveSavedPasswordOrException();
    this.$.undoToast.hide();
  },
  /**
   * Fires an event that should delete the password exception.
   * @param {!ExceptionEntryEntryEvent} e The polymer event.
   * @private
   */
  onRemoveExceptionButtonTap_: function(e) {
    this.passwordManager_.removeException(e.model.item.id);
  },

  /**
   * Opens the password action menu.
   * @param {!Event} event
   * @private
   */
  onPasswordMenuTap_: function(event) {
    const menu = /** @type {!CrActionMenuElement} */ (this.$.menu);
    const target = /** @type {!HTMLElement} */ (event.detail.target);

    this.activePassword =
        /** @type {!PasswordListItemElement} */ (event.detail.listItem);
    menu.showAt(target);
    this.activeDialogAnchor_ = target;
  },

  /**
   * Opens the export/import action menu.
   * @private
   */
  onImportExportMenuTap_: function() {
    const menu = /** @type {!CrActionMenuElement} */ (this.$.exportImportMenu);
    const target =
        /** @type {!HTMLElement} */ (this.$$('#exportImportMenuButton'));

    menu.showAt(target);
    this.activeDialogAnchor_ = target;
  },

  /**
   * Fires an event that should trigger the password import process.
   * @private
   */
  onImportTap_: function() {
    this.passwordManager_.importPasswords();
    this.$.exportImportMenu.close();
  },

  /**
   * Opens the export passwords dialog.
   * @private
   */
  onExportTap_: function() {
    this.showPasswordsExportDialog_ = true;
    this.$.exportImportMenu.close();
  },

  /** @private */
  onPasswordsExportDialogClosed_: function() {
    this.showPasswordsExportDialog_ = false;
    cr.ui.focusWithoutInk(assert(this.activeDialogAnchor_));
    this.activeDialogAnchor_ = null;
  },

  /**
   * Returns true if the list exists and has items.
   * @param {Array<Object>} list
   * @return {boolean}
   * @private
   */
  hasSome_: function(list) {
    return !!(list && list.length);
  },

  /**
   * @private
   * @param {boolean} toggleValue
   * @return {string}
   */
  getOnOffLabel_: function(toggleValue) {
    return toggleValue ? this.i18n('toggleOn') : this.i18n('toggleOff');
  },

  /** @private */
  hasPasswords_: function() {
    return this.savedPasswords.length > 0;
  },

  /**
   * @private
   * @param {boolean} showExportPasswords
   * @param {boolean} showImportPasswords
   * @return {boolean}
   */
  showImportOrExportPasswords_: function(
      showExportPasswords, showImportPasswords) {
    return showExportPasswords || showImportPasswords;
  }
});
})();
