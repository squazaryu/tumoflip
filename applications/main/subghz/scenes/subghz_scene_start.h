#pragma once

enum SubmenuIndex {
    SubmenuIndexRead = 10,
    SubmenuIndexSaved,
    SubmenuIndexAddManually,
    SubmenuIndexAddManuallyAdvanced,
    SubmenuIndexFrequencyAnalyzer,
    SubmenuIndexReadRAW,
    SubmenuIndexExtSettings,
    SubmenuIndexRadioSetting,
#ifdef ARF_EXTERNAL_FULL
    SubmenuIndexArfKeeloq,
    SubmenuIndexArfCounter,
    SubmenuIndexArfCar,
    SubmenuIndexArfPsa,
    SubmenuIndexArfAnalyzer,
    SubmenuIndexProtoPirate,
    SubmenuIndexRollJam,
    SubmenuIndexSubBrute,
    SubmenuIndexArfStatus,
#endif
};
