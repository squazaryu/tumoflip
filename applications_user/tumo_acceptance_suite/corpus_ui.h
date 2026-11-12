#pragma once
#include <gui/view_dispatcher.h>
#include <storage/storage.h>
typedef struct CorpusUi CorpusUi;
CorpusUi* corpus_ui_alloc(Storage* storage, ViewDispatcher* dispatcher, uint32_t parent_view);
void corpus_ui_free(CorpusUi* ui);
void corpus_ui_show(CorpusUi* ui);
bool corpus_ui_event(CorpusUi* ui, uint32_t event);
void corpus_ui_tick(CorpusUi* ui);
