typedef struct{const char*name;}StorageAnimationManifestInfo;
typedef struct{bool external;BubbleAnimation*animation;StorageAnimationManifestInfo manifest_info;}StorageAnimation;
typedef struct{StorageAnimation*items[2];unsigned count;}StorageAnimationList;
typedef StorageAnimationList StorageAnimationList_t[1];
typedef struct{void*desktop_profile;}AnimationManager;
typedef int Dolphin;
typedef struct{unsigned unused;}DolphinStats;
enum{DesktopProfileOrderSequential};
#define RECORD_DOLPHIN "dolphin"
#define HARDCODED_ANIMATION_NAME "Builtin"
#define M_EACH(item,list,type) (StorageAnimation**item=(list)[0].items;item<(list)[0].items+(list)[0].count;item++)
static BubbleAnimation builtin_frames;
static StorageAnimation builtin={.animation=&builtin_frames,.manifest_info={"Builtin"}};
static StorageAnimation candidate={.external=true,.manifest_info={"Fixture"}};
static unsigned discarded_candidates,builtin_selections;
static void StorageAnimationList_init(StorageAnimationList_t list){list[0].count=0;}
static void StorageAnimationList_clear(StorageAnimationList_t list){list[0].count=0;}
static void animation_storage_fill_animation_list(StorageAnimationList_t*list){list[0][0].items[0]=&candidate;list[0][0].count=1;}
static DolphinStats dolphin_stats(Dolphin*d){(void)d;return(DolphinStats){0};}
static bool desktop_profile_is_active(void*p){(void)p;return false;}
static int desktop_profile_get_order(void*p){(void)p;return DesktopProfileOrderSequential;}
static uint32_t animation_manager_filter_animation_list(AnimationManager*m,StorageAnimationList_t*l,DolphinStats*s,bool p){(void)m;(void)l;(void)s;(void)p;return 1;}
static void animation_manager_release_list(StorageAnimationList_t*l){l[0][0].count=0;}
static StorageAnimation*animation_manager_select_sequential(AnimationManager*m,StorageAnimationList_t*l){(void)m;(void)l;return NULL;}
static StorageAnimation*animation_manager_select_weighted(StorageAnimationList_t*l,uint32_t w){assert(w==1&&l[0][0].count==1);return l[0][0].items[0];}
static StorageAnimation*animation_storage_find_animation(const char*name){assert(!strcmp(name,HARDCODED_ANIMATION_NAME));builtin_selections++;return &builtin;}
static StorageAnimationManifestInfo*animation_storage_get_meta(StorageAnimation*a){return &a->manifest_info;}
static void animation_storage_free_storage_animation(StorageAnimation**a){assert(*a==&candidate);animation_storage_free_animation(&candidate.animation);*a=NULL;discarded_candidates++;}
static bool animation_storage_reload_frames(const char*n,BubbleAnimation*a){(void)n;(void)a;assert(!"unexpected cache reload");return false;}
