#pragma once
#include <gui/view.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Loading anonymous structure */
typedef struct Loading Loading;

/** Allocate and initialize
 *
 * This View used to show system is doing some processing
 *
 * @return     Loading View instance
 */
Loading* loading_alloc(void);

/** Deinitialize and free Loading View
 *
 * @param      instance  Loading instance
 */
void loading_free(Loading* instance);

/** Get Loading view
 *
 * @param      instance  Loading instance
 *
 * @return     View instance that can be used for embedding
 */
View* loading_get_view(Loading* instance);

/** Show how far along the work is, under the animation
 *
 * For work whose length is known up front and long enough that a spinner alone
 * leaves the user unsure anything is happening. The animation keeps running: the
 * bar says how much is left, the animation says the system is still alive.
 *
 * @param      instance  Loading instance
 * @param      progress  Fraction done, 0.0f to 1.0f, clamped
 */
void loading_set_progress(Loading* instance, float progress);

/** Hide the progress bar
 *
 * @param      instance  Loading instance
 */
void loading_reset_progress(Loading* instance);

/** Name the work in progress next to the animation
 *
 * With a label, the animation moves left and the label is centered in the remaining width.
 * A progress bar, when enabled, sits under the label. The text is copied into the view model.
 * NULL or an empty string restores the centered animation; progress state is unchanged.
 *
 * @param      instance  Loading instance
 * @param      text      Label; may be multiline
 */
void loading_set_text(Loading* instance, const char* text);

#ifdef __cplusplus
}
#endif
