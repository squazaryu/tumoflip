#pragma once

#include "tumospectrum_types.h"

void tumospectrum_analyze(TumoSpectrumCapture* capture);
TumoSpectrumComparison tumospectrum_compare(
    const TumoSpectrumCapture* first,
    const TumoSpectrumCapture* second);
