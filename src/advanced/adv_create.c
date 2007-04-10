/** \file
 */

#include <openpgpsdk/create.h>
#include <openpgpsdk/keyring.h>
#include "keyring_local.h"
#include <openpgpsdk/packet.h>
#include <openpgpsdk/util.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>

#include <openpgpsdk/final.h>

struct ops_writer_info
    {
    ops_writer_t *writer;
    ops_writer_finaliser_t *finaliser;
    ops_writer_destroyer_t *destroyer;
    void *arg;
    ops_writer_info_t *next;
    };

/**
 * \ingroup Create
 * This struct contains the required information about how to write
 */
struct ops_create_info
    {
    ops_writer_info_t winfo;
    ops_error_t *errors;	/*!< an error stack */
    };

/*
 * return true if OK, otherwise false
 */
static ops_boolean_t base_write(const void *src,unsigned length,
				ops_create_info_t *info)
    {
    return info->winfo.writer(src,length,&info->errors,&info->winfo);
    }

/**
 * \ingroup Create
 *
 * \param src
 * \param length
 * \param info
 * \return 1 if OK, otherwise 0
 */

ops_boolean_t ops_write(const void *src,unsigned length,
			ops_create_info_t *info)
    {
    return base_write(src,length,info);
    }

/**
 * \ingroup Create
 * \param n
 * \param length
 * \param info
 * \return ops_true if OK, otherwise ops_false
 */

ops_boolean_t ops_write_scalar(unsigned n,unsigned length,
			       ops_create_info_t *info)
    {
    while(length-- > 0)
	{
	unsigned char c[1];

	c[0]=n >> (length*8);
	if(!base_write(c,1,info))
	    return ops_false;
	}
    return ops_true;
    }

/** 
 * \ingroup Create
 * \param bn
 * \param info
 * \return 1 if OK, otherwise 0
 * \todo This statement about the return value is true based on the assumption
 *	that ops_true=1. Tidy this assumption up.
 */

ops_boolean_t ops_write_mpi(const BIGNUM *bn,ops_create_info_t *info)
    {
    unsigned char buf[8192];
    int bits=BN_num_bits(bn);

    assert(bits <= 65535);
    BN_bn2bin(bn,buf);
    return ops_write_scalar(bits,2,info)
	&& ops_write(buf,(bits+7)/8,info);
    }

/** 
 * \ingroup Create
 * \param tag
 * \param info
 * \return 1 if OK, otherwise 0
 */

ops_boolean_t ops_write_ptag(ops_content_tag_t tag,ops_create_info_t *info)
    {
    unsigned char c[1];

    c[0]=tag|OPS_PTAG_ALWAYS_SET|OPS_PTAG_NEW_FORMAT;

    return base_write(c,1,info);
    }

/** 
 * \ingroup Create
 * \param length
 * \param info
 * \return 1 if OK, otherwise 0
 */

ops_boolean_t ops_write_length(unsigned length,ops_create_info_t *info)
    {
    unsigned char c[2];

    if(length < 192)
	{
	c[0]=length;
	return base_write(c,1,info);
	}
    else if(length < 8384)
	{
	c[0]=((length-192) >> 8)+192;
	c[1]=(length-192)%256;
	return base_write(c,2,info);
	}
    return ops_write_scalar(0xff,1,info) && ops_write_scalar(length,4,info);
    }

/** 
 * \ingroup Create
 * \param length
 * \param type
 * \param info
 * \return 1 if OK, otherwise 0
 */

ops_boolean_t ops_write_ss_header(unsigned length,ops_content_tag_t type,
				  ops_create_info_t *info)
    {
    return ops_write_length(length,info)
	&& ops_write_scalar(type-OPS_PTAG_SIGNATURE_SUBPACKET_BASE,1,info);
    }

/* XXX: the general idea of _fast_ is that it doesn't copy stuff
 * the safe (i.e. non _fast_) version will, and so will also need to
 * be freed. */

/**
 * \ingroup Create
 *
 * ops_fast_create_user_id() sets id->user_id to the given user_id.
 * This is fast because it is only copying a char*. However, if user_id
 * is changed or freed in the future, this could have injurious results.
 * \param id
 * \param user_id
 */

