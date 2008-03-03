/** \file
 */

#include "packet-parse.h"

int ops_decompress(ops_region_t *region,ops_parse_info_t *parse_info,
		   ops_compression_type_t type);

ops_boolean_t ops_write_compressed(const unsigned char* data,
                                   const unsigned int len,
                                   ops_create_info_t *cinfo);
