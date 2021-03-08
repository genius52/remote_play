// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * VolumeManager is responsible for tracking list of mounted volumes.
 * @implements {VolumeManager}
 */
class VolumeManagerImpl extends cr.EventTarget {
  constructor() {
    super();

    /** @override */
    this.volumeInfoList = new VolumeInfoListImpl();

    /**
     * The list of archives requested to mount. We will show contents once
     * archive is mounted, but only for mounts from within this filebrowser tab.
     * @type {Object<Object>}
     * @private
     */
    this.requests_ = {};

    /**
     * Mutex guarding the mounting and unmounting operations, in order to
     * guarantee that onMountCompleted events happen after initialization.
     * @private @const {AsyncUtil.Queue}
     */
    this.mutex_ = new AsyncUtil.Queue();

    // The status should be merged into VolumeManager.
    // TODO(hidehiko): Remove them after the migration.
    /**
     * Connection state of the Drive.
     * @type {VolumeManagerCommon.DriveConnectionState}
     * @private
     */
    this.driveConnectionState_ = {
      type: VolumeManagerCommon.DriveConnectionType.OFFLINE,
      reason: VolumeManagerCommon.DriveConnectionReason.NO_SERVICE,
      hasCellularNetworkAccess: false
    };

    chrome.fileManagerPrivate.onDriveConnectionStatusChanged.addListener(
        this.onDriveConnectionStatusChanged_.bind(this));
    this.onDriveConnectionStatusChanged_();
  }

  /** @override */
  dispose() {}

  /**
   * Invoked when the drive connection status is changed.
   * @private
   */
  onDriveConnectionStatusChanged_() {
    chrome.fileManagerPrivate.getDriveConnectionState(state => {
      // TODO(crbug.com/931971): Convert private API to use enum.
      this.driveConnectionState_ =
          /** @type {VolumeManagerCommon.DriveConnectionState} */ (state);
      cr.dispatchSimpleEvent(this, 'drive-connection-changed');
    });
  }

  /** @override */
  getDriveConnectionState() {
    return this.driveConnectionState_;
  }

  /**
   * Adds new volume info from the given volumeMetadata. If the corresponding
   * volume info has already been added, the volumeMetadata is ignored.
   * @param {!chrome.fileManagerPrivate.VolumeMetadata} volumeMetadata
   * @return {!Promise<!VolumeInfo>}
   * @private
   */
  async addVolumeMetadata_(volumeMetadata) {
    const volumeInfo = await volumeManagerUtil.createVolumeInfo(volumeMetadata);

    // We don't show Downloads and Drive on volume list if they have
    // mount error, since users can do nothing in this situation. We
    // show Removable and Provided volumes regardless of mount error
    // so that users can unmount or format the volume.
    // TODO(fukino): Once the Files app gets ready, show erroneous
    // Drive volume so that users can see auth warning banner on the
    // volume. crbug.com/517772.
    let shouldShow = true;
    switch (volumeInfo.volumeType) {
      case VolumeManagerCommon.VolumeType.DOWNLOADS:
      case VolumeManagerCommon.VolumeType.DRIVE:
        shouldShow = !!volumeInfo.fileSystem;
        break;
    }

    if (!shouldShow) {
      return volumeInfo;
    }

    if (this.volumeInfoList.findIndex(volumeInfo.volumeId) === -1) {
      this.volumeInfoList.add(volumeInfo);

      // Update the network connection status, because until the drive
      // is initialized, the status is set to not ready.
      // TODO(mtomasz): The connection status should be migrated into
      // chrome.fileManagerPrivate.VolumeMetadata.
      if (volumeMetadata.volumeType === VolumeManagerCommon.VolumeType.DRIVE) {
        this.onDriveConnectionStatusChanged_();
      }
    } else if (
        volumeMetadata.volumeType ===
        VolumeManagerCommon.VolumeType.REMOVABLE) {
      // Update for remounted USB external storage, because they were
      // remounted to switch read-only policy.
      this.volumeInfoList.add(volumeInfo);
    }

    return volumeInfo;
  }

