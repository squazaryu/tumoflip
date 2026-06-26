// scenes/rolljam_scene_config.h

#include "../defines.h"

ADD_SCENE(rolljam, start, Start)
#ifdef ENABLE_SUB_DECODE_SCENE
ADD_SCENE(rolljam, sub_decode, SubDecode)
#endif
ADD_SCENE(rolljam, about, About)
#ifdef ENABLE_DUAL_RX_SCENE
ADD_SCENE(rolljam, dual_receiver, DualReceiver)
ADD_SCENE(rolljam, dual_receiver_config, DualReceiverConfig)
#endif
#ifdef ENABLE_SHIELD_RX_SCENE
ADD_SCENE(rolljam, shield_receiver, ShieldReceiver)
ADD_SCENE(rolljam, shield_receiver_config, ShieldReceiverConfig)
#endif
ADD_SCENE(rolljam, receiver_info, ReceiverInfo)
ADD_SCENE(rolljam, need_saving, NeedSaving)
ADD_SCENE(rolljam, saved, Saved)
ADD_SCENE(rolljam, saved_info, SavedInfo)
#ifdef ENABLE_EMULATE_FEATURE
ADD_SCENE(rolljam, emulate, Emulate)
#endif
#ifdef ENABLE_TIMING_TUNER_SCENE
ADD_SCENE(rolljam, timing_tuner, TimingTuner)
#endif
