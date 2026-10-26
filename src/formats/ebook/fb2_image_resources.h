#ifndef QUIREWISE_FB2_IMAGE_RESOURCES_INTERNAL_H
#define QUIREWISE_FB2_IMAGE_RESOURCES_INTERNAL_H

#include "../../core/text_buffer.h"
#include "../../core/xml_parser.h"

int qw_fb2_append_embedded_image_html(QwTextBuffer *buffer, QwXmlNode *root,
                                      QwXmlNode *image, int *render_error);

#endif