void ops_fast_create_user_id(ops_user_id_t *id,unsigned char *user_id)
    {
    id->user_id=user_id;
    }

/**
 * \ingroup Create
 *
 * Writes a User Id from the information held in id and info
 *
 * \param id
 * \param info
 * \return Return value from ops_write() unless call to ops_write_ptag() or ops_write_length() failed before it was called, in which case returns 0
 * \todo tidy up that return value description!
 */
ops_boolean_t ops_write_struct_user_id(ops_user_id_t *id,
				       ops_create_info_t *info)
    {
    return ops_write_ptag(OPS_PTAG_CT_USER_ID,info)
	&& ops_write_length(strlen((char *)id->user_id),info)
	&& ops_write(id->user_id,strlen((char *)id->user_id),info);
    }

/**
 * \ingroup Create
 *
 * Write User Id
 * 
 * \param user_id
 * \param info
 *
 * \return return value from ops_write_struct_user_id()
 * \todo better descr of return value
 */
ops_boolean_t ops_write_user_id(const unsigned char *user_id,ops_create_info_t *info)
    {
    ops_user_id_t id;

    id.user_id=(unsigned char *)user_id;
    return ops_write_struct_user_id(&id,info);
    }

static unsigned mpi_length(const BIGNUM *bn)
    {
    return 2+(BN_num_bits(bn)+7)/8;
    }

static unsigned public_key_length(const ops_public_key_t *key)
    {
    switch(key->algorithm)
	{
    case OPS_PKA_RSA:
	return mpi_length(key->key.rsa.n)+mpi_length(key->key.rsa.e);

    default:
	assert(!"unknown key algorithm");
	}
    /* not reached */
    return 0;
    }

static unsigned secret_key_length(const ops_secret_key_t *key)
    {
    int l;

    switch(key->public_key.algorithm)
	{
    case OPS_PKA_RSA:
	l=mpi_length(key->key.rsa.d)+mpi_length(key->key.rsa.p)
	    +mpi_length(key->key.rsa.q)+mpi_length(key->key.rsa.u);
	break;

    default:
	assert(!"unknown key algorithm");
	}

    return l+public_key_length(&key->public_key);
    }

/** 
 * \ingroup Create
 * \param key
 * \param time
 * \param n
 * \param e
*/
void ops_fast_create_rsa_public_key(ops_public_key_t *key,time_t time,
				    BIGNUM *n,BIGNUM *e)
    {
    key->version=4;
    key->creation_time=time;
    key->algorithm=OPS_PKA_RSA;
    key->key.rsa.n=n;
    key->key.rsa.e=e;
    }

/* Note that we support v3 keys here because they're needed for
 * for verification - the writer doesn't allow them, though */
static ops_boolean_t write_public_key_body(const ops_public_key_t *key,
					   ops_create_info_t *info)
    {
    if(!(ops_write_scalar(key->version,1,info)
	 && ops_write_scalar(key->creation_time,4,info)))
	return ops_false;

    if(key->version != 4 && !ops_write_scalar(key->days_valid,2,info))
	return ops_false;

    if(!ops_write_scalar(key->algorithm,1,info))
	return ops_false;

    switch(key->algorithm)
	{
    case OPS_PKA_DSA:
	return ops_write_mpi(key->key.dsa.p,info)
	    && ops_write_mpi(key->key.dsa.q,info)
	    && ops_write_mpi(key->key.dsa.g,info)
	    && ops_write_mpi(key->key.dsa.y,info);

    case OPS_PKA_RSA:
    case OPS_PKA_RSA_ENCRYPT_ONLY:
    case OPS_PKA_RSA_SIGN_ONLY:
	return ops_write_mpi(key->key.rsa.n,info)
	    && ops_write_mpi(key->key.rsa.e,info);

    case OPS_PKA_ELGAMAL:
	return ops_write_mpi(key->key.elgamal.p,info)
	    && ops_write_mpi(key->key.elgamal.g,info)
	    && ops_write_mpi(key->key.elgamal.y,info);

    default:
	assert(0);
	break;
	}

    /* not reached */
    return ops_false;
    }

