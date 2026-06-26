// scenes/protopirate_scene_config.h

#include "../defines.h"

ADD_SCENE(protopirate, start, Start)
#ifdef ENABLE_SUB_DECODE_SCENE
ADD_SCENE(protopirate, sub_decode, SubDecode)
#endif
ADD_SCENE(protopirate, about, About)
#ifdef ENABLE_DUAL_RX_SCENE
ADD_SCENE(protopirate, dual_receiver, DualReceiver)
ADD_SCENE(protopirate, dual_receiver_config, DualReceiverConfig)
#endif
#ifdef ENABLE_SHIELD_RX_SCENE
ADD_SCENE(protopirate, shield_receiver, ShieldReceiver)
ADD_SCENE(protopirate, shield_receiver_config, ShieldReceiverConfig)
#endif
ADD_SCENE(protopirate, receiver, Receiver)
ADD_SCENE(protopirate, receiver_config, ReceiverConfig)
ADD_SCENE(protopirate, receiver_info, ReceiverInfo)
ADD_SCENE(protopirate, need_saving, NeedSaving)
ADD_SCENE(protopirate, saved, Saved)
ADD_SCENE(protopirate, saved_info, SavedInfo)
#ifdef ENABLE_EMULATE_FEATURE
ADD_SCENE(protopirate, emulate, Emulate)
#endif
#ifdef ENABLE_TIMING_TUNER_SCENE
ADD_SCENE(protopirate, timing_tuner, TimingTuner)
#endif
