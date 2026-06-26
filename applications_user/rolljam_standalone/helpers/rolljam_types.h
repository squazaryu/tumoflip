// helpers/rolljam_types.h
#pragma once

#include <furi.h>
#include <furi_hal.h>
#include "../defines.h"

typedef struct RollJamApp RollJamApp;
typedef struct RollJamHistory RollJamHistory;

typedef enum {
    RollJamCaptureOwnerNone = 0,
    RollJamCaptureOwnerDualReceiver,
    RollJamCaptureOwnerShieldReceiver,
    RollJamCaptureOwnerMainReceiver,
} RollJamCaptureOwner;

typedef struct {
    uint16_t index;
    RollJamHistory* history;
    RollJamCaptureOwner owner;
} RollJamSelectedCapture;

typedef enum {
    RollJamViewVariableItemList,
    RollJamViewSubmenu,
    RollJamViewWidget,
    RollJamViewReceiver,
    RollJamViewAbout,
    RollJamViewFileBrowser,
    RollJamViewTextInput,
#ifdef ENABLE_DUAL_RX_SCENE
    RollJamViewDualReceiver,
#endif
} RollJamView;

typedef enum {
    // Custom events for views
    RollJamCustomEventViewReceiverOK,
    RollJamCustomEventViewReceiverConfig,
    RollJamCustomEventViewReceiverBack,
    RollJamCustomEventViewReceiverDeleteItem,
    RollJamCustomEventViewReceiverUnlock,
    // Custom events for scenes
    RollJamCustomEventSceneReceiverUpdate,
    RollJamCustomEventReceiverDeferredRxStart,
    RollJamCustomEventSceneSettingLock,
    // File management
    RollJamCustomEventReceiverInfoSave,
    RollJamCustomEventReceiverInfoSaveConfirm,
    RollJamCustomEventReceiverInfoEmulate,
    RollJamCustomEventReceiverInfoBruteforceStart,
    RollJamCustomEventReceiverInfoBruteforceCancel,
    RollJamCustomEventSavedInfoDelete,
    // Emulator
    RollJamCustomEventSavedInfoEmulate,
    RollJamCustomEventEmulateTransmit,
    RollJamCustomEventEmulateStop,
    RollJamCustomEventEmulateExit,
    // Sub decode
    RollJamCustomEventSubDecodeUpdate,
    RollJamCustomEventSubDecodeSave,
    RollJamCustomEventSubDecodeBruteforceStart,
    RollJamCustomEventPsaBruteforceComplete,
    // File Browser
    RollJamCustomEventSavedFileSelected,
    // Need saving confirmation
    RollJamCustomEventSceneStay,
    RollJamCustomEventSceneExit,
    // About scene
    RollJamCustomEventAboutToggleEmulate,
#ifdef ENABLE_DUAL_RX_SCENE
    // Dual RX scene
    RollJamCustomEventDualReceiverDeferredRxStart,
    RollJamCustomEventDualReceiverUpdate,
    RollJamCustomEventViewDualReceiverOK,
    RollJamCustomEventViewDualReceiverBack,
    RollJamCustomEventViewDualReceiverDeleteItem,
    RollJamCustomEventViewDualReceiverConfig,
#endif
#ifdef ENABLE_SHIELD_RX_SCENE
    RollJamCustomEventShieldReceiverDeferredStart,
    RollJamCustomEventShieldReceiverUpdate,
#endif
} RollJamCustomEvent;

typedef enum {
    RollJamLockOff,
    RollJamLockOn,
} RollJamLock;

typedef enum {
    RollJamTxRxStateIDLE,
    RollJamTxRxStateRx,
    RollJamTxRxStateTx,
    RollJamTxRxStateSleep,
} RollJamTxRxState;

typedef enum {
    RollJamHopperStateOFF,
    RollJamHopperStateRunning,
    RollJamHopperStatePause,
    RollJamHopperStateRSSITimeOut,
} RollJamHopperState;

typedef enum {
    RollJamRxKeyStateIDLE,
    RollJamRxKeyStateBack,
    RollJamRxKeyStateStart,
    RollJamRxKeyStateAddKey,
} RollJamRxKeyState;