  /**
   * Initializes mount points.
   * @return {!Promise<void>}
   */
  async initialize() {
    chrome.fileManagerPrivate.onMountCompleted.addListener(
        this.onMountCompleted_.bind(this));

    console.warn('Getting volumes');
    const volumeMetadataList = await new Promise(
        resolve => chrome.fileManagerPrivate.getVolumeMetadataList(resolve));
    if (!volumeMetadataList) {
      console.error('Cannot get volumes');
      return;
    }
    console.warn(`There are ${volumeMetadataList.length} volumes`);

    // We must subscribe to the mount completed event in the callback of
    // getVolumeMetadataList (crbug.com/330061). But volumes reported by
    // onMountCompleted events must be added after the volumes in the
    // volumeMetadataList are mounted (crbug.com/135477).
    const unlock = await this.mutex_.lock();
    try {
      // Create VolumeInfo for each volume.
      await Promise.all(volumeMetadataList.map(async (volumeMetadata) => {
        console.warn(`Initializing volume: ${volumeMetadata.volumeId}`);
        const volumeInfo = await this.addVolumeMetadata_(volumeMetadata);
        console.warn(`Initialized volume: ${volumeInfo.volumeId}`);
      }));

      console.warn(`Initialized all ${volumeMetadataList.length} volumes`);
    } finally {
      unlock();
    }
  }

  /**
   * Event handler called when some volume was mounted or unmounted.
   * @param {chrome.fileManagerPrivate.MountCompletedEvent} event
   * @private
   */
  async onMountCompleted_(event) {
    const unlock = await this.mutex_.lock();
    try {
      switch (event.eventType) {
        case 'mount':
          var requestKey = this.makeRequestKey_(
              'mount', event.volumeMetadata.sourcePath || '');

          if (event.status === 'success' ||
              event.status ===
                  VolumeManagerCommon.VolumeError.UNKNOWN_FILESYSTEM ||
              event.status ===
                  VolumeManagerCommon.VolumeError.UNSUPPORTED_FILESYSTEM) {
            const volumeInfo =
                await this.addVolumeMetadata_(event.volumeMetadata);
            this.finishRequest_(requestKey, event.status, volumeInfo);
            break;
          }

          if (event.status ===
              VolumeManagerCommon.VolumeError.ALREADY_MOUNTED) {
            const navigationEvent =
                new Event(VolumeManagerCommon.VOLUME_ALREADY_MOUNTED);
            navigationEvent.volumeId = event.volumeMetadata.volumeId;
            this.dispatchEvent(navigationEvent);
            this.finishRequest_(requestKey, event.status);
            break;
          }

          console.error(`Cannot mount volume: ${event.status}`);
          this.finishRequest_(requestKey, event.status);
          break;

        case 'unmount':
          const volumeId = event.volumeMetadata.volumeId;
          const status = event.status;
          var requestKey = this.makeRequestKey_('unmount', volumeId);
          const requested = requestKey in this.requests_;
          const volumeInfoIndex = this.volumeInfoList.findIndex(volumeId);
          const volumeInfo = volumeInfoIndex !== -1 ?
              this.volumeInfoList.item(volumeInfoIndex) :
              null;

          if (event.status === 'success' && !requested && volumeInfo) {
            console.warn(`Unmounted volume without request: ${volumeId}`);
            this.dispatchEvent(
                new CustomEvent('externally-unmounted', {detail: volumeInfo}));
          }

          this.finishRequest_(requestKey, status);
          if (event.status === 'success') {
            this.volumeInfoList.remove(event.volumeMetadata.volumeId);
          }

          console.warn(`Unmounted volume: ${volumeId}`);
          break;
      }
    } finally {
      unlock();
    }
  }

  /**
   * Creates string to match mount events with requests.
   * @param {string} requestType 'mount' | 'unmount'. TODO(hidehiko): Replace by
   *     enum.
   * @param {string} argument Argument describing the request, eg. source file
   *     path of the archive to be mounted, or a volumeId for unmounting.
   * @return {string} Key for |this.requests_|.
   * @private
   */
  makeRequestKey_(requestType, argument) {
    return requestType + ':' + argument;
  }

  /** @override */
  mountArchive(fileUrl, successCallback, errorCallback) {
    chrome.fileManagerPrivate.addMount(fileUrl, sourcePath => {
      console.info(`Mount request: url=${fileUrl}; sourcePath=${sourcePath}`);
      const requestKey = this.makeRequestKey_('mount', sourcePath);
      this.startRequest_(requestKey, successCallback, errorCallback);
    });
  }

  /** @override */
  unmount(volumeInfo, successCallback, errorCallback) {
    chrome.fileManagerPrivate.removeMount(volumeInfo.volumeId);
    const requestKey = this.makeRequestKey_('unmount', volumeInfo.volumeId);
    this.startRequest_(requestKey, successCallback, errorCallback);
  }

  /** @override */
  configure(volumeInfo) {
    return new Promise((fulfill, reject) => {
      chrome.fileManagerPrivate.configureVolume(volumeInfo.volumeId, () => {
        if (chrome.runtime.lastError) {
          reject(chrome.runtime.lastError.message);
        } else {
          fulfill();
        }
      });
    });
  }