static void push_secret_key_checksum_writer(ops_create_info_t *info)
    {
    OPS_USED(info);
    // XXX: push a SHA-1 checksum writer (and change s2k to 254).
    }

static ops_boolean_t pop_secret_key_checksum_writer(ops_create_info_t *info)
    {
    // XXX: actually write a SHA-1 checksum, but for now, dummy...
    return ops_write_scalar(0,2,info);
    }


/* Note that we support v3 keys here because they're needed for
 * for verification - the writer doesn't allow them, though */
static ops_boolean_t write_secret_key_body(const ops_secret_key_t *key,
					   ops_create_info_t *info)
    {
    if(!write_public_key_body(&key->public_key,info))
	return ops_false;

    if(!ops_write_scalar(key->s2k_usage,1,info))
	return ops_false;
    
    // XXX: for now, no secret key encryption, so s2k == 0
    assert(key->s2k_usage == OPS_S2KU_NONE);

    push_secret_key_checksum_writer(info);

    switch(key->public_key.algorithm)
	{
	//    case OPS_PKA_DSA:
	//	return ops_write_mpi(key->key.dsa.x,info);

    case OPS_PKA_RSA:
    case OPS_PKA_RSA_ENCRYPT_ONLY:
    case OPS_PKA_RSA_SIGN_ONLY:
	if(!ops_write_mpi(key->key.rsa.d,info)
	   || !ops_write_mpi(key->key.rsa.p,info)
	   || !ops_write_mpi(key->key.rsa.q,info)
	   || !ops_write_mpi(key->key.rsa.u,info))
	    return ops_false;
	break;

	//    case OPS_PKA_ELGAMAL:
	//	return ops_write_mpi(key->key.elgamal.x,info);

    default:
	assert(0);
	break;
	}

    return pop_secret_key_checksum_writer(info);
    }


/**
 * \ingroup Create
 *
 * Writes a Public Key from the information held in "key" and "info"
 *
 * \param key
 * \param info
 * \return Return value from write_public_key_body() unless call to ops_write_ptag() or ops_write_length() failed before it was called, in which case returns 0
 * \todo tidy up that return value description!
 */
ops_boolean_t ops_write_struct_public_key(const ops_public_key_t *key,
					  ops_create_info_t *info)
    {
    assert(key->version == 4);

    return ops_write_ptag(OPS_PTAG_CT_PUBLIC_KEY,info)
	&& ops_write_length(1+4+1+public_key_length(key),info)
	&& write_public_key_body(key,info);
    }

/**
 * \ingroup Create
 *
 * Writes one RSA public key.
 *
 * The parameters for the public key are provided by "time", "n" and "e".
 *
 * This function expects "info" to specify a "writer" function to be used, for the
 * actual output.
 *
 * \sa See Detailed Description for usage.
 *
 * \param time Creation time
 * \param n RSA public modulus
 * \param e RSA public encryption exponent
 * \param info Writer setup
 *
 * \return result from ops_write_struct_public_key()
 * 
 * \todo get better definition of return values
 */

ops_boolean_t ops_write_rsa_public_key(time_t time,const BIGNUM *n,
				       const BIGNUM *e,
				       ops_create_info_t *info)
    {
    ops_public_key_t key;

    ops_fast_create_rsa_public_key(&key,time,DECONST(BIGNUM,n),
				   DECONST(BIGNUM,e));
    return ops_write_struct_public_key(&key,info);
    }

/**
 * \ingroup Create
 * \param out
 * \param key
 * \param make_packet
 */

void ops_build_public_key(ops_memory_t *out,const ops_public_key_t *key,
			  ops_boolean_t make_packet)
    {
    ops_create_info_t *info;

    info=ops_create_info_new();

    ops_memory_init(out,128);
    ops_writer_set_memory(info,out);

    write_public_key_body(key,info);

    if(make_packet)
	ops_memory_make_packet(out,OPS_PTAG_CT_PUBLIC_KEY);

    ops_create_info_delete(info);
    }

