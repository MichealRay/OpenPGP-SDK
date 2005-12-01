/** \file
 */

#include <sys/types.h>
#include <openssl/bn.h>
#include "packet.h"

#ifndef OPS_MEMORY_H
#define OPS_MEMORY_H

/** ops_memory_t
 */
typedef struct ops_memory ops_memory_t;

ops_memory_t *ops_memory_new(void);
void ops_memory_free(ops_memory_t *mem);
void ops_memory_init(ops_memory_t *mem,size_t initial_size);
void ops_memory_pad(ops_memory_t *mem,size_t length);
void ops_memory_add(ops_memory_t *mem,const unsigned char *src,size_t length);
void ops_memory_place_int(ops_memory_t *mem,unsigned offset,unsigned n,
			  size_t length);
void ops_memory_make_packet(ops_memory_t *out,ops_content_tag_t tag);
void ops_memory_clear(ops_memory_t *mem);
void ops_memory_release(ops_memory_t *mem);

void ops_writer_set_memory(ops_create_info_t *info,ops_memory_t *mem);

size_t ops_memory_get_length(const ops_memory_t *mem);
void *ops_memory_get_data(ops_memory_t *mem);

#endif