  /** @override */
  getVolumeInfo(entry) {
    if (!entry) {
      console.error(`Invalid entry passed to getVolumeInfo: ${entry}`);
      return null;
    }

    for (let i = 0; i < this.volumeInfoList.length; i++) {
      const volumeInfo = this.volumeInfoList.item(i);
      if (volumeInfo.fileSystem &&
          util.isSameFileSystem(volumeInfo.fileSystem, entry.filesystem)) {
        return volumeInfo;
      }
      // Additionally, check fake entries.
      for (let key in volumeInfo.fakeEntries) {
        const fakeEntry = volumeInfo.fakeEntries[key];
        if (util.isSameEntry(fakeEntry, entry)) {
          return volumeInfo;
        }
      }
    }

    return null;
  }

  /** @override */
  getCurrentProfileVolumeInfo(volumeType) {
    for (let i = 0; i < this.volumeInfoList.length; i++) {
      const volumeInfo = this.volumeInfoList.item(i);
      if (volumeInfo.profile.isCurrentProfile &&
          volumeInfo.volumeType === volumeType) {
        return volumeInfo;
      }
    }
    return null;
  }

  /** @override */
  getLocationInfo(entry) {
    if (!entry) {
      console.error(`Invalid entry passed to getLocationInfo: ${entry}`);
      return null;
    }

    const volumeInfo = this.getVolumeInfo(entry);

    if (util.isFakeEntry(entry)) {
      return new EntryLocationImpl(
          volumeInfo, assert(entry.rootType),
          true /* the entry points a root directory. */,
          true /* fake entries are read only. */);
    }

    if (!volumeInfo) {
      return null;
    }

    let rootType;
    let isReadOnly;
    let isRootEntry;
    if (volumeInfo.volumeType === VolumeManagerCommon.VolumeType.DRIVE) {
      // For Drive, the roots are /root, /team_drives, /Computers and /other,
      // instead of /. Root URLs contain trailing slashes.
      if (entry.fullPath == '/root' || entry.fullPath.indexOf('/root/') === 0) {
        rootType = VolumeManagerCommon.RootType.DRIVE;
        isReadOnly = volumeInfo.isReadOnly;
        isRootEntry = entry.fullPath === '/root';
      } else if (
          entry.fullPath == VolumeManagerCommon.SHARED_DRIVES_DIRECTORY_PATH ||
          entry.fullPath.indexOf(
              VolumeManagerCommon.SHARED_DRIVES_DIRECTORY_PATH + '/') === 0) {
        if (entry.fullPath ==
            VolumeManagerCommon.SHARED_DRIVES_DIRECTORY_PATH) {
          rootType = VolumeManagerCommon.RootType.SHARED_DRIVES_GRAND_ROOT;
          isReadOnly = true;
          isRootEntry = true;
        } else {
          rootType = VolumeManagerCommon.RootType.SHARED_DRIVE;
          if (util.isTeamDriveRoot(entry)) {
            isReadOnly = false;
            isRootEntry = true;
          } else {
            // Regular files/directories under Shared Drives.
            isRootEntry = false;
            isReadOnly = volumeInfo.isReadOnly;
          }
        }
      } else if (
          entry.fullPath == VolumeManagerCommon.COMPUTERS_DIRECTORY_PATH ||
          entry.fullPath.indexOf(
              VolumeManagerCommon.COMPUTERS_DIRECTORY_PATH + '/') === 0) {
        if (entry.fullPath == VolumeManagerCommon.COMPUTERS_DIRECTORY_PATH) {
          rootType = VolumeManagerCommon.RootType.COMPUTERS_GRAND_ROOT;
          isReadOnly = true;
          isRootEntry = true;
        } else {
          rootType = VolumeManagerCommon.RootType.COMPUTER;
          if (util.isComputersRoot(entry)) {
            isReadOnly = true;
            isRootEntry = true;
          } else {
            // Regular files/directories under a Computer entry.
            isRootEntry = false;
            isReadOnly = volumeInfo.isReadOnly;
          }
        }
      } else if (
          entry.fullPath == '/other' ||
          entry.fullPath.indexOf('/other/') === 0) {
        rootType = VolumeManagerCommon.RootType.DRIVE_OTHER;
        isReadOnly = true;
        isRootEntry = entry.fullPath === '/other';
      } else if (
          entry.fullPath === '/.files-by-id' ||
          entry.fullPath.indexOf('/.files-by-id/') === 0) {
        rootType = VolumeManagerCommon.RootType.DRIVE_OTHER;

        // /.files-by-id/<id> is read-only, but /.files-by-id/<id>/foo is
        // read-write.
        isReadOnly = entry.fullPath.split('/').length < 4;
        isRootEntry = entry.fullPath === '/.files-by-id';
      } else {
        // Accessing Drive files outside of /drive/root and /drive/other is not
        // allowed, but can happen. Therefore returning null.
        return null;
      }
    } else {
      rootType =
          VolumeManagerCommon.getRootTypeFromVolumeType(volumeInfo.volumeType);
      isRootEntry = util.isSameEntry(entry, volumeInfo.fileSystem.root);
      // Although "Play files" root directory is writable in file system level,
      // we prohibit write operations on it in the UI level to avoid confusion.
      // Users can still have write access in sub directories like
      // /Play files/Pictures, /Play files/DCIM, etc...
      if (volumeInfo.volumeType ==
              VolumeManagerCommon.VolumeType.ANDROID_FILES &&
          isRootEntry) {
        isReadOnly = true;
      } else {
        isReadOnly = volumeInfo.isReadOnly;
      }
    }

    return new EntryLocationImpl(volumeInfo, rootType, isRootEntry, isReadOnly);
  }

