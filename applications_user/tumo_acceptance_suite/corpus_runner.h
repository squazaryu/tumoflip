#pragma once
#include <storage/storage.h>
#include <lib/subghz/protocols/plugin_registry.h>

typedef struct CorpusRunner CorpusRunner;
CorpusRunner* corpus_runner_alloc(Storage* storage);
void corpus_runner_free(CorpusRunner* runner);
bool corpus_runner_add(CorpusRunner* runner, const char* path, SubGhzProtocolPackGroup pack);
bool corpus_runner_check(CorpusRunner* runner);
/** One bounded file chunk per UI tick; false once finished. */
bool corpus_runner_step(CorpusRunner* runner);
void corpus_runner_cancel(CorpusRunner* runner);
bool corpus_runner_can_save(const CorpusRunner* runner);
bool corpus_runner_save(CorpusRunner* runner);
const char* corpus_runner_text(CorpusRunner* runner);
uint8_t corpus_runner_progress(CorpusRunner* runner);
bool corpus_runner_export(CorpusRunner* runner);
