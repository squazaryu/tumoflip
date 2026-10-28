#pragma once
#include "field_io.h"

typedef bool (*FieldReportRow)(void* context, size_t index, char* row, size_t capacity);
bool field_report_save(
    Storage* storage, const char* kind, uint32_t timestamp, const char* header,
    size_t rows, FieldReportRow row, void* context, char* path, size_t capacity);