/**
 * \ingroup Create
 *
 * Create an RSA secret key structure. If a parameter is marked as
 * [OPTIONAL], then it can be omitted and will be calculated from
 * other parameters - or, in the case of e, will default to 0x10001.
 *
 * Parameters are _not_ copied, so will be freed if the structure is
 * freed.
 *
 * \param key The key structure to be initialised.
 * \param e The RSA parameter d (=e^-1 mod (p-1)(q-1)) [OPTIONAL]
 * \param p The RSA parameter p
 * \param q The RSA parameter q (q > p)
 * \param u The RSA parameter u (=p^-1 mod q) [OPTIONAL]
 * \param n The RSA public parameter n (=p*q) [OPTIONAL]
 * \param e The RSA public parameter e */

void ops_fast_create_rsa_secret_key(ops_secret_key_t *key,time_t time,
				    BIGNUM *d,BIGNUM *p,BIGNUM *q,BIGNUM *u,
				    BIGNUM *n,BIGNUM *e)
    {
    ops_fast_create_rsa_public_key(&key->public_key,time,n,e);

    // XXX: calculate optionals
    key->key.rsa.d=d;
    key->key.rsa.p=p;
    key->key.rsa.q=q;
    key->key.rsa.u=u;

    key->s2k_usage=OPS_S2KU_NONE;

    // XXX: sanity check and add errors...
    }

/**
 * \ingroup Create
 *
 * Writes a secret key.
 *
 * \param key The secret key
 * \param info
 * \return success
 */
ops_boolean_t ops_write_struct_secret_key(const ops_secret_key_t *key,
					  ops_create_info_t *info)
    {
    assert(key->public_key.version == 4);

    return ops_write_ptag(OPS_PTAG_CT_SECRET_KEY,info)
	&& ops_write_length(1+4+1+1+secret_key_length(key)+2,info)
	&& write_secret_key_body(key,info);
    }

/**
 * \ingroup Create
 *
 * Create a new ops_create_info_t structure.
 *
 * \return the new structure.
 */
ops_create_info_t *ops_create_info_new(void)
    { return ops_mallocz(sizeof(ops_create_info_t)); }

/* Note that we finalise from the top down, so we don't use writers below
 * that have already been finalised
 */
static ops_boolean_t writer_info_finalise(ops_error_t **errors,
					  ops_writer_info_t *winfo)
    {
    ops_boolean_t ret=ops_true;

    if(winfo->finaliser)
	{
	ret=winfo->finaliser(errors,winfo);
	winfo->finaliser=NULL;
	}
    if(winfo->next && !writer_info_finalise(errors,winfo->next))
	{
	winfo->finaliser=NULL;
	return ops_false;
	}
    return ret;
    }

static void writer_info_delete(ops_writer_info_t *winfo)
    {
    // we should have finalised before deleting
    assert(!winfo->finaliser);
    if(winfo->next)
	{
	writer_info_delete(winfo->next);
	free(winfo->next);
	winfo->next=NULL;
	}
    if(winfo->destroyer)
	{
	winfo->destroyer(winfo);
	winfo->destroyer=NULL;
	}
    winfo->writer=NULL;
    }

/**
 * \ingroup Create
 *
 * Delete an ops_create_info_t structure. If a writer is active, then
 * that is also deleted.
 *
 * \param info the structure to be deleted.
 */
void ops_create_info_delete(ops_create_info_t *info)
    {
    writer_info_delete(&info->winfo);
    free(info);
    }

typedef struct
    {
    int fd;
    } writer_fd_arg_t;

static ops_boolean_t fd_writer(const unsigned char *src,unsigned length,
			       ops_error_t **errors,
			       ops_writer_info_t *winfo)
    {
    writer_fd_arg_t *arg=ops_writer_get_arg(winfo);
    int n=write(arg->fd,src,length);

    if(n == -1)
	{
	OPS_SYSTEM_ERROR_1(errors,OPS_E_W_WRITE_FAILED,"write",
			   "file descriptor %d",arg->fd);
	return ops_false;
	}

    if((unsigned)n != length)
	{
	OPS_ERROR_1(errors,OPS_E_W_WRITE_TOO_SHORT,
		    "file descriptor %d",arg->fd);
	return ops_false;
	}

    return ops_true;
    }