  /** @override */
  findByDevicePath(devicePath) {
    for (let i = 0; i < this.volumeInfoList.length; i++) {
      const volumeInfo = this.volumeInfoList.item(i);
      if (volumeInfo.devicePath && volumeInfo.devicePath === devicePath) {
        return volumeInfo;
      }
    }
    return null;
  }

  /** @override */
  whenVolumeInfoReady(volumeId) {
    return new Promise((fulfill) => {
      const handler = () => {
        const index = this.volumeInfoList.findIndex(volumeId);
        if (index !== -1) {
          fulfill(this.volumeInfoList.item(index));
          this.volumeInfoList.removeEventListener('splice', handler);
        }
      };
      this.volumeInfoList.addEventListener('splice', handler);
      handler();
    });
  }

  /** @override */
  getDefaultDisplayRoot(callback) {
    console.error('Unexpected call to VolumeManagerImpl.getDefaultDisplayRoot');
    callback(null);
  }

  /**
   * @param {string} key Key produced by |makeRequestKey_|.
   * @param {function(VolumeInfo)} successCallback To be called when the request
   *     finishes successfully.
   * @param {function(VolumeManagerCommon.VolumeError)} errorCallback To be
   *     called when the request fails.
   * @private
   */
  startRequest_(key, successCallback, errorCallback) {
    if (key in this.requests_) {
      const request = this.requests_[key];
      request.successCallbacks.push(successCallback);
      request.errorCallbacks.push(errorCallback);
    } else {
      this.requests_[key] = {
        successCallbacks: [successCallback],
        errorCallbacks: [errorCallback],

        timeout: setTimeout(
            this.onTimeout_.bind(this, key), volumeManagerUtil.TIMEOUT)
      };
    }
  }

  /**
   * Called if no response received in |TIMEOUT|.
   * @param {string} key Key produced by |makeRequestKey_|.
   * @private
   */
  onTimeout_(key) {
    this.invokeRequestCallbacks_(
        this.requests_[key], VolumeManagerCommon.VolumeError.TIMEOUT);
    delete this.requests_[key];
  }

  /**
   * @param {string} key Key produced by |makeRequestKey_|.
   * @param {VolumeManagerCommon.VolumeError|string} status Status received
   *     from the API.
   * @param {VolumeInfo=} opt_volumeInfo Volume info of the mounted volume.
   * @private
   */
  finishRequest_(key, status, opt_volumeInfo) {
    const request = this.requests_[key];
    if (!request) {
      return;
    }

    clearTimeout(request.timeout);
    this.invokeRequestCallbacks_(request, status, opt_volumeInfo);
    delete this.requests_[key];
  }

  /**
   * @param {Object} request Structure created in |startRequest_|.
   * @param {VolumeManagerCommon.VolumeError|string} status If status ===
   *     'success' success callbacks are called.
   * @param {VolumeInfo=} opt_volumeInfo Volume info of the mounted volume.
   * @private
   */
  invokeRequestCallbacks_(request, status, opt_volumeInfo) {
    const callEach = (callbacks, self, args) => {
      for (let i = 0; i < callbacks.length; i++) {
        callbacks[i].apply(self, args);
      }
    };

    if (status === 'success') {
      callEach(request.successCallbacks, this, [opt_volumeInfo]);
    } else {
      volumeManagerUtil.validateError(status);
      callEach(request.errorCallbacks, this, [status]);
    }
  }
}
