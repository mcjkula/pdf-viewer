#ifndef PDF_ENGINE_H
#define PDF_ENGINE_H

#include "poppler.h"

typedef struct Engine {
    PopplerDocument *(*get_document)(const char *uri);
    PopplerPage *(*get_page)(PopplerDocument *document, int index);
    
    int (*get_n_pages)(PopplerDocument *document);
    void (*get_page_size)(PopplerPage *page, double *width, double *height);
    void (*render_page)(PopplerPage *page, cairo_t *cr);
} Engine;

Engine* engine_new();

#endif
