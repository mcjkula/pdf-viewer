#include "pdf_engine.h"

#include "poppler-document.h"
#include "poppler-page.h"

PopplerDocument *engine_get_document(const char *uri) {
    PopplerDocument *document = poppler_document_new_from_file(uri, NULL, NULL);

    return document;
}

PopplerPage *engine_get_page(PopplerDocument *document, int index) {
    PopplerPage *page = poppler_document_get_page(document, index);

    return page;
}

int engine_get_n_pages(PopplerDocument *document) {
    int pages_count = poppler_document_get_n_pages(document);

    return pages_count;
}

void engine_get_page_size(PopplerPage *page, double *width, double *height) {
    poppler_page_get_size(page, width, height);
}

void engine_render_page(PopplerPage *page, cairo_t *cr) {
    poppler_page_render(page, cr);
}

Engine *engine_new() {
    Engine *self = malloc(sizeof(Engine));

    if (!self)
        return NULL;

    self->get_document = engine_get_document;
    self->get_page = engine_get_page;
    self->get_n_pages = engine_get_n_pages;
    self->get_page_size = engine_get_page_size;

    return self;
}