static void fd_destroyer(ops_writer_info_t *winfo)
    {
    free(ops_writer_get_arg(winfo));
    }

/**
 * \ingroup Create
 *
 * Set the writer in info to be a stock writer that writes to a file
 * descriptor. If another writer has already been set, then that is
 * first destroyed.
 * 
 * \param info The info structure
 * \param fd The file descriptor
 *
 */

void ops_writer_set_fd(ops_create_info_t *info,int fd)
    {
    writer_fd_arg_t *arg=malloc(sizeof *arg);

    arg->fd=fd;
    ops_writer_set(info,fd_writer,NULL,fd_destroyer,arg);
    }

/**
 * \ingroup Create
 *
 * Set a writer in info. There should not be another writer set.
 *
 * \param info The info structure
 * \param writer The writer
 * \param destroyer The destroyer
 * \param arg The argument for the writer and destroyer
 */
void ops_writer_set(ops_create_info_t *info,
		    ops_writer_t *writer,
		    ops_writer_finaliser_t *finaliser,
		    ops_writer_destroyer_t *destroyer,
		    void *arg)
    {
    assert(!info->winfo.writer);
    info->winfo.writer=writer;
    info->winfo.finaliser=finaliser;
    info->winfo.destroyer=destroyer;
    info->winfo.arg=arg;
    }

/**
 * \ingroup Create
 *
 * Push a writer in info. There must already be another writer set.
 *
 * \param info The info structure
 * \param writer The writer
 * \param destroyer The destroyer
 * \param arg The argument for the writer and destroyer
 */
void ops_writer_push(ops_create_info_t *info,
		     ops_writer_t *writer,
		     ops_writer_finaliser_t *finaliser,
		     ops_writer_destroyer_t *destroyer,
		     void *arg)
    {
    ops_writer_info_t *copy=ops_mallocz(sizeof *copy);

    assert(info->winfo.writer);
    *copy=info->winfo;
    info->winfo.next=copy;

    info->winfo.writer=writer;
    info->winfo.finaliser=finaliser;
    info->winfo.destroyer=destroyer;
    info->winfo.arg=arg;
    }

void ops_writer_pop(ops_create_info_t *info)
    {
    ops_writer_info_t *next;

    // Make sure the finaliser has been called.
    assert(!info->winfo.finaliser);
    // Makew sure this is a stacked writer
    assert(info->winfo.next);
    if(info->winfo.destroyer)
	info->winfo.destroyer(&info->winfo);

    next=info->winfo.next;
    info->winfo=*next;

    free(next);
    }

/**
 * \ingroup Create
 *
 * Close the writer currently set in info.
 *
 * \param info The info structure
 */
ops_boolean_t ops_writer_close(ops_create_info_t *info)
    {
    ops_boolean_t ret=writer_info_finalise(&info->errors,&info->winfo);

    writer_info_delete(&info->winfo);

    return ret;
    }

/**
 * \ingroup Create
 *
 * Get the arg supplied to ops_create_info_set_writer().
 *
 * \param winfo The writer_info structure
 * \return The arg
 */
void *ops_writer_get_arg(ops_writer_info_t *winfo)
    { return winfo->arg; }

/**
 * \ingroup Create
 *
 * Write to the next writer down in the stack.
 *
 * \param src The data to write.
 * \param length The length of src.
 * \param flags The writer flags.
 * \param errors A place to store errors.
 * \param info The writer_info structure.
 * \return Success - if ops_false, then errors should contain the error.
 */
ops_boolean_t ops_stacked_write(const void *src,unsigned length,
				ops_error_t **errors,ops_writer_info_t *winfo)
    {
    return winfo->next->writer(src,length,errors,winfo->next);
    }

/**
 * \ingroup Create
 *
 * Free the arg. Many writers just have a malloc()ed lump of storage, this
 * function releases it.
 *
 * \param winfo the info structure.
 */
void ops_writer_generic_destroyer(ops_writer_info_t *winfo)
    { free(ops_writer_get_arg(winfo)); }

/**
 * \ingroup Create
 *
 * A writer that just writes to the next one down. Useful for when you
 * want to insert just a finaliser into the stack.
 */
ops_boolean_t ops_writer_passthrough(const unsigned char *src,
				     unsigned length,
				     ops_error_t **errors,
				     ops_writer_info_t *winfo)
    { return ops_stacked_write(src,length,errors,winfo); }

ops_pk_session_key_t *ops_create_pk_session_key(const ops_key_data_t *key)
    {
    ops_pk_session_key_t *session_key=ops_mallocz(sizeof *session_key);

    session_key->version=OPS_PKSK_V3;
    memcpy(session_key->key_id, key->key_id, sizeof session_key->key_id);
    // XXX: finish filling in the structure
    return session_key;
    }

// XXX: should these be common and just be called ops_crypt_*?
typedef struct _ops_encrypt_t ops_encrypt_t;
typedef void ops_encrypt_set_iv_t(ops_encrypt_t *encrypt,
				  const unsigned char *iv);
typedef void ops_encrypt_init_t(ops_encrypt_t *encrypt);
typedef void ops_encrypt_resync_t(ops_encrypt_t *encrypt);
typedef void ops_encrypt_block_encrypt_t(ops_encrypt_t *encrypt,void *out,
					 const void *in);
typedef void ops_encrypt_finish_t(ops_encrypt_t *encrypt);

/** _ops_encrypt_t */
struct _ops_encrypt_t
    {
    ops_symmetric_algorithm_t algorithm;
    size_t blocksize;
    size_t keysize;
    //    ops_encrypt_set_iv_t *set_iv; /* Call this before init! */
    ops_encrypt_set_iv_t *set_key; /* Call this before init! */
    ops_encrypt_init_t *base_init; /* Once the key is set, call this */
    ops_encrypt_resync_t *resync;
    //    ops_decrypt_decrypt_t *decrypt;
    ops_encrypt_block_encrypt_t *block_encrypt;
    ops_encrypt_finish_t *finish;
    unsigned char iv[OPS_MAX_BLOCK_SIZE];
    unsigned char civ[OPS_MAX_BLOCK_SIZE];
    unsigned char siv[OPS_MAX_BLOCK_SIZE]; /* Needed for weird v3 resync */
    unsigned char key[OPS_MAX_KEY_SIZE];
    size_t num;
    void *data;
    };

typedef struct
    {
    ops_encrypt_t *encrypter;
    } encrypted_arg_t;


/* dummy function */

#ifndef ATTRIBUTE_UNUSED
#define ATTRIBUTE_UNUSED __attribute__ ((__unused__))
#endif /* ATTRIBUTE_UNUSED */

void ops_write_pk_session_key(ops_create_info_t *info ATTRIBUTE_UNUSED, ops_pk_session_key_t *session_key ATTRIBUTE_UNUSED)
    {
    /* \todo write ops_write_pk_session_key() */
    assert(0);
    }

static ops_boolean_t encrypted_writer(const unsigned char *src ATTRIBUTE_UNUSED,
			      unsigned length ATTRIBUTE_UNUSED,
			      ops_error_t **errors ATTRIBUTE_UNUSED,
			      ops_writer_info_t *winfo ATTRIBUTE_UNUSED
			      )
    {
     /* \todo */
    assert(0);

    return ops_false;
    }

static ops_boolean_t encrypted_finaliser(ops_error_t **errors ATTRIBUTE_UNUSED,
					   ops_writer_info_t *winfo ATTRIBUTE_UNUSED)
    {
    /* \todo */
    assert(0);

    return ops_false;
    }

void encrypted_destroyer (ops_writer_info_t *winfo ATTRIBUTE_UNUSED)
     
    {
    /* \todo */
    assert(0);
    }

/* end of dummy code */

void ops_writer_push_encrypt(ops_create_info_t *info,
			     const ops_key_data_t *key)
    {
    ops_pk_session_key_t *session_key;
    encrypted_arg_t *arg=ops_mallocz(sizeof *arg);

    session_key=ops_create_pk_session_key(key);
    ops_write_pk_session_key(info,session_key);

    ops_write_ptag(OPS_PTAG_CT_SE_DATA,info);
    ops_writer_push(info,encrypted_writer,encrypted_finaliser,
		    encrypted_destroyer,arg);
    }